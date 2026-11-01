#include "tclock.h"
#include "plugin_format_bridge.h"
#include "plugin_format_async.h"
#include "../common/format_bridge.h"
#include <stdio.h>
#include <string.h>

typedef struct PFB_STATE {
	BOOL active;
	PFRM_BINDING binding;
	ULONGLONG generation;
	HWND clock;
	HANDLE section;
	const BYTE* view;
	WCHAR name[TCF_BRIDGE_NAME_CCH];
	/* Per consumer slot: a newer source is waiting / last revision formatted. */
	BOOL pending[TCF_BRIDGE_SLOTS];
	ULONGLONG done[TCF_BRIDGE_SLOTS];
	PFA_TASK* task;
	ULONGLONG taskGeneration;
	ULONGLONG taskRevision;
	UINT taskConsumer;
	TC_FORMAT_SOURCE* scratch;
	TCF_BRIDGE_FRAME* reply;
} PFB_STATE;
static PFB_STATE pfb;

static BOOL pfb_send(HWND clock, HWND owner, ULONG_PTR kind, const void* data, DWORD bytes)
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

static void pfb_close_section(void)
{
	if (pfb.view) UnmapViewOfFile(pfb.view);
	if (pfb.section) CloseHandle(pfb.section);
	pfb.view = NULL;
	pfb.section = NULL;
	pfb.name[0] = 0;
}

/* The name carries the exe PID so the clock can reject a section it was not offered. */
static BOOL pfb_open_section(void)
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	swprintf_s(pfb.name, _countof(pfb.name), L"Local\\TClockFormatBridge.%lu.%016llx",
		GetCurrentProcessId(), (unsigned long long)counter.QuadPart ^ GetTickCount64());
	pfb.section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
		TCF_BRIDGE_SECTION_BYTES, pfb.name);
	if (pfb.section && GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(pfb.section);
		pfb.section = NULL;
	}
	if (pfb.section) pfb.view = (const BYTE*)MapViewOfFile(pfb.section, FILE_MAP_READ, 0, 0, TCF_BRIDGE_SECTION_BYTES);
	if (!pfb.view) pfb_close_section();
	return pfb.view != NULL;
}

static void pfb_stop(HWND owner)
{
	if (pfb.active) {
		TCF_BRIDGE_CONFIG config;
		ZeroMemory(&config, sizeof(config));
		config.size = sizeof(config);
		config.generation = pfb.generation;
		pfb_send(pfb.clock, owner, TCF_BRIDGE_CONFIG_DATA, &config, sizeof(config));
	}
	/* Cancel only suppresses publication; the poll timer still settles the task. */
	if (pfb.task) pfa_cancel(pfb.task);
	pfb.active = FALSE;
	ZeroMemory(pfb.pending, sizeof(pfb.pending));
	pfb.clock = NULL;
	pfb_close_section();
}

void pfb_refresh(HWND owner, BOOL force)
{
	static ULONGLONG last;
	PFRM_BINDING binding;
	TCF_BRIDGE_CONFIG config;
	BOOL wanted;
	ULONGLONG now = GetTickCount64();
	if (!force && last && now - last < PFB_REFRESH_MS) return;
	last = now;
	ZeroMemory(&binding, sizeof(binding));
	wanted = prt_owns_thread() && pfa_prepare(&binding) == S_OK &&
		(binding.demand.consumers & TCF_BRIDGE_CONSUMERS) &&
		g_hwndClock && IsWindow(g_hwndClock);
	if (wanted && pfb.active && pfb.clock == g_hwndClock && !memcmp(&binding, &pfb.binding, sizeof(binding))) return;
	if (!wanted && !pfb.active) return;
	pfb_stop(owner);
	if (!wanted || pfb.generation == ~(ULONGLONG)0 || !pfb_open_section()) return;
	ZeroMemory(&config, sizeof(config));
	config.size = sizeof(config);
	config.enabled = 1;
	config.generation = ++pfb.generation;
	config.demand = binding.demand;
	wcscpy_s(config.section, _countof(config.section), pfb.name);
	if (!pfb_send(g_hwndClock, owner, TCF_BRIDGE_CONFIG_DATA, &config, sizeof(config))) {
		pfb_close_section();
		return;
	}
	pfb.binding = binding;
	pfb.clock = g_hwndClock;
	ZeroMemory(pfb.done, sizeof(pfb.done));
	pfb.active = TRUE;
}

/* The view is read-only: interlocked operations would write, so use acquire reads. */
static LONG pfb_read_sequence(const TCF_BRIDGE_HEADER* header)
{
	LONG sequence = *(volatile const LONG*)&header->sequence;
	MemoryBarrier();
	return sequence;
}

