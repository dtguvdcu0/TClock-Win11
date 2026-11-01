#include "tcdll.h"
#include "../common/format_bridge.h"
#include "format_collect.h"
#include <stdio.h>
#include <wchar.h>

extern HWND hwndTClockExeMain;
extern BOOL b_DebugLog;
extern HWND hwndClockMain;

/* Clock-thread side of common/format_bridge.h. Every entry point runs on the
   clock window thread; nothing here waits for the exe. */
/* Capture ticks kept per slot; older completions are rejected as unknown. */
#define FBR_TICKS 4u

typedef struct FBR_SLOT {
	LONG sequence;
	ULONGLONG revision;
	ULONGLONG published;
	/* First revision captured with the current format; older frames are never shown. */
	ULONGLONG first;
	ULONG formatHash;
	SYSTEMTIME publishedTime;
	int publishedBeat;
	TC_FORMAT_FRAME* frame;
	TC_FORMAT_FRAME* spare;
	/* Capture tick of the shown frame's source; freshness counts from capture, not arrival. */
	ULONGLONG frameTick;
	ULONGLONG ticks[FBR_TICKS];
	BOOL showing;
	/* Statistics logged when the route ends. */
	ULONGLONG frames, late, ageSum, ageMax;
} FBR_SLOT;

typedef struct FBR_STATE {
	BOOL enabled;
	UINT consumers;
	ULONGLONG generation;
	ULONGLONG lease;
	UINT intervalMs;
	HANDLE section;
	BYTE* view;
	FBR_SLOT slots[TCF_BRIDGE_SLOTS];
	UINT measured;
	/* Sources are published only after the first collection for this lease. */
	BOOL collected;
} FBR_STATE;
static FBR_STATE fbr;

/* Logs route transitions only, never every frame. */
static void fbr_log(const char* event, const WCHAR* text, ULONGLONG value)
{
	char line[320];
	int used;
	if (!b_DebugLog) return;
	used = sprintf_s(line, sizeof(line), "[format_bridge] %s", event);
	if (text && used > 0 && used < (int)sizeof(line) - 4) {
		line[used++] = ':';
		line[used++] = ' ';
		if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, line + used, (int)sizeof(line) - used, NULL, NULL)) line[used] = 0;
	}
	writeDebugLog_Win10(line, (int)value);
}

void fbr_reset(void)
{
	TC_FORMAT_FRAME* frames[TCF_BRIDGE_SLOTS][2];
	ULONGLONG generation = fbr.generation;
	UINT i;
	for (i = 0; i < TCF_BRIDGE_SLOTS; ++i) {
		const FBR_SLOT* slot = &fbr.slots[i];
		char event[160];
		if (!fbr.enabled || (!slot->frames && !slot->late)) continue;
		sprintf_s(event, sizeof(event), "stats frames %llu late %llu age avg %llu max %llu ms, consumer",
			slot->frames, slot->late, slot->frames ? slot->ageSum / slot->frames : 0, slot->ageMax);
		fbr_log(event, NULL, i + 1);
	}
	if (fbr.lease) fcl_close(fbr.lease);
	if (fbr.view) UnmapViewOfFile(fbr.view);
	if (fbr.section) CloseHandle(fbr.section);
	for (i = 0; i < TCF_BRIDGE_SLOTS; ++i) {
		frames[i][0] = fbr.slots[i].frame;
		frames[i][1] = fbr.slots[i].spare;
	}
	ZeroMemory(&fbr, sizeof(fbr));
	/* Keep storage and the last generation; a reused generation never revives a frame. */
	for (i = 0; i < TCF_BRIDGE_SLOTS; ++i) {
		fbr.slots[i].frame = frames[i][0];
		fbr.slots[i].spare = frames[i][1];
	}
	fbr.generation = generation;
}

static FBR_SLOT* fbr_slot(UINT consumer)
{
	if (!fbr.enabled || consumer < 1 || consumer > TCF_BRIDGE_SLOTS || !(fbr.consumers & (1u << (consumer - 1)))) return NULL;
	return &fbr.slots[consumer - 1];
}

/* Called by the source collector after each pass on the clock thread. */
void fbr_mark_collected(void)
{
	if (fbr.enabled) fbr.collected = TRUE;
}

BOOL fbr_active(void)
{
	return fbr_slot(TCF_CONSUMER_MAIN) != NULL;
}

