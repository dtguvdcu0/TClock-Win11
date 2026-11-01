#include "tcdll.h"
#include "../common/chime_bridge.h"
#include <stdio.h>

extern HWND hwndTClockExeMain;
extern BOOL b_DebugLog;
extern int nBlink;
extern int BlinksOnChime;

/* Clock-thread side of common/chime_bridge.h: executes the selected provider's plan
   and suppresses the built-in chime decision while the plan is current. */
enum { CCR_FREE, CCR_PENDING, CCR_ACTIVE };

typedef struct CCR_EVENT {
	UINT tag, effects, strikes, state;
	ULONGLONG second;
} CCR_EVENT;

typedef struct CCR_STATE {
	BOOL enabled, owning;
	ULONGLONG generation, until, lastStrike;
	UINT remaining;
	CCR_EVENT* active;
	CCR_EVENT events[TC_CHIME_MAX_EVENTS];
	/* Tags that started or ended in this generation; the exe never reuses a tag. */
	BYTE used[65536 / 8];
} CCR_STATE;
static CCR_STATE ccr;

static void ccr_log(const char* event, UINT tag, int value)
{
	char line[128];
	if (!b_DebugLog) return;
	sprintf_s(line, sizeof(line), "[chime_route] %s, tag %u:", event, tag);
	writeDebugLog_Win10(line, value);
}

static ULONGLONG ccr_second(const SYSTEMTIME* time)
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

static BOOL ccr_used(UINT tag)
{
	return (ccr.used[tag >> 3] >> (tag & 7)) & 1;
}

/* Every accepted event ends exactly once through here. */
static void ccr_finish(CCR_EVENT* event, UINT outcome)
{
	ccr.used[event->tag >> 3] |= (BYTE)(1u << (event->tag & 7));
	PostMessageW(hwndTClockExeMain, TCC_OUTCOME, (WPARAM)ccr.generation, MAKELPARAM(event->tag, outcome));
	ccr_log("outcome", event->tag, (int)outcome);
	if (ccr.active == event) ccr.active = NULL;
	event->state = CCR_FREE;
}

/* Ends events that the next plan no longer contains (all of them when plan is NULL). */
static void ccr_drop(const TCC_PLAN* plan)
{
	UINT i, j;
	for (i = 0; i < TC_CHIME_MAX_EVENTS; ++i) {
		CCR_EVENT* event = &ccr.events[i];
		BOOL kept = FALSE;
		if (event->state == CCR_FREE) continue;
		if (plan) for (j = 0; j < plan->count && !kept; ++j) kept = plan->events[j].tag == event->tag;
		/* A started event cannot be withdrawn; only shutdown or owner change ends it early. */
		if (kept || (plan && event->state == CCR_ACTIVE)) continue;
		ccr_finish(event, event->state == CCR_ACTIVE ? TC_CHIME_UNKNOWN : TC_CHIME_CANCELLED);
	}
}

void ccr_reset(void)
{
	ULONGLONG generation = ccr.generation;
	if (ccr.enabled) {
		ccr_drop(NULL);
		ccr_log("disabled", 0, (int)generation);
	}
	ZeroMemory(&ccr, sizeof(ccr));
	ccr.generation = generation;
}

static BOOL ccr_check(const TCC_PLAN* plan)
{
	UINT i;
	if (plan->size != sizeof(*plan) || plan->count > TC_CHIME_MAX_EVENTS || !ccr_second(&plan->until)) return FALSE;
	for (i = 0; i < plan->count; ++i) {
		const TCC_EVENT* event = &plan->events[i];
		if (!event->tag || event->tag > 0xFFFF || !event->effects || (event->effects & ~TC_CHIME_EFFECTS) ||
			((event->effects & TC_CHIME_PRIMARY) ? (!event->strikes || event->strikes > TC_CHIME_MAX_STRIKES) : event->strikes) ||
			!ccr_second(&event->time)) return FALSE;
	}
	return TRUE;
}

