#include "tclock.h"
#include "plugin_chime.h"
#include "plugin_runtime.h"
#include <stdio.h>
#include <string.h>

extern BOOL b_DebugLog;

/* Exe side of common/chime_bridge.h. The clock decides which accepted event started,
   finished or was dropped; this side turns those decisions into exactly one provider
   report per accepted event and never accepts an id again once it started. */
#define PCH_TRACKED 64u
#define PCH_DONE 128u

/* REPORT: the outcome is known but the provider was busy; retried, never re-executed. */
enum { PCH_FREE, PCH_NEW, PCH_PLANNED, PCH_STARTED, PCH_REPORT };

typedef struct PCH_ENTRY {
	UINT state;
	UINT tag;
	UINT outcome;
	ULONGLONG second;
	TC_CHIME_EVENT event;
	PRT_FEATURE_REF owner;
} PCH_ENTRY;

typedef struct PCH_STATE {
	BOOL active;
	PRT_FEATURE_REF owner;
	ULONGLONG generation;
	UINT nextTag;
	HWND clock;
	PCH_ENTRY entries[PCH_TRACKED];
	/* Started or finished ids (registration, id); cancelled ids may be planned again. */
	ULONGLONG done[PCH_DONE][2];
	UINT doneNext;
} PCH_STATE;
static PCH_STATE pch;

typedef struct PCH_CALL {
	const TC_CHIME_REQUEST* request;
	TC_CHIME_PLAN* plan;
	ULONGLONG id;
	UINT outcome;
} PCH_CALL;

static void pch_log(const char* event, ULONGLONG value, UINT detail)
{
	char line[160];
	if (!b_DebugLog) return;
	sprintf_s(line, sizeof(line), "[plugin_chime] %s %llu %u", event, value, detail);
	WriteDebug_New2(line);
}

static ULONGLONG pch_second(const SYSTEMTIME* time)
{
	SYSTEMTIME local = *time;
	FILETIME file;
	ULARGE_INTEGER value;
	local.wMilliseconds = 0;
	if (!SystemTimeToFileTime(&local, &file)) return 0;
	value.LowPart = file.dwLowDateTime;
	value.HighPart = file.dwHighDateTime;
	return value.QuadPart / 10000000u;
}

static BOOL pch_time(ULONGLONG second, SYSTEMTIME* time)
{
	ULARGE_INTEGER value;
	FILETIME file;
	value.QuadPart = second * 10000000u;
	file.dwLowDateTime = value.LowPart;
	file.dwHighDateTime = value.HighPart;
	return FileTimeToSystemTime(&file, time);
}

static BOOL pch_table(const void* table)
{
	const TC_CHIME_PROVIDER* provider = (const TC_CHIME_PROVIDER*)table;
	return provider && provider->size == sizeof(*provider) && provider->plan && provider->report;
}

static HRESULT pch_call_plan(const void* table, void* user, void* context)
{
	PCH_CALL* call = (PCH_CALL*)context;
	if (!pch_table(table)) return E_INVALIDARG;
	return ((const TC_CHIME_PROVIDER*)table)->plan(user, call->request, call->plan);
}

static HRESULT pch_call_report(const void* table, void* user, void* context)
{
	PCH_CALL* call = (PCH_CALL*)context;
	if (!pch_table(table)) return E_INVALIDARG;
	((const TC_CHIME_PROVIDER*)table)->report(user, call->id, call->outcome);
	return S_OK;
}

static BOOL pch_is_done(const PRT_FEATURE_REF* owner, ULONGLONG id)
{
	UINT i;
	for (i = 0; i < PCH_DONE; ++i)
		if (pch.done[i][0] == owner->registration && pch.done[i][1] == id) return TRUE;
	return FALSE;
}

/* Reports to the registration that planned the event; a stopped or replaced
   registration can no longer be called, so its report is dropped. */
static void pch_settle(PCH_ENTRY* entry, UINT outcome)
{
	PCH_CALL call;
	BOOL invoked;
	HRESULT result;
	if (entry->state != PCH_REPORT && (outcome != TC_CHIME_CANCELLED || entry->state == PCH_STARTED)) {
		pch.done[pch.doneNext][0] = entry->owner.registration;
		pch.done[pch.doneNext][1] = entry->event.id;
		pch.doneNext = (pch.doneNext + 1) % PCH_DONE;
	}
	ZeroMemory(&call, sizeof(call));
	call.id = entry->event.id;
	call.outcome = outcome;
	result = prt_run_feature(&entry->owner, pch_call_report, &call, &invoked);
	if (result == HRESULT_FROM_WIN32(ERROR_BUSY)) {
		entry->state = PCH_REPORT;
		entry->outcome = outcome;
		return;
	}
	pch_log(result == S_OK ? "reported id" : "report dropped for id", entry->event.id, outcome);
	entry->state = PCH_FREE;
}

static void pch_retry(void)
{
	UINT i;
	for (i = 0; i < PCH_TRACKED; ++i)
		if (pch.entries[i].state == PCH_REPORT) pch_settle(&pch.entries[i], pch.entries[i].outcome);
}