static BOOL fbr_check_name(const WCHAR* name)
{
	WCHAR prefix[48];
	DWORD process = 0;
	size_t length = wcsnlen(name, TCF_BRIDGE_NAME_CCH);
	if (length == 0 || length == TCF_BRIDGE_NAME_CCH ||
		!GetWindowThreadProcessId(hwndTClockExeMain, &process) || !process) return FALSE;
	swprintf_s(prefix, _countof(prefix), L"Local\\TClockFormatBridge.%lu.", process);
	return !wcsncmp(name, prefix, wcslen(prefix));
}

/* Returns the clock to the built-in text right away; it may not redraw on its own
   for up to a minute, and the provider text can have a different width. */
static void fbr_release(BOOL enabled, BOOL shown)
{
	if (!enabled) return;
	fbr_log("disabled, generation", NULL, fbr.generation);
	PostMessageW(hwndClockMain, TCF_BRIDGE_REDRAW, 0, 0);
	if (shown) PostMessageW(hwndClockMain, CLOCKM_UPDATE_EXTTEXT, 0, 0);
}

static void fbr_configure(const TCF_BRIDGE_CONFIG* config)
{
	BOOL enabled = fbr.enabled, shown = fbr.slots[TCF_CONSUMER_MAIN - 1].showing;
	fbr_reset();
	if (!config->enabled || !TcfValidateDemand(&config->demand) || !(config->demand.consumers & TCF_BRIDGE_CONSUMERS) ||
		!fbr_check_name(config->section) || config->generation <= fbr.generation) {
		fbr_release(enabled, shown);
		return;
	}
	fbr.section = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, config->section);
	if (fbr.section) fbr.view = (BYTE*)MapViewOfFile(fbr.section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, TCF_BRIDGE_SECTION_BYTES);
	if (!fbr.view || fcl_open(TCF_CONSUMER_MAIN, &config->demand, &fbr.lease) != S_OK) {
		fbr.lease = 0;
		fbr_reset();
		fbr_release(enabled, shown);
		return;
	}
	fbr.generation = config->generation;
	fbr.consumers = config->demand.consumers & TCF_BRIDGE_CONSUMERS;
	fbr.intervalMs = config->demand.intervalMs;
	fbr.enabled = TRUE;
	fbr_log("enabled, generation", NULL, fbr.generation);
	PostMessageW(hwndClockMain, TCF_BRIDGE_REDRAW, 1, 0);
}

static void fbr_accept(const TCF_BRIDGE_FRAME* reply)
{
	FBR_SLOT* slot = fbr_slot(reply->consumer);
	TC_FORMAT_FRAME* frame;
	ULONGLONG now = GetTickCount64(), captured, age;
	if (!slot || reply->generation != fbr.generation) return;
	if (!slot->spare) slot->spare = (TC_FORMAT_FRAME*)HeapAlloc(GetProcessHeap(), 0, sizeof(*slot->spare));
	if (!slot->spare) return;
	/* Validate an aligned private copy; the message buffer is not retained and an
	   invalid candidate leaves the current frame untouched. */
	frame = slot->spare;
	CopyMemory(frame, &reply->frame, sizeof(*frame));
	if (frame->sourceRevision <= slot->revision || frame->sourceRevision > slot->published ||
		frame->sourceRevision < slot->first ||
		!TcfValidateFrame(frame, sizeof(*frame), frame->sourceRevision, TCF_FRAME_CCH)) return;
	/* A result older than the age limit is already stale: keep the current state. */
	captured = slot->ticks[frame->sourceRevision % FBR_TICKS];
	age = now - captured;
	if (slot->published - frame->sourceRevision >= FBR_TICKS || age > TCF_BRIDGE_FRAME_AGE_MS) {
		++slot->late;
		return;
	}
	++slot->frames;
	slot->ageSum += age;
	if (age > slot->ageMax) slot->ageMax = age;
	slot->spare = slot->frame;
	slot->frame = frame;
	slot->revision = frame->sourceRevision;
	slot->frameTick = captured;
	PostMessageW(hwndClockMain, reply->consumer == TCF_CONSUMER_MAIN ? TCF_BRIDGE_REDRAW : TCF_BRIDGE_TOOLTIP, 0, 0);
}

BOOL fbr_receive(HWND sender, const COPYDATASTRUCT* data)
{
	if (!data || (data->dwData != TCF_BRIDGE_CONFIG_DATA && data->dwData != TCF_BRIDGE_FRAME_DATA)) return FALSE;
	if (!sender || sender != hwndTClockExeMain || !data->lpData) return TRUE;
	if (data->dwData == TCF_BRIDGE_CONFIG_DATA) {
		TCF_BRIDGE_CONFIG config;
		if (data->cbData != sizeof(config)) return TRUE;
		CopyMemory(&config, data->lpData, sizeof(config));
		if (config.size == sizeof(config)) fbr_configure(&config);
	}
	else if (data->cbData == sizeof(TCF_BRIDGE_FRAME)) fbr_accept((const TCF_BRIDGE_FRAME*)data->lpData);
	return TRUE;
}

