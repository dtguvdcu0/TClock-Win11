#include "tclock.h"
#include "plugin_render.h"
#include "plugin_runtime.h"
#include <stdio.h>
#include <string.h>

extern BOOL b_DebugLog;

/* Exe side of common/render_bridge.h: renders the newest request of the clock with the
   selected tclock.render/1 provider on the runtime UI thread and returns the image. */
typedef struct PRN_STATE {
	BOOL active;
	PRT_FEATURE_REF owner;
	ULONGLONG generation;
	HWND clock;
	HANDLE section;
	const BYTE* view;
	WCHAR name[TCR_NAME_CCH];
	ULONGLONG done;
	HRESULT lastResult;
	TCR_FRAME* reply;
	SIZE_T replyBytes;
	/* Regions of the last frame the clock accepted, in its image coordinates. */
	ULONGLONG regionTick;
	UINT regionWidth, regionHeight, regionCount;
	TC_RENDER_REGION regions[TC_RENDER_MAX_REGIONS];
} PRN_STATE;
static PRN_STATE prn;

typedef struct PRN_CALL {
	const TC_RENDER_REQUEST* request;
	TC_RENDER_OUTPUT* output;
} PRN_CALL;

static void prn_log(const char* event, ULONGLONG value)
{
	char line[128];
	if (!b_DebugLog) return;
	sprintf_s(line, sizeof(line), "[plugin_render] %s %llu", event, value);
	WriteDebug_New2(line);
}

static BOOL prn_send(HWND clock, HWND owner, ULONG_PTR kind, const void* data, DWORD bytes)
{
	COPYDATASTRUCT copy;
	DWORD_PTR result = 0;
	copy.dwData = kind;
	copy.cbData = bytes;
	copy.lpData = (void*)data;
	return clock && IsWindow(clock) &&
		SendMessageTimeoutW(clock, WM_COPYDATA, (WPARAM)owner, (LPARAM)&copy,
			SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result) && result;
}

static void prn_close_section(void)
{
	if (prn.view) UnmapViewOfFile(prn.view);
	if (prn.section) CloseHandle(prn.section);
	prn.view = NULL;
	prn.section = NULL;
	prn.name[0] = 0;
}

/* The name carries the exe PID so the clock can reject a section it was not offered. */
static BOOL prn_open_section(void)
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	swprintf_s(prn.name, _countof(prn.name), L"Local\\TClockRenderBridge.%lu.%016llx",
		GetCurrentProcessId(), (unsigned long long)counter.QuadPart ^ GetTickCount64());
	prn.section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, TCR_SECTION_BYTES, prn.name);
	if (prn.section && GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(prn.section);
		prn.section = NULL;
	}
	if (prn.section) prn.view = (const BYTE*)MapViewOfFile(prn.section, FILE_MAP_READ, 0, 0, TCR_SECTION_BYTES);
	if (!prn.view) prn_close_section();
	return prn.view != NULL;
}

static void prn_stop(HWND owner)
{
	if (prn.active) {
		TCR_CONFIG config;
		ZeroMemory(&config, sizeof(config));
		config.size = sizeof(config);
		config.generation = prn.generation;
		prn_send(prn.clock, owner, TCR_CONFIG_DATA, &config, sizeof(config));
		prn_log("route ended, generation", prn.generation);
	}
	prn.active = FALSE;
	prn.clock = NULL;
	prn.regionTick = 0;
	prn_close_section();
}

