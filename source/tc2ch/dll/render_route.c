#include "tcdll.h"
#include "../common/render_bridge.h"
#include <stdio.h>
#include <wchar.h>

extern HWND hwndTClockExeMain;
extern HWND hwndClockMain;
extern BOOL b_DebugLog;
extern COLORREF colfore, colback;

/* Clock-thread side of common/render_bridge.h. Nothing here waits for the exe. */
#define RRT_TICKS 4u

typedef struct RRT_STATE {
	BOOL enabled, showing, excluded;
	ULONGLONG generation;
	HANDLE section;
	BYTE* view;
	LONG sequence;
	ULONGLONG published, accepted;
	/* Request tick per revision: frame age counts from the request. */
	ULONGLONG ticks[RRT_TICKS];
	SYSTEMTIME publishedTime;
	ULONG publishedKey;
	UINT width, height, preferredWidth, preferredHeight, measuredWidth, measuredHeight;
	ULONGLONG frameTick;
	UINT* pixels;
	UINT* spare;
	SIZE_T capacity, spareCapacity;
	ULONGLONG frames, late, invalid;
} RRT_STATE;
static RRT_STATE rrt;

static void rrt_log(const char* event, const WCHAR* text, ULONGLONG value)
{
	char line[320];
	int used;
	if (!b_DebugLog) return;
	used = sprintf_s(line, sizeof(line), "[render_route] %s", event);
	if (text && used > 0 && used < (int)sizeof(line) - 4) {
		line[used++] = ':';
		line[used++] = ' ';
		if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, line + used, (int)sizeof(line) - used, NULL, NULL)) line[used] = 0;
	}
	writeDebugLog_Win10(line, (int)value);
}

void rrt_reset(void)
{
	UINT* pixels = rrt.pixels;
	UINT* spare = rrt.spare;
	SIZE_T capacity = rrt.capacity, spareCapacity = rrt.spareCapacity;
	ULONGLONG generation = rrt.generation;
	if (rrt.enabled && (rrt.frames || rrt.late || rrt.invalid)) {
		char event[128];
		sprintf_s(event, sizeof(event), "stats frames %llu late %llu invalid %llu, generation", rrt.frames, rrt.late, rrt.invalid);
		rrt_log(event, NULL, generation);
	}
	if (rrt.view) UnmapViewOfFile(rrt.view);
	if (rrt.section) CloseHandle(rrt.section);
	ZeroMemory(&rrt, sizeof(rrt));
	/* Keep storage and the last generation; a reused generation never revives a frame. */
	rrt.pixels = pixels;
	rrt.spare = spare;
	rrt.capacity = capacity;
	rrt.spareCapacity = spareCapacity;
	rrt.generation = generation;
}

BOOL rrt_active(void)
{
	return rrt.enabled;
}

/* TRUE while the last draw composed a provider frame instead of the text. */
BOOL rrt_showing(void)
{
	return rrt.enabled && rrt.showing;
}

static BOOL rrt_check_name(const WCHAR* name)
{
	WCHAR prefix[48];
	DWORD process = 0;
	size_t length = wcsnlen(name, TCR_NAME_CCH);
	if (length == 0 || length == TCR_NAME_CCH ||
		!GetWindowThreadProcessId(hwndTClockExeMain, &process) || !process) return FALSE;
	swprintf_s(prefix, _countof(prefix), L"Local\\TClockRenderBridge.%lu.", process);
	return !wcsncmp(name, prefix, wcslen(prefix));
}

/* Back to built-in text right away, at the built-in size. */
static void rrt_release(BOOL enabled, BOOL measured)
{
	if (!enabled) return;
	rrt_log("disabled, generation", NULL, rrt.generation);
	PostMessageW(hwndClockMain, TCR_REDRAW, 0, 0);
	if (measured) PostMessageW(hwndClockMain, CLOCKM_UPDATE_EXTTEXT, 0, 0);
}

static void rrt_configure(const TCR_CONFIG* config)
{
	BOOL enabled = rrt.enabled, measured = rrt.measuredWidth || rrt.measuredHeight;
	rrt_reset();
	if (!config->enabled || !rrt_check_name(config->section) || config->generation <= rrt.generation) {
		rrt_release(enabled, measured);
		return;
	}
	rrt.section = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, config->section);
	if (rrt.section) rrt.view = (BYTE*)MapViewOfFile(rrt.section, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, TCR_SECTION_BYTES);
	if (!rrt.view) {
		rrt_reset();
		rrt_release(enabled, measured);
		return;
	}
	rrt.generation = config->generation;
	rrt.enabled = TRUE;
	rrt_log("enabled, generation", NULL, rrt.generation);
	PostMessageW(hwndClockMain, TCR_REDRAW, 0, 0);
}

