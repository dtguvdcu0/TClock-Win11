# Formatter source body /1

The format_source.h representation supplies the original format, time basis, cached telemetry and effective text inputs for formatter replacement. It has no process pointers, handles, paths or callbacks. Its internal producer is FormatCaptureSourceW in dll/format.c.

The source body and collector are implemented. Negotiated formatter callbacks, authenticated asynchronous transport, dependency scheduling and real drawing/measurement routing remain P3 work. Existing runtime/registry/actions/settings/extension /1 layouts and normal DLL exports remain unchanged.

## Storage and helpers

Version 1 uses fixed-width Win32 fields and eight-byte packing. Each TC_FORMAT_CALENDAR is 1304 bytes. C/C++ x86/x64 tests report identical layouts: a one-row body is 279048 bytes; 513 rows occupy 946696 bytes, under the 1048576-byte cap.

Only TcfSourceBytes(calendarCount) bytes are present. Do not copy sizeof(TC_FORMAT_SOURCE) from a shorter received body or read unused table entries. The host must own an eight-byte-aligned readable buffer before calling TcfValidateSource(source, bytes). The validator checks exact length/version, flags, nonzero revision, dates, array sample counts, finite doubles, terminated valid UTF-16, CUSTOM state and calendar consistency. It rejects truncated tables, duplicate time states, unknown flags and malformed surrogates. TcfFindCalendar takes the actual byte count and rejects truncated tables.

Validation covers body structure. Peer identity, request freshness, selected-owner revisions and deadlines belong to the separate envelope/adapter. TcfFreeSource releases process-heap owned collector storage; providers must not free borrowed callback inputs. No helper retains the body.

## Time and context

Consumer kind distinguishes main clock, tooltip body/title, LED, LED preview and menu. The adapter separately owns each context lifetime. The request owner supplies a nonzero revision and must invalidate old requests when source/settings/owner context changes.

Time and beat100 are the exact supplied display basis. SampleTick is one captured GetTickCount64 value and the uptime-token input; it is not an independent hardware sampling timestamp. UTC is present only under TCF_OPT_UTC when the caller supplies its actual origin; otherwise the field is zero. The collector never guesses an origin across an ambiguous local hour.

TcfShiftTime uses the captured base, timezone bias, previous display year and query-local summer options. It reads no global timezone state and preserves both outputs on failure. It intentionally retains the current post-2023 tu branch, which subtracts one from the requested hour and preserves the previous US summer flag. This is formatter compatibility behavior, not a timezone rules database. The caller applies the legacy parser's hours-above-23 normalization before invoking the helper.

The collector records the base and distinct td/tu/te states inside compounds, respecting quotes and retaining display state across compounds. Calendar rows contain short/long date, 24-hour time, short/long weekday and short/long month strings resolved with effective user-default Windows locale/overrides. Valid bits record successful calls; unavailable strings are empty. Separators and effective AM/PM symbols are separate labels. Alternative year/era retain the initialized base behavior. English dde/mme names have fixed Sun-Sat and Jan-Dec meanings.

## Numeric fields

All values copy current process collector caches. RefreshedCategories records categories for which the caller says an update was attempted before capture; zero makes no freshness assertion. It does not certify successful hardware collection. Existing unknown/negative values remain distinguishable.

| Fields | Meaning |
| --- | --- |
| logicalProcessors, physicalCores | Reported counts, independent of storage capacity |
| sampledProcessors | Usable CPU array count, at most 64 and no greater than the nonnegative logical count; use this value for indexing |
| totalCpu, cpuUsage | Current percent samples |
| cpuClock, cpuClock2, clock2Average | MHz samples/average, selected by TCF_OPT_CLOCK2 |
| memory | Total/available physical, pagefile and virtual bytes, in TCF_MEMORY order; the MS conversion constant is explicit |
| network 0-11 | Receive/send byte totals, byte/second rates, decimal KB totals/rates, then decimal MB totals/rates, in interleaved receive/send order |
| network 12-20 | LTE/WiFi/Ethernet state/activity at 12/15/18 and cached byte counters at 13-14/16-17/19-20 |
| diskFree, diskTotal | 36 slots in existing MB units followed by their GB values; drive letters then ten configured mounts |
| diskRead, diskWrite, diskRate | 26 byte/second drive rates |
| Battery/power fields | Raw percentage/lifetime, hours/minutes/seconds, power mode, charging option |
| Volume/GPU/temperature fields | Existing samples; mute/temperature availability are explicit options |
| Connection/adapter/profile fields | Existing connection selection, LTE profile, VPN/metered options and display widths |

Capture does not poll devices, clear actdvl or change counter ownership. The adapter must merge concurrent clock/tooltip/LED/preview/menu demands and manage drive/counter retirement before promising fresh inputs.

## Unicode labels and CUSTOM caches

Capacities include terminators: format 4096 UTF-16 units, each label 128, and each of 32 CUSTOM values 4096. Full values fit without a 64 KiB truncation.

The producer preserves existing text conversions. SSID/APN/LTE use the current UTF-8 boundary. Era/mute/IP and NMX use the configured legacy display-codepage boundary. NMX is captured before width/padding with the same selector as the built-in formatter. Current GIP byte-as-character rendering is captured as Unicode units, with aligned IPv4 text separate. No legacy byte pointer crosses the body.

CUSTOM capture only reads tc_custom_get_emit_value. It never loads CUSTOM settings, calls tc_custom_get_value/refresh_one, reads configured data files, launches scripts or moves refresh/execution deadlines. TCF_OPT_CUSTOM_READY identifies loaded settings; an unloaded cache exposes no CUSTOM values/state. PRESENT records nonempty data; PATH/SCRIPT describe existing modes without exposing paths/commands. The owner performs scheduled updates separately. Effective AM/PM symbols still use the existing read-only setting branch.

GIP copying uses TryEnterCriticalSection after existing lock initialization and returns ERROR_BUSY on contention. Collector failure frees unpublished storage and leaves the caller's output pointer unchanged.

## Scheduling and evidence limits

FormatCaptureSourceW is internal, declared in tcdll.h and absent from the normal module-definition exports. It validates before returning owned storage and preserves legacy day/month/DST state. Locale-table construction is currently synchronous local work; actual capture/update budgets and scheduling must be measured before repaint/animation integration.

Owned private-DLL tests cover every numeric array, Unicode/legacy labels, immutable copies, unchanged CUSTOM state, GIP contention, maximum reachable 512 time states with 131040 CUSTOM units, and 144 comparisons with the existing offset helpers. They do not establish provider dispatch, host/window replacement, whole-frame publication, UI latency or visible output.

Normal builds compile the implementation directly from tc2ch without source/scripts or test-export dependencies.