static PCH_ENTRY* pch_find(UINT tag)
{
	UINT i;
	for (i = 0; i < PCH_TRACKED; ++i)
		if (pch.entries[i].state != PCH_FREE && pch.entries[i].tag == tag) return &pch.entries[i];
	return NULL;
}

static void pch_outcome(WPARAM generation, LPARAM value)
{
	PCH_ENTRY* entry;
	UINT code = HIWORD(value);
	if (generation != (WPARAM)pch.generation || !(entry = pch_find(LOWORD(value))) ||
		entry->state == PCH_NEW || entry->state == PCH_REPORT) return;
	if (code == TCC_STARTED) {
		entry->state = PCH_STARTED;
		return;
	}
	if (code >= TC_CHIME_COMPLETED && code <= TC_CHIME_CANCELLED) pch_settle(entry, code);
}

/* Outcomes the clock posted while handling a synchronous plan are already queued. */
static void pch_drain(HWND owner)
{
	MSG message;
	while (PeekMessageW(&message, owner, TCC_OUTCOME, TCC_OUTCOME, PM_REMOVE))
		pch_outcome(message.wParam, message.lParam);
}

enum { PCH_REJECTED, PCH_ACCEPTED, PCH_UNKNOWN };

/* A timed-out send can still be processed by the clock later, so it is not a rejection. */
static int pch_send(HWND owner, const TCC_PLAN* plan)
{
	COPYDATASTRUCT copy;
	DWORD_PTR result = 0;
	int sent;
	copy.dwData = TCC_PLAN_DATA;
	copy.cbData = sizeof(*plan);
	copy.lpData = (void*)plan;
	if (!pch.clock || !IsWindow(pch.clock)) sent = PCH_REJECTED;
	else if (!SendMessageTimeoutW(pch.clock, WM_COPYDATA, (WPARAM)owner, (LPARAM)&copy,
		SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result)) sent = PCH_UNKNOWN;
	else sent = result ? PCH_ACCEPTED : PCH_REJECTED;
	pch_drain(owner);
	return sent;
}

static void pch_stop(HWND owner)
{
	static TCC_PLAN plan;
	SYSTEMTIME now;
	ULONGLONG second;
	UINT i;
	if (!pch.active) return;
	pch_retry();
	ZeroMemory(&plan, sizeof(plan));
	plan.size = sizeof(plan);
	plan.generation = pch.generation;
	pch_send(owner, &plan);
	/* Entries left now were not ended by the clock: it is gone or did not answer. */
	GetLocalTime(&now);
	second = pch_second(&now);
	for (i = 0; i < PCH_TRACKED; ++i) {
		PCH_ENTRY* entry = &pch.entries[i];
		if (entry->state == PCH_PLANNED || entry->state == PCH_STARTED)
			pch_settle(entry, entry->state == PCH_STARTED || entry->second <= second ? TC_CHIME_UNKNOWN : TC_CHIME_CANCELLED);
		/* Still busy at the end of the route: the report is dropped, the event is not. */
		if (entry->state == PCH_REPORT) pch_log("report dropped for id", entry->event.id, entry->outcome);
		entry->state = PCH_FREE;
	}
	pch_log("route ended, generation", pch.generation, 0);
	pch.active = FALSE;
	pch.clock = NULL;
}

static BOOL pch_check(const TC_CHIME_PLAN* plan, ULONGLONG from, UINT window)
{
	UINT i, j;
	if (plan->size != sizeof(*plan) || plan->count > TC_CHIME_MAX_EVENTS) return FALSE;
	for (i = 0; i < plan->count; ++i) {
		const TC_CHIME_EVENT* event = &plan->events[i];
		ULONGLONG second = pch_second(&event->time);
		if (!event->id || !event->effects || (event->effects & ~TC_CHIME_EFFECTS) ||
			((event->effects & TC_CHIME_PRIMARY) ? (!event->strikes || event->strikes > TC_CHIME_MAX_STRIKES) : event->strikes) ||
			second < from || second >= from + window) return FALSE;
		for (j = 0; j < i; ++j) if (plan->events[j].id == event->id) return FALSE;
	}
	return TRUE;
}

static PCH_ENTRY* pch_track(const PRT_FEATURE_REF* owner, const TC_CHIME_EVENT* event)
{
	PCH_ENTRY* entry = NULL;
	UINT i;
	for (i = 0; i < PCH_TRACKED && !entry; ++i) {
		PCH_ENTRY* item = &pch.entries[i];
		if (item->state != PCH_FREE && item->event.id == event->id && item->owner.registration == owner->registration) entry = item;
	}
	if (entry) return entry->state == PCH_STARTED || entry->state == PCH_REPORT ? NULL : entry;
	if (pch.nextTag >= 0xFFFF) return NULL;
	for (i = 0; i < PCH_TRACKED && !entry; ++i)
		if (pch.entries[i].state == PCH_FREE) entry = &pch.entries[i];
	if (!entry) return NULL;
	entry->state = PCH_NEW;
	entry->tag = ++pch.nextTag;
	entry->owner = *owner;
	pch_log("accepted id", event->id, entry->tag);
	return entry;
}