static BOOL rrt_reserve(UINT** buffer, SIZE_T* capacity, SIZE_T bytes)
{
	UINT* grown;
	if (*capacity >= bytes) return TRUE;
	grown = *buffer ? (UINT*)HeapReAlloc(GetProcessHeap(), 0, *buffer, bytes) : (UINT*)HeapAlloc(GetProcessHeap(), 0, bytes);
	if (!grown) return FALSE;
	*buffer = grown;
	*capacity = bytes;
	return TRUE;
}

/* Validates a whole frame into the spare buffer; an invalid or stale frame leaves the
   current one untouched. */
static BOOL rrt_accept(const TCR_FRAME* frame, DWORD bytes)
{
	const UINT* from = (const UINT*)(frame + 1);
	ULONGLONG now = GetTickCount64(), age;
	SIZE_T count, i;
	UINT* swap;
	if (!rrt.enabled || frame->generation != rrt.generation) return FALSE;
	if (!frame->width || !frame->height || frame->width > TC_RENDER_MAX_WIDTH || frame->height > TC_RENDER_MAX_HEIGHT ||
		bytes != sizeof(*frame) + (SIZE_T)frame->width * frame->height * 4 ||
		frame->preferredWidth > TC_RENDER_MAX_WIDTH || frame->preferredHeight > TC_RENDER_MAX_HEIGHT ||
		frame->regionCount > TC_RENDER_MAX_REGIONS) {
		++rrt.invalid;
		return FALSE;
	}
	for (i = 0; i < frame->regionCount; ++i) {
		const TC_RENDER_REGION* region = &frame->regions[i];
		if (region->zone < 1 || region->zone > TC_RENDER_MAX_ZONE || region->bounds.left < 0 || region->bounds.top < 0 ||
			region->bounds.left >= region->bounds.right || region->bounds.top >= region->bounds.bottom ||
			region->bounds.right > (LONG)frame->width || region->bounds.bottom > (LONG)frame->height) {
			++rrt.invalid;
			return FALSE;
		}
	}
	if (frame->revision <= rrt.accepted || frame->revision > rrt.published || rrt.published - frame->revision >= RRT_TICKS) return FALSE;
	age = now - rrt.ticks[frame->revision % RRT_TICKS];
	if (age > TCR_FRAME_AGE_MS) {
		++rrt.late;
		return FALSE;
	}
	count = (SIZE_T)frame->width * frame->height;
	if (!rrt_reserve(&rrt.spare, &rrt.spareCapacity, count * 4)) return FALSE;
	for (i = 0; i < count; ++i) {
		UINT pixel = from[i], alpha = pixel >> 24;
		if (((pixel >> 16) & 0xFF) > alpha || ((pixel >> 8) & 0xFF) > alpha || (pixel & 0xFF) > alpha) {
			++rrt.invalid;
			return FALSE;
		}
		rrt.spare[i] = pixel;
	}
	swap = rrt.pixels;
	rrt.pixels = rrt.spare;
	rrt.spare = swap;
	{
		SIZE_T capacity = rrt.capacity;
		rrt.capacity = rrt.spareCapacity;
		rrt.spareCapacity = capacity;
	}
	rrt.accepted = frame->revision;
	rrt.frameTick = rrt.ticks[frame->revision % RRT_TICKS];
	rrt.width = frame->width;
	rrt.height = frame->height;
	if (frame->preferredWidth != rrt.preferredWidth || frame->preferredHeight != rrt.preferredHeight) {
		rrt.preferredWidth = frame->preferredWidth;
		rrt.preferredHeight = frame->preferredHeight;
		PostMessageW(hwndClockMain, CLOCKM_UPDATE_EXTTEXT, 0, 0);
	}
	++rrt.frames;
	PostMessageW(hwndClockMain, TCR_REDRAW, 0, 0);
	return TRUE;
}

/* *result is the WM_COPYDATA result: nonzero for a taken configuration or an accepted frame. */
BOOL rrt_receive(HWND sender, const COPYDATASTRUCT* data, LRESULT* result)
{
	if (!data || (data->dwData != TCR_CONFIG_DATA && data->dwData != TCR_FRAME_DATA)) return FALSE;
	*result = FALSE;
	if (!sender || sender != hwndTClockExeMain || !data->lpData) return TRUE;
	if (data->dwData == TCR_CONFIG_DATA) {
		TCR_CONFIG config;
		if (data->cbData != sizeof(config)) return TRUE;
		CopyMemory(&config, data->lpData, sizeof(config));
		if (config.size == sizeof(config)) rrt_configure(&config);
		*result = TRUE;
	}
	else if (data->cbData >= sizeof(TCR_FRAME)) *result = rrt_accept((const TCR_FRAME*)data->lpData, data->cbData);
	return TRUE;
}