static void ccr_accept(const TCC_PLAN* plan)
{
	UINT i, j;
	if (plan->generation != ccr.generation) {
		ccr_reset();
		ccr.generation = plan->generation;
	}
	else ccr_drop(plan);
	for (i = 0; i < plan->count; ++i) {
		const TCC_EVENT* from = &plan->events[i];
		CCR_EVENT* to = NULL;
		if (ccr_used(from->tag)) continue;
		for (j = 0; j < TC_CHIME_MAX_EVENTS && !to; ++j)
			if (ccr.events[j].state != CCR_FREE && ccr.events[j].tag == from->tag) to = &ccr.events[j];
		if (to && to->state == CCR_ACTIVE) continue;
		for (j = 0; j < TC_CHIME_MAX_EVENTS && !to; ++j)
			if (ccr.events[j].state == CCR_FREE) to = &ccr.events[j];
		if (!to) break;
		to->tag = from->tag;
		to->effects = from->effects;
		to->strikes = from->strikes;
		to->second = ccr_second(&from->time);
		to->state = CCR_PENDING;
	}
	ccr.until = ccr_second(&plan->until);
	if (!ccr.enabled) ccr_log("plan accepted, generation", 0, (int)plan->generation);
	ccr.enabled = TRUE;
}

/* Returns the WM_COPYDATA result: nonzero only when the clock took the plan. */
BOOL ccr_receive(HWND sender, const COPYDATASTRUCT* data, LRESULT* result)
{
	TCC_PLAN plan;
	if (!data || data->dwData != TCC_PLAN_DATA) return FALSE;
	*result = FALSE;
	if (!sender || sender != hwndTClockExeMain || !data->lpData || data->cbData != sizeof(plan)) return TRUE;
	CopyMemory(&plan, data->lpData, sizeof(plan));
	if (!plan.enabled) {
		if (plan.generation == ccr.generation) ccr_reset();
		*result = TRUE;
		return TRUE;
	}
	if (!ccr_check(&plan) || plan.generation < ccr.generation) return TRUE;
	ccr_accept(&plan);
	*result = TRUE;
	return TRUE;
}

static void ccr_strike(ULONGLONG now)
{
	if (now == ccr.lastStrike) return;
	ccr.lastStrike = now;
	PlayChime(FALSE);
	ccr_log("strike", ccr.active->tag, (int)(ccr.active->strikes - ccr.remaining + 1));
	if (--ccr.remaining == 0) ccr_finish(ccr.active, TC_CHIME_COMPLETED);
}

static void ccr_start(CCR_EVENT* event, ULONGLONG now, BOOL* redraw)
{
	event->state = CCR_ACTIVE;
	ccr.active = event;
	ccr.used[event->tag >> 3] |= (BYTE)(1u << (event->tag & 7));
	PostMessageW(hwndTClockExeMain, TCC_OUTCOME, (WPARAM)ccr.generation, MAKELPARAM(event->tag, TCC_STARTED));
	ccr_log("start, effects", event->tag, (int)event->effects);
	if (event->effects & TC_CHIME_BLINK) {
		nBlink = BlinksOnChime * 2;
		*redraw = TRUE;
	}
	if (event->effects & TC_CHIME_SECONDARY) PlayChime(TRUE);
	if (event->effects & TC_CHIME_PRIMARY) {
		ccr.remaining = event->strikes;
		ccr_strike(now);
	}
	else ccr_finish(event, TC_CHIME_COMPLETED);
}

/* Called every clock tick. TRUE: the plan owns the chime decision for this tick, so the
   caller skips the built-in decision. Strikes keep the built-in one-per-second pace. */
BOOL ccr_tick(const SYSTEMTIME* time, BOOL paused, BOOL* redraw)
{
	ULONGLONG now;
	UINT i;
	if (!ccr.enabled) return FALSE;
	now = ccr_second(time);
	if (ccr.active && paused) ccr_finish(ccr.active, TC_CHIME_UNKNOWN);
	/* Due events first: one that falls on a second still taken by strikes is skipped. */
	for (;;) {
		CCR_EVENT* due = NULL;
		for (i = 0; i < TC_CHIME_MAX_EVENTS; ++i) {
			CCR_EVENT* event = &ccr.events[i];
			if (event->state == CCR_PENDING && event->second <= now && (!due || event->second < due->second)) due = event;
		}
		if (!due) break;
		if (paused || ccr.active || now - due->second > TCC_START_GRACE) ccr_finish(due, TC_CHIME_SKIPPED);
		else ccr_start(due, now, redraw);
	}
	if (ccr.active) ccr_strike(now);
	if (now >= ccr.until && !ccr.active) {
		if (ccr.owning) ccr_log("plan expired, built-in resumes", 0, 0);
		ccr.owning = FALSE;
		return FALSE;
	}
	if (!ccr.owning) ccr_log("plan owns the chime decision", 0, 0);
	ccr.owning = TRUE;
	return TRUE;
}