void prn_refresh(HWND owner, BOOL force)
{
	static ULONGLONG last;
	PRT_FEATURE_REF ref;
	TCR_CONFIG config;
	ULONGLONG now = GetTickCount64();
	BOOL wanted;
	if (!force && last && now - last < PRN_REFRESH_MS) return;
	if (!prt_owns_thread()) return;
	last = now;
	ZeroMemory(&ref, sizeof(ref));
	wanted = prt_capture_feature(TC_PLUGIN_RENDER_ID, &ref) == S_OK && g_hwndClock && IsWindow(g_hwndClock);
	if (wanted && prn.active && prn.clock == g_hwndClock && !memcmp(&ref, &prn.owner, sizeof(ref))) return;
	if (!wanted && !prn.active) return;
	prn_stop(owner);
	if (!wanted || prn.generation == ~(ULONGLONG)0 || !prn_open_section()) return;
	ZeroMemory(&config, sizeof(config));
	config.size = sizeof(config);
	config.enabled = 1;
	config.generation = ++prn.generation;
	wcscpy_s(config.section, _countof(config.section), prn.name);
	if (!prn_send(g_hwndClock, owner, TCR_CONFIG_DATA, &config, sizeof(config))) {
		prn_close_section();
		return;
	}
	prn.owner = ref;
	prn.clock = g_hwndClock;
	prn.done = 0;
	prn.lastResult = S_OK;
	prn.active = TRUE;
	prn_log("route started, generation", prn.generation);
}

/* The view is read-only: interlocked operations would write, so use acquire reads. */
static LONG prn_read_sequence(const TCR_HEADER* header)
{
	LONG sequence = *(volatile const LONG*)&header->sequence;
	MemoryBarrier();
	return sequence;
}

static BOOL prn_read_request(TC_RENDER_REQUEST* request)
{
	const TCR_HEADER* header = (const TCR_HEADER*)prn.view;
	int attempt;
	if (!header) return FALSE;
	for (attempt = 0; attempt < 3; ++attempt) {
		LONG sequence = prn_read_sequence(header);
		if (sequence & 1) continue;
		if (header->generation != prn.generation) return FALSE;
		CopyMemory(request, prn.view + TCR_HEADER_BYTES, sizeof(*request));
		MemoryBarrier();
		if (prn_read_sequence(header) == sequence) {
			return request->size == sizeof(*request) && request->revision == header->revision &&
				request->width && request->height && request->width <= TC_RENDER_MAX_WIDTH &&
				request->height <= TC_RENDER_MAX_HEIGHT && request->text[TC_RENDER_TEXT_CCH - 1] == 0;
		}
	}
	return FALSE;
}

static HRESULT prn_call(const void* table, void* user, void* context)
{
	const TC_RENDER_PROVIDER* provider = (const TC_RENDER_PROVIDER*)table;
	PRN_CALL* call = (PRN_CALL*)context;
	if (!provider || provider->size != sizeof(*provider) || !provider->render) return E_INVALIDARG;
	return provider->render(user, call->request, call->output);
}

static BOOL prn_check(const TC_RENDER_REQUEST* request, const TC_RENDER_OUTPUT* output)
{
	SIZE_T count = (SIZE_T)request->width * request->height, i;
	if (output->size != sizeof(*output) || output->preferredWidth > TC_RENDER_MAX_WIDTH ||
		output->preferredHeight > TC_RENDER_MAX_HEIGHT || !output->preferredWidth != !output->preferredHeight ||
		output->regionCount > TC_RENDER_MAX_REGIONS) return FALSE;
	for (i = 0; i < output->regionCount; ++i) {
		const TC_RENDER_REGION* region = &output->regions[i];
		if (region->zone < 1 || region->zone > TC_RENDER_MAX_ZONE || region->bounds.left < 0 || region->bounds.top < 0 ||
			region->bounds.left >= region->bounds.right || region->bounds.top >= region->bounds.bottom ||
			region->bounds.right > (LONG)request->width || region->bounds.bottom > (LONG)request->height) return FALSE;
	}
	for (i = 0; i < count; ++i) {
		UINT pixel = output->pixels[i], alpha = pixel >> 24;
		if (((pixel >> 16) & 0xFF) > alpha || ((pixel >> 8) & 0xFF) > alpha || (pixel & 0xFF) > alpha) return FALSE;
	}
	return TRUE;
}