static ULONG rrt_hash(const TC_RENDER_REQUEST* request)
{
	const BYTE* bytes = (const BYTE*)&request->flags;
	ULONG hash = 2166136261u;
	SIZE_T i, length = 7 * sizeof(UINT);
	for (i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
	for (i = 0; i < TC_RENDER_TEXT_CCH && request->text[i]; ++i) hash = (hash ^ request->text[i]) * 16777619u;
	return hash;
}

static void rrt_publish(TC_RENDER_REQUEST* request)
{
	TCR_HEADER* header = (TCR_HEADER*)rrt.view;
	if (!header || rrt.published == ~(ULONGLONG)0) return;
	request->revision = rrt.published + 1;
	InterlockedExchange(&header->sequence, ++rrt.sequence);
	MemoryBarrier();
	CopyMemory(rrt.view + TCR_HEADER_BYTES, request, sizeof(*request));
	header->generation = rrt.generation;
	header->revision = request->revision;
	MemoryBarrier();
	InterlockedExchange(&header->sequence, ++rrt.sequence);
	rrt.published = request->revision;
	rrt.ticks[request->revision % RRT_TICKS] = GetTickCount64();
	PostMessageW(hwndTClockExeMain, TCR_READY, (WPARAM)rrt.generation, 0);
}

/* Publishes the request for this draw (once per displayed second or on any input
   change) and returns TRUE when a fresh frame of exactly this size replaces the
   built-in text. `excluded` names a display mode outside the contract. */
BOOL rrt_take(const SYSTEMTIME* time, const WCHAR* text, UINT width, UINT height, UINT dpi, UINT flags, const char* excluded)
{
	TC_RENDER_REQUEST request;
	SYSTEMTIME second, previous;
	ULONGLONG now = GetTickCount64();
	ULONG key;
	BOOL usable;
	if (!rrt.enabled) return FALSE;
	if (excluded) {
		if (!rrt.excluded) rrt_log(excluded, NULL, 0);
		rrt.excluded = TRUE;
		return FALSE;
	}
	rrt.excluded = FALSE;
	if (!width || !height || width > TC_RENDER_MAX_WIDTH || height > TC_RENDER_MAX_HEIGHT) return FALSE;
	ZeroMemory(&request, sizeof(request));
	request.size = sizeof(request);
	request.flags = flags;
	request.width = width;
	request.height = height;
	request.dpi = dpi;
	request.foreColor = colfore;
	request.backColor = colback;
	request.time = *time;
	request.time.wMilliseconds = 0;
	if (text) wcsncpy_s(request.text, _countof(request.text), text, _TRUNCATE);
	key = rrt_hash(&request);
	second = *time;
	previous = rrt.publishedTime;
	second.wMilliseconds = previous.wMilliseconds = 0;
	if (key != rrt.publishedKey || memcmp(&second, &previous, sizeof(second))) {
		rrt_publish(&request);
		rrt.publishedKey = key;
		rrt.publishedTime = second;
	}
	usable = rrt.frameTick && rrt.width == width && rrt.height == height && now - rrt.frameTick <= TCR_FRAME_AGE_MS;
	if (usable != rrt.showing) {
		rrt_log(usable ? "provider frame shown, revision" : "built-in fallback, revision", NULL, rrt.accepted);
		if (!usable && rrt.preferredWidth) PostMessageW(hwndClockMain, CLOCKM_UPDATE_EXTTEXT, 0, 0);
	}
	rrt.showing = usable;
	return usable;
}

/* Source-over of the premultiplied frame onto the clock's premultiplied buffer. */
void rrt_blend(RGBQUAD* buffer, UINT width, UINT height, BOOL topDown)
{
	UINT x, y;
	if (!rrt.showing || !buffer || width != rrt.width || height != rrt.height) return;
	for (y = 0; y < height; ++y) {
		const UINT* from = rrt.pixels + (SIZE_T)y * width;
		RGBQUAD* to = buffer + (SIZE_T)(topDown ? y : height - 1 - y) * width;
		for (x = 0; x < width; ++x) {
			UINT pixel = from[x], alpha = pixel >> 24, keep = 255 - alpha;
			if (!alpha) continue;
			to[x].rgbBlue = (BYTE)((pixel & 0xFF) + to[x].rgbBlue * keep / 255);
			to[x].rgbGreen = (BYTE)(((pixel >> 8) & 0xFF) + to[x].rgbGreen * keep / 255);
			to[x].rgbRed = (BYTE)(((pixel >> 16) & 0xFF) + to[x].rgbRed * keep / 255);
			to[x].rgbReserved = (BYTE)(alpha + to[x].rgbReserved * keep / 255);
		}
	}
}

/* Content size requested by the provider while its frames are current. */
BOOL rrt_measure(LONG* width, LONG* height)
{
	ULONGLONG now = GetTickCount64();
	if (!rrt.enabled || rrt.excluded || !rrt.preferredWidth || !rrt.preferredHeight ||
		!rrt.frameTick || now - rrt.frameTick > TCR_FRAME_AGE_MS) {
		rrt.measuredWidth = rrt.measuredHeight = 0;
		return FALSE;
	}
	*width = (LONG)rrt.preferredWidth;
	*height = (LONG)rrt.preferredHeight;
	rrt.measuredWidth = rrt.preferredWidth;
	rrt.measuredHeight = rrt.preferredHeight;
	return TRUE;
}