static ULONG fbr_hash(const WCHAR* text)
{
	ULONG hash = 2166136261u;
	for (; *text; ++text) hash = (hash ^ *text) * 16777619u;
	return hash;
}

static void fbr_publish(UINT consumer, FBR_SLOT* slot, const SYSTEMTIME* time, int beat100, const WCHAR* formatText)
{
	TCF_BRIDGE_HEADER* header = (TCF_BRIDGE_HEADER*)(fbr.view + (consumer - 1) * TCF_BRIDGE_SLOT_BYTES);
	TC_FORMAT_SOURCE* source = NULL;
	UINT bytes;
	if (!fbr.view || slot->published == ~(ULONGLONG)0 ||
		FAILED(FormatCaptureSourceW(time, NULL, beat100, formatText, consumer, slot->published + 1, 0, &source))) return;
	bytes = source->bytes;
	if (bytes <= TCF_SOURCE_MAX_BYTES && TcfValidateSource(source, bytes)) {
		InterlockedExchange(&header->sequence, ++slot->sequence);
		MemoryBarrier();
		CopyMemory((BYTE*)header + TCF_BRIDGE_HEADER_BYTES, source, bytes);
		header->bytes = bytes;
		header->generation = fbr.generation;
		header->revision = source->revision;
		MemoryBarrier();
		InterlockedExchange(&header->sequence, ++slot->sequence);
		slot->published = source->revision;
		slot->ticks[source->revision % FBR_TICKS] = GetTickCount64();
		slot->publishedTime = *time;
		slot->publishedBeat = beat100;
		PostMessageW(hwndTClockExeMain, TCF_BRIDGE_READY, (WPARAM)fbr.generation, (LPARAM)consumer);
	}
	TcfFreeSource(source);
}

/* Publishes a source when the format or displayed second changes (or the provider
   interval elapses) and returns the consumer's frame while it is fresh and fits.
   *changed reports a shown/fallback transition. */
static const TC_FORMAT_FRAME* fbr_take(UINT consumer, int capacity, const SYSTEMTIME* time, int beat100,
	const WCHAR* formatText, BOOL publish, BOOL* changed)
{
	FBR_SLOT* slot = fbr_slot(consumer);
	SYSTEMTIME second, previous;
	ULONGLONG now = GetTickCount64();
	ULONG hash;
	*changed = FALSE;
	if (!slot || !formatText) return NULL;
	hash = fbr_hash(formatText);
	if (hash != slot->formatHash) {
		slot->formatHash = hash;
		slot->first = slot->published + 1;
		slot->frameTick = 0;
		publish = TRUE;
	}
	second = *time;
	previous = slot->publishedTime;
	second.wMilliseconds = previous.wMilliseconds = 0;
	if (publish && fbr.collected && (slot->first > slot->published || memcmp(&second, &previous, sizeof(second)) ||
		beat100 != slot->publishedBeat ||
		(fbr.intervalMs < 1000 && now - slot->frameTick >= fbr.intervalMs && slot->published == slot->revision)))
		fbr_publish(consumer, slot, time, beat100, formatText);
	if (!slot->frame || !slot->frameTick || now - slot->frameTick > TCF_BRIDGE_FRAME_AGE_MS ||
		(int)slot->frame->textLength >= capacity) {
		if (slot->showing) {
			if (consumer == TCF_CONSUMER_MAIN) fbr_log("built-in fallback, last revision", NULL, slot->revision);
			else fbr_log("aux built-in fallback, consumer", NULL, consumer);
			*changed = TRUE;
		}
		slot->showing = FALSE;
		return NULL;
	}
	if (!slot->showing) {
		if (consumer == TCF_CONSUMER_MAIN) fbr_log("provider frame shown", slot->frame->text, slot->revision);
		else fbr_log("aux frame shown, consumer", slot->frame->text, consumer);
		*changed = TRUE;
	}
	slot->showing = TRUE;
	return slot->frame;
}

/* Re-measures the clock when the shown text changes source or length. The handler
   only relayouts the tray when the measured size actually changed. */
static void fbr_resize(UINT length)
{
	fbr.measured = length;
	PostMessageW(hwndClockMain, CLOCKM_UPDATE_EXTTEXT, 0, 0);
}