static void prn_render(HWND owner)
{
	static TC_RENDER_REQUEST request;
	TC_RENDER_OUTPUT output;
	PRN_CALL call;
	SIZE_T bytes;
	BOOL invoked;
	HRESULT result;
	if (!prn_read_request(&request) || request.revision <= prn.done) return;
	prn.done = request.revision;
	bytes = sizeof(TCR_FRAME) + (SIZE_T)request.width * request.height * 4;
	if (prn.replyBytes < bytes) {
		TCR_FRAME* grown = prn.reply ? (TCR_FRAME*)HeapReAlloc(GetProcessHeap(), 0, prn.reply, bytes) :
			(TCR_FRAME*)HeapAlloc(GetProcessHeap(), 0, bytes);
		if (!grown) return;
		prn.reply = grown;
		prn.replyBytes = bytes;
	}
	ZeroMemory(prn.reply, bytes);
	ZeroMemory(&output, sizeof(output));
	output.size = sizeof(output);
	output.pixels = (UINT*)(prn.reply + 1);
	call.request = &request;
	call.output = &output;
	result = prt_run_feature(&prn.owner, prn_call, &call, &invoked);
	if (result == S_OK && !prn_check(&request, &output)) result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
	if (result != prn.lastResult) prn_log(result == S_OK ? "render recovered, revision" : "render failed, revision", request.revision);
	prn.lastResult = result;
	if (result != S_OK) return;
	output.pixels = NULL;
	prn.reply->generation = prn.generation;
	prn.reply->revision = request.revision;
	prn.reply->width = request.width;
	prn.reply->height = request.height;
	prn.reply->preferredWidth = output.preferredWidth;
	prn.reply->preferredHeight = output.preferredHeight;
	prn.reply->regionCount = output.regionCount;
	CopyMemory(prn.reply->regions, output.regions, sizeof(output.regions));
	if (!prn_send(prn.clock, owner, TCR_FRAME_DATA, prn.reply, (DWORD)bytes)) return;
	prn.regionTick = GetTickCount64();
	prn.regionWidth = request.width;
	prn.regionHeight = request.height;
	prn.regionCount = output.regionCount;
	CopyMemory(prn.regions, output.regions, sizeof(prn.regions));
}

BOOL prn_hit_zone(POINT point, const RECT* client, int* zone)
{
	LONG width = client->right - client->left, height = client->bottom - client->top;
	POINT image;
	UINT i;
	/* A frame without regions leaves the built-in zone split in place. */
	if (!prn.active || !prn.regionTick || !prn.regionCount || GetTickCount64() - prn.regionTick > TCR_FRAME_AGE_MS ||
		width <= 0 || height <= 0) return FALSE;
	image.x = MulDiv(point.x - client->left, (int)prn.regionWidth, width);
	image.y = MulDiv(point.y - client->top, (int)prn.regionHeight, height);
	*zone = 0;
	for (i = 0; i < prn.regionCount; ++i) {
		if (PtInRect(&prn.regions[i].bounds, image)) {
			*zone = (int)prn.regions[i].zone;
			break;
		}
	}
	return TRUE;
}

BOOL prn_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam)
{
	MSG queued;
	(void)lParam;
	if (message != TCR_READY) return FALSE;
	/* Render only the newest request: a slow provider must not build a backlog that
	   keeps other messages (WM_CLOSE, menus) waiting behind queued requests. */
	while (PeekMessageW(&queued, owner, TCR_READY, TCR_READY, PM_REMOVE)) wParam = queued.wParam;
	if (prn.active && (ULONGLONG)wParam == (prn.generation & (ULONG_PTR)-1)) prn_render(owner);
	return TRUE;
}

void prn_shutdown(HWND owner)
{
	prn_stop(owner);
	if (prn.reply) HeapFree(GetProcessHeap(), 0, prn.reply);
	prn.reply = NULL;
	prn.replyBytes = 0;
}
