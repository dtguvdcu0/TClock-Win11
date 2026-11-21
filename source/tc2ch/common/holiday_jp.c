#include "holiday_jp.h"

#include <math.h>
#include <wchar.h>

/* Port of TCalendar providers/jp-public-holiday.js.
   Closure rows are evaluated separately and never mask public holidays,
   so 1/2 after a Sunday New Year's Day is reported as a substitute holiday. */

#define HJ_LIMITED 0
#define HJ_FIXED 1
#define HJ_MONDAY 2
#define HJ_SPRING 3
#define HJ_AUTUMN 4
#define HJ_MAX_ENTRIES 64

typedef struct HJ_RULE {
	WORD first, last, year;
	BYTE kind, month, day, closure;
	const WCHAR* name;
} HJ_RULE;

#include "holiday_jp_rules.h"

typedef struct HJ_YEAR {
	int first, next, count;
	int serial[HJ_MAX_ENTRIES];
	const WCHAR* name[HJ_MAX_ENTRIES];
} HJ_YEAR;

/* Days since 1970-01-01 in the proleptic Gregorian calendar. */
static int hj_get_serial(int year, int month, int day)
{
	int era, yoe, doy, doe;
	year -= month <= 2;
	era = (year >= 0 ? year : year - 399) / 400;
	yoe = year - era * 400;
	doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

/* 0 = Sunday. */
static int hj_get_weekday(int serial)
{
	return ((serial % 7) + 11) % 7;
}

static int hj_get_equinox(int year, BOOL autumn)
{
	double base;
	if (year > 1850 && year < 1900) base = autumn ? 22.2588 : 20.8431;
	else if (year > 1899 && year < 1980) base = autumn ? 23.2588 : 20.8357;
	else if (year > 1979 && year < 2100) base = autumn ? 23.2488 : 20.8431;
	else if (year > 2099 && year < 2151) base = autumn ? 24.2488 : 21.851;
	else return autumn ? 23 : 20;
	return (int)floor(base + 0.242194 * (year - 1980)) - (int)floor((year - 1980) / 4.0);
}

static void hj_build_year(int year, BYTE closure, HJ_YEAR* out)
{
	const WCHAR* seen[HJ_MAX_ENTRIES];
	int seenCount = 0, i, j, month, day, serial;
	out->first = hj_get_serial(year, 1, 1);
	out->next = hj_get_serial(year + 1, 1, 1);
	out->count = 0;
	for (i = 0; i < (int)ARRAYSIZE(hj_rules); i++) {
		const HJ_RULE* rule = &hj_rules[i];
		if (rule->closure != closure || year < rule->first || year > rule->last) continue;
		for (j = 0; j < seenCount && wcscmp(seen[j], rule->name) != 0; j++) {}
		if (j < seenCount) continue;
		month = rule->month;
		day = rule->day;
		if (rule->kind == HJ_LIMITED) {
			if (rule->year != year) continue;
		}
		else if (rule->kind == HJ_MONDAY) {
			day = 1 + (8 - hj_get_weekday(hj_get_serial(year, month, 1))) % 7 + 7 * (rule->day - 1);
		}
		else if (rule->kind == HJ_SPRING) {
			month = 3;
			day = hj_get_equinox(year, FALSE);
		}
		else if (rule->kind == HJ_AUTUMN) {
			month = 9;
			day = hj_get_equinox(year, TRUE);
		}
		serial = hj_get_serial(year, month, day);
		for (j = 0; j < out->count && out->serial[j] != serial; j++) {}
		if (j == out->count) {
			if (out->count >= HJ_MAX_ENTRIES) continue;
			out->serial[out->count++] = serial;
		}
		out->name[j] = rule->name;
		if (seenCount < HJ_MAX_ENTRIES) seen[seenCount++] = rule->name;
	}
}

static const WCHAR* hj_find_base(const HJ_YEAR* table, int serial)
{
	int i;
	if (serial < table->first || serial >= table->next) return NULL;
	for (i = 0; i < table->count; i++) {
		if (table->serial[i] == serial) return table->name[i];
	}
	return NULL;
}

static BOOL hj_is_citizen(const HJ_YEAR* table, int year, int serial)
{
	if (year < HJ_CITIZEN_FIRST || hj_get_weekday(serial) == 0) return FALSE;
	if (hj_find_base(table, serial)) return FALSE;
	return hj_find_base(table, serial - 1) && hj_find_base(table, serial + 1);
}

/* A substitute holiday is the first non-holiday day after a Sunday holiday. */
static BOOL hj_is_substitute(const HJ_YEAR* table, int year, int serial)
{
	int scan;
	if (year < HJ_SUBSTITUTE_FIRST || hj_find_base(table, serial)) return FALSE;
	for (scan = serial - 1; hj_find_base(table, scan) || hj_is_citizen(table, year, scan); scan--) {
		if (hj_find_base(table, scan) && hj_get_weekday(scan) == 0) return TRUE;
	}
	return FALSE;
}

UINT TcHolidayFind(int year, int month, int day, const WCHAR** name)
{
	HJ_YEAR table;
	const WCHAR* found;
	int serial;
	if (name) *name = NULL;
	if (year < HJ_RANGE_FIRST || year > HJ_RANGE_LAST || month < 1 || month > 12 || day < 1 || day > 31) {
		return TC_HOLIDAY_NONE;
	}
	serial = hj_get_serial(year, month, day);
	hj_build_year(year, 0, &table);
	found = hj_find_base(&table, serial);
	if (!found && hj_is_citizen(&table, year, serial)) found = L"国民の休日";
	if (!found && hj_is_substitute(&table, year, serial)) found = L"振替休日";
	if (found) {
		if (name) *name = found;
		return TC_HOLIDAY_PUBLIC;
	}
	hj_build_year(year, 1, &table);
	if (!hj_find_base(&table, serial)) return TC_HOLIDAY_NONE;
	if (name) *name = L"年末年始";
	return TC_HOLIDAY_CLOSURE;
}
