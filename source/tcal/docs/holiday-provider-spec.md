# TCalendar Holiday Provider Specification

## Purpose

This document defines the implementation-ready contract for holiday providers used by TCalendar.

The first provider target is:

- `jp-public-holiday`

This provider is based on the repaired and validated Excel/VBA holiday logic extracted from the user workbook.

## Scope

This specification covers:

- workbook historical holiday rows beginning in the 1870s
- statutory Japanese public holidays
- workbook year-end closure rows
- yearly holiday generation
- spring/autumn equinox handling
- substitute holiday handling
- `国民の休日` handling

## Provider Contract

## Input

- `providerId`:
  - required string
  - first supported value: `jp-public-holiday`
- `year`:
  - required integer-like value at the host boundary
  - provider must support the workbook-derived historical range shipped by the generator

## Output

Return a year-scoped ordered list of holiday records.

Minimum record fields:

- `date`
- `name`

Recommended extended fields:

- `kind`
- `source`
- `providerId`

Example:

```json
[
  {
    "date": "2026-01-01",
    "name": "元日",
    "kind": "public",
    "source": "jp-public-holiday",
    "providerId": "jp-public-holiday"
  }
]
```

## Source-of-Truth Rules

The migration baseline is the repaired exported VBA logic plus the worksheet table semantics.

Primary source artifacts:

- workbook: `【VBA】カレンダーフォーム.xlsm`
- worksheet module: `Sheet4.cls`
- standard module: `CalenderForm_ShowCalender.bas`

## Worksheet Interpretation Rules

Use displayed text semantics for worksheet-driven holiday rules.

Required rule:

- treat worksheet values as display text first
- do not derive holiday semantics from Excel date serial values unless explicitly proven numeric for that row type

This is especially important for workbook column `D`, which mixes multiple formats:

- full dates such as `2020/7/23`
- month/day values such as `4/29`
- movable expressions such as `1月第2`
- equinox placeholders such as `3/20(or21)` and `9/23(or24)`

## Base Rule Types

The validated worksheet/VBA path supports these base row kinds:

- `限定`
- `月曜固定`
- `固定`
- `春分`
- `秋分`

## Base Expansion Rules

### `限定`

- use the explicit year/month/day value as written
- apply only within the row year range

### `月曜固定`

- parse `N月第M`
- resolve to the `M`th Monday of month `N` in the target year

### `固定`

- parse `month/day`
- apply to the target year

### `春分`

- use the validated VBA-equivalent formula
- preserve VBA `Int(...)` semantics, not language-default integer truncation

Formula buckets:

- `1900-1979`: `Int(20.8357 + 0.242194 * (y - 1980)) - Int((y - 1980) / 4)`
- `1980-2099`: `Int(20.8431 + 0.242194 * (y - 1980)) - Int((y - 1980) / 4)`

### `秋分`

- use the validated VBA-equivalent formula
- preserve VBA `Int(...)` semantics, not language-default integer truncation

Formula buckets:

- `1900-1979`: `Int(23.2588 + 0.242194 * (y - 1980)) - Int((y - 1980) / 4)`
- `1980-2099`: `Int(23.2488 + 0.242194 * (y - 1980)) - Int((y - 1980) / 4)`

## Name and Duplicate Rules

- resolve candidate holidays from worksheet rows in worksheet order
- if the same holiday name appears more than once for the same target year, keep the first applicable row

This follows the exported VBA `GetYearHolidayMap` behavior.

## Derived Holiday Rules

Derived holiday rules run after base holiday expansion.

Evaluation order:

1. base holiday
2. `国民の休日`
3. substitute holiday

### `国民の休日`

Return `国民の休日` when all conditions are true:

- target year is `>= 1948`
- target date is not Sunday
- previous day is a base holiday
- next day is a base holiday
- target date itself is not already a base holiday

### Substitute Holiday

Return `<base holiday name>振替` when all conditions are true:

- target year is `>= 1973`
- target date itself is not already a base holiday
- scan backward across consecutive holiday days using:
  - base holidays
  - `国民の休日`
- if the scan reaches a Sunday that is a base holiday, assign substitute holiday to the first later non-holiday date under evaluation

This rule must support chained carry-forward cases such as:

- `2026-05-03` Sunday `憲法記念日`
- `2026-05-04` holiday
- `2026-05-05` holiday
- `2026-05-06` => `憲法記念日振替`

## Workbook Coverage Rule

`jp-public-holiday` currently mirrors the workbook holiday table rather than a narrowed public-only subset.

This includes:

- historical pre-1948 observances present in the workbook
- workbook year-end closure rows

## Subscription File Overlay

Holiday providers are activated only by subscription entries listed in:

- `HolidaySubscriptionFiles`

Current file format:

- UTF-8 text
- one record per line
- line format:
  - `YYYY-MM-DD|Name|Kind`
- `Kind` is optional
- blank lines are ignored
- lines starting with `#` or `;` are ignored

Merge rules:

- files are processed in listed order
- later files override earlier files for the same date
- `jp-public-holiday` is a provider alias that resolves to the shipped manifest/script pair
- file subscription items also override earlier subscription output for the same date

Resolution rules:

- relative paths are resolved from the directory containing `tcalendar.ini`
- provider alias resolution:
  - `jp-public-holiday` -> `tcalendar/providers/jp-public-holiday.ini`
  - runtime script pair -> `tcalendar/providers/jp-public-holiday.js`

## Ordering Rule

Provider output should be ordered by date ascending.

## Provider Artifact

Current delivery shape:

- `jp-public-holiday` is delivered as a generated manifest/script provider.
- The canonical shipped files are:
  - `tcalendar/providers/jp-public-holiday.ini`
  - `tcalendar/providers/jp-public-holiday.js`

Update path:

- regenerate the provider files from the workbook-driven manifest/script generator
- generator script:
  - `source/scripts/generate_jp_public_holiday_manifest_provider.py`
- redeploy the provider files beside `TCalendar.exe`

Required properties:

- user-visible as a concrete subscribed artifact
- updateable independently from the host binary
- compatible with existing subscription ordering and override rules
- safe for UTF-16 path handling on Windows

## Validation Baseline

The repaired exported VBA logic was validated against an independent implementation across:

- corrected equinox comparison range:
  - `1948-2027`
- multiple random 10-year sample rounds from `1960+`

Validation artifacts:

- `backups/equinox_compare_1948_2027_vba_int.csv`
- task investigation log under `tasks/2026-04-22-tcal-holiday-provider-migration-plan.md`

## Native Port Guidance

Recommended host placement:

- `source/tcal/host/`

Recommended decomposition:

- worksheet-derived base rule loader or embedded normalized rule table
- year holiday expander
- derived-rule post-processor
- host bridge serializer for year-scoped responses

Do not port Excel form behavior into the provider.

Port only:

- holiday computation
- provider output ordering
- provider mode separation

## Non-Goals

This specification does not define:

- task list rendering
- holiday UI colors
- workbook shortcut key behavior
- Excel form lifecycle

Those belong to UI or migration tooling, not the provider core.