static void pch_plan(HWND owner, const PRT_FEATURE_REF* ref)
{
	static TC_CHIME_PLAN plan;
	static TCC_PLAN out;
	TC_CHIME_REQUEST request;
	PCH_CALL call;
	ULONGLONG from;
	BOOL invoked;
	int accepted;
	HRESULT result;
	UINT i;
	ZeroMemory(&request, sizeof(request));
	request.size = sizeof(request);
	request.windowSeconds = PCH_WINDOW_SECONDS;
	GetLocalTime(&request.from);
	request.from.wMilliseconds = 0;
	from = pch_second(&request.from);
	ZeroMemory(&plan, sizeof(plan));
	plan.size = sizeof(plan);
	ZeroMemory(&call, sizeof(call));
	call.request = &request;
	call.plan = &plan;
	result = prt_run_feature(ref, pch_call_plan, &call, &invoked);
	/* A failed or invalid plan accepts nothing; the current plan stays until it expires. */
	if (result != S_OK || !pch_check(&plan, from, PCH_WINDOW_SECONDS)) {
		pch_log("plan rejected, result", (ULONGLONG)(UINT)result, plan.count);
		return;
	}
	if (!pch.active) {
		++pch.generation;
		pch.nextTag = 0;
		pch.owner = *ref;
		pch.clock = g_hwndClock;
	}
	ZeroMemory(&out, sizeof(out));
	out.size = sizeof(out);
	out.enabled = 1;
	out.generation = pch.generation;
	if (!pch_time(from + PCH_WINDOW_SECONDS, &out.until)) return;
	for (i = 0; i < plan.count; ++i) {
		PCH_ENTRY* entry;
		if (pch_is_done(ref, plan.events[i].id) || !(entry = pch_track(ref, &plan.events[i]))) continue;
		entry->event = plan.events[i];
		entry->second = pch_second(&plan.events[i].time);
		out.events[out.count].tag = entry->tag;
		out.events[out.count].effects = entry->event.effects;
		out.events[out.count].strikes = entry->event.strikes;
		out.events[out.count].time = entry->event.time;
		++out.count;
	}
	accepted = pch_send(owner, &out);
	/* Unknown delivery keeps the events and the route: ending it later settles them once. */
	for (i = 0; i < PCH_TRACKED; ++i)
		if (pch.entries[i].state == PCH_NEW) pch.entries[i].state = accepted != PCH_REJECTED ? PCH_PLANNED : PCH_FREE;
	if (accepted == PCH_UNKNOWN) pch_log("plan delivery unknown, generation", pch.generation, out.count);
	if (accepted != PCH_REJECTED && !pch.active) pch_log("route started, generation", pch.generation, out.count);
	pch.active = pch.active || accepted != PCH_REJECTED;
}

void pch_refresh(HWND owner, BOOL force)
{
	static ULONGLONG last;
	static PRT_FEATURE_REF tried;
	static HWND triedClock;
	static BOOL triedWanted;
	PRT_FEATURE_REF ref;
	ULONGLONG now = GetTickCount64();
	BOOL wanted;
	if (!prt_owns_thread()) return;
	pch_retry();
	/* Without an active route nothing can be ended early; keep the PCH_REFRESH_MS cadence. */
	if (!force && last && now - last < PCH_REFRESH_MS && !pch.active) return;
	ZeroMemory(&ref, sizeof(ref));
	wanted = prt_capture_feature(TC_PLUGIN_CHIME_ID, &ref) == S_OK && g_hwndClock && IsWindow(g_hwndClock);
	/* The owner is checked every tick, so an owner disabled outside the Plugins page (for example
	   by an INI edit) ends its route at once; an unchanged owner is replanned every PCH_REFRESH_MS. */
	if (!force && last && now - last < PCH_REFRESH_MS && wanted == triedWanted && triedClock == g_hwndClock &&
		(!wanted || !memcmp(&ref, &tried, sizeof(ref)))) return;
	last = now;
	tried = ref;
	triedClock = g_hwndClock;
	triedWanted = wanted;
	/* Owner, registration, activation or clock changes start a new generation. A generation
	   also ends before its 16-bit tag space runs out, so later events are still accepted. */
	if (pch.active && (!wanted || pch.clock != g_hwndClock || memcmp(&ref, &pch.owner, sizeof(ref)) ||
		pch.nextTag >= 0xFFFFu - TC_CHIME_MAX_EVENTS)) pch_stop(owner);
	if (wanted) pch_plan(owner, &ref);
}

BOOL pch_handle_message(HWND owner, UINT message, WPARAM wParam, LPARAM lParam)
{
	(void)owner;
	if (message != TCC_OUTCOME) return FALSE;
	pch_outcome(wParam, lParam);
	return TRUE;
}

void pch_shutdown(HWND owner)
{
	pch_stop(owner);
}
