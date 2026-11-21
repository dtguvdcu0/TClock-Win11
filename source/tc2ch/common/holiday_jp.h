#pragma once

#include <windows.h>

/* Japanese public holiday lookup ported from the TCalendar jp-public-holiday provider.
   The rule table is built in; no plugin or external file is required. */

#define TC_HOLIDAY_NONE 0u
#define TC_HOLIDAY_PUBLIC 1u   /* statutory holiday, substitute holiday, or citizen's holiday */
#define TC_HOLIDAY_CLOSURE 2u  /* year-end/new-year closure day (12/29-12/31, 1/2, 1/3) */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns TC_HOLIDAY_* for the given local date. When name is not NULL it receives a
   static holiday name (or NULL for TC_HOLIDAY_NONE). Thread-safe; keeps no state. */
UINT TcHolidayFind(int year, int month, int day, const WCHAR** name);

#ifdef __cplusplus
}
#endif