/* Main clock text. FALSE selects the built-in formatter; outputs are untouched then. */
BOOL fbr_format(WCHAR* text, int capacity, char* info, const SYSTEMTIME* time, int beat100,
	const WCHAR* formatText, TC_FORMAT_SPANS* spans)
{
	BOOL changed;
	const TC_FORMAT_FRAME* frame = fbr_take(TCF_CONSUMER_MAIN, capacity, time, beat100, formatText, TRUE, &changed);
	UINT i;
	if (!frame) {
		if (changed) fbr_resize(~0u);
		return FALSE;
	}
	if (frame->textLength != fbr.measured) fbr_resize(frame->textLength);
	CopyMemory(text, frame->text, (frame->textLength + 1) * sizeof(WCHAR));
	CopyMemory(info, frame->info, frame->textLength + 1);
	ZeroMemory(spans, sizeof(*spans));
	for (i = 0; i < frame->spanCount && i < TC_FORMAT_MAX_SPANS; ++i) {
		const TCF_FRAME_SPAN* from = &frame->spans[i];
		TC_FORMAT_SPAN* to = &spans->items[i];
		to->start = (int)from->start;
		to->length = (int)from->length;
		to->occurrence = (int)from->occurrence;
		/* Span zone only seeds value rewrites; ACCENT (88) has no such role here, so map it
		   to unspecified. Per-character info bytes keep 88 and use the host accent color. */
		to->zone = from->zone == TCF_ZONE_ACCENT ? 0 : (BYTE)from->zone;
		/* TCF_STYLE_* and TC_FS_PROP_* share bit values for these nine fields. */
		to->style.setMask = from->style.fields & TCF_STYLE_ALL;
		to->style.foreColor = from->style.foreColor;
		to->style.shadowColor = from->style.shadowColor;
		to->style.fontSize = from->style.fontSize;
		to->style.shadowRange = from->style.shadowRange;
		to->style.bold = (BYTE)!!(from->style.traits & TCF_TRAIT_BOLD);
		to->style.italic = (BYTE)!!(from->style.traits & TCF_TRAIT_ITALIC);
		to->style.shadow = (BYTE)!!(from->style.traits & TCF_TRAIT_SHADOW);
		to->style.border = (BYTE)!!(from->style.traits & TCF_TRAIT_BORDER);
		wcsncpy_s(to->style.fontFace, _countof(to->style.fontFace), from->style.fontFace, _TRUNCATE);
		wcsncpy_s(to->selector, _countof(to->selector), from->selector, _TRUNCATE);
	}
	spans->count = (int)i;
	spans->resolved = TRUE;
	if (!TcFormatStyleValidateFrame(text, (int)frame->textLength, spans)) {
		if (changed) fbr_log("provider spans rejected, count", NULL, i);
		ZeroMemory(spans, sizeof(*spans));
		spans->resolved = TRUE;
	}
	else if (changed) fbr_log("provider spans accepted, count", NULL, i);
	return TRUE;
}

/* Clock timer hook while the main consumer is routed. Publishes this tick's source and
   returns whether the tick itself must draw: not while a fresh provider frame is shown,
   because that frame was already drawn and the next accepted frame redraws. */
BOOL fbr_tick(const SYSTEMTIME* time, int beat100, const WCHAR* formatText)
{
	FBR_SLOT* slot = fbr_slot(TCF_CONSUMER_MAIN);
	BOOL changed;
	if (!slot) return FALSE;
	if (!slot->showing) return TRUE;
	if (fbr_take(TCF_CONSUMER_MAIN, TCF_FRAME_CCH + 1, time, beat100, formatText, TRUE, &changed)) return FALSE;
	/* The frame expired here, so the draw no longer sees the transition: re-measure now. */
	if (changed) fbr_resize(~0u);
	return TRUE;
}

/* Plain-text consumers (tooltip body/title). publish = FALSE only re-reads the
   current frame, so a refresh caused by an accepted frame cannot publish again. */
BOOL fbr_format_text(UINT consumer, WCHAR* text, int capacity, const SYSTEMTIME* time, int beat100,
	const WCHAR* formatText, BOOL publish)
{
	BOOL changed;
	const TC_FORMAT_FRAME* frame;
	if (consumer == TCF_CONSUMER_MAIN) return FALSE;
	frame = fbr_take(consumer, capacity, time, beat100, formatText, publish, &changed);
	if (!frame) return FALSE;
	CopyMemory(text, frame->text, (frame->textLength + 1) * sizeof(WCHAR));
	return TRUE;
}