/* Copies the latest body of one slot only while one even sequence brackets the copy. */
static BOOL pfb_read_source(UINT consumer, UINT* bytes, ULONGLONG* revision)
{
	const BYTE* slot = pfb.view ? pfb.view + (consumer - 1) * TCF_BRIDGE_SLOT_BYTES : NULL;
	const TCF_BRIDGE_HEADER* header = (const TCF_BRIDGE_HEADER*)slot;
	int attempt;
	if (!pfb.scratch) pfb.scratch = (TC_FORMAT_SOURCE*)HeapAlloc(GetProcessHeap(), 0, TCF_SOURCE_MAX_BYTES);
	if (!pfb.scratch || !header) return FALSE;
	for (attempt = 0; attempt < 3; ++attempt) {
		LONG sequence = pfb_read_sequence(header);
		UINT size;
		if (sequence & 1) continue;
		size = header->bytes;
		*revision = header->revision;
		if (header->generation != pfb.generation || size < sizeof(UINT) * 2 || size > TCF_SOURCE_MAX_BYTES) return FALSE;
		CopyMemory(pfb.scratch, slot + TCF_BRIDGE_HEADER_BYTES, size);
		MemoryBarrier();
		if (pfb_read_sequence(header) == sequence) {
			*bytes = size;
			return pfb.scratch->revision == *revision && pfb.scratch->consumer == consumer;
		}
	}
	return FALSE;
}

/* One task in flight; pending slots are served in consumer order, main first. */
static void pfb_pump(HWND owner)
{
	UINT bytes, consumer;
	ULONGLONG revision;
	PFA_TASK* task;
	if (pfb.task || !pfb.active) return;
	for (consumer = 1; consumer <= TCF_BRIDGE_SLOTS; ++consumer) {
		if (!pfb.pending[consumer - 1]) continue;
		pfb.pending[consumer - 1] = FALSE;
		if (!pfb_read_source(consumer, &bytes, &revision) || revision <= pfb.done[consumer - 1]) continue;
		if (pfa_stage(pfb.scratch, bytes, TCF_FRAME_CCH, &task) != S_OK) continue;
		if (pfa_launch(&pfb.binding, task) != S_OK) {
			pfa_dispose(task);
			continue;
		}
		pfb.task = task;
		pfb.taskGeneration = pfb.generation;
		pfb.taskRevision = revision;
		pfb.taskConsumer = consumer;
		SetTimer(owner, PFB_POLL_TIMER, 15, NULL);
		return;
	}
}

BOOL pfb_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message != TCF_BRIDGE_READY) return FALSE;
	if (pfb.active && (ULONGLONG)wParam == (pfb.generation & (ULONG_PTR)-1) &&
		lParam >= 1 && lParam <= (LPARAM)TCF_BRIDGE_SLOTS) {
		pfb.pending[lParam - 1] = TRUE;
		pfb_pump(owner);
	}
	return TRUE;
}

void pfb_on_timer(HWND owner)
{
	BOOL complete = FALSE, invoked;
	HRESULT result;
	if (!pfb.task) {
		KillTimer(owner, PFB_POLL_TIMER);
		return;
	}
	if (pfa_poll(pfb.task, &complete) != S_OK || !complete) return;
	if (!pfb.reply) pfb.reply = (TCF_BRIDGE_FRAME*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pfb.reply));
	result = pfa_finish(pfb.task, pfb.reply ? &pfb.reply->frame : NULL, &invoked);
	if (pfa_dispose(pfb.task) != S_OK) return; /* Settlement failed; retry on the next tick. */
	pfb.task = NULL;
	KillTimer(owner, PFB_POLL_TIMER);
	if (result == S_OK && pfb.reply && pfb.active && pfb.taskGeneration == pfb.generation) {
		pfb.reply->generation = pfb.generation;
		pfb.reply->consumer = pfb.taskConsumer;
		pfb_send(pfb.clock, owner, TCF_BRIDGE_FRAME_DATA, pfb.reply, sizeof(*pfb.reply));
	}
	if (pfb.taskGeneration == pfb.generation) pfb.done[pfb.taskConsumer - 1] = pfb.taskRevision;
	pfb_pump(owner);
}

void pfb_shutdown(HWND owner)
{
	pfb_stop(owner);
	KillTimer(owner, PFB_POLL_TIMER);
	if (pfb.task && pfa_dispose(pfb.task) == S_OK) pfb.task = NULL;
	/* A still-running task keeps its own source copy; its scratch/reply are separate. */
	if (pfb.scratch) HeapFree(GetProcessHeap(), 0, pfb.scratch);
	if (pfb.reply) HeapFree(GetProcessHeap(), 0, pfb.reply);
	pfb.scratch = NULL;
	pfb.reply = NULL;
}
