# TCalendar API Contract (v1)

## Envelope
All responses must use:

```json
{
  "ok": true,
  "code": "OK",
  "message": "",
  "requestId": "abc-123",
  "data": {}
}
```

## Required Request Fields
- `apiVersion` (string)
- `requestId` (string)
- `method` (string)
- `params` (object)

## Minimum Methods
- `calendar.getMonthSummary`
- `calendar.getDayTasks`
- `calendar.getRangeHolidays`
- `task.get`
- `task.create`
- `task.updateTitle`
- `task.delete`
- `task.toggleDone`
- `settings.getTokens`
- `system.getVersion`
- `system.getStartupState`
- `system.appReady`

## Version Rule
- Current: `1.0`
- Requests with unsupported version must return:
  - `ok=false`
  - `code=UNSUPPORTED_API_VERSION`

## Parser and Type Rules (Current)
- Root payload must be a JSON object.
- Supported JSON value types in parser: `object`, `string`, `bool`, `null`.
- Numeric and array literals are currently rejected by parser.
- Method arguments must be provided under `params` as a JSON object.
- Type mismatches in required method fields must return `VALIDATION_ERROR`.

## Startup State Method (Current)
- `system.getStartupState` batches initial UI config and task data for first render.
- Required params are string fields: `selectedDate`, `monthFrom`, and `monthTo`.
- Response data includes `config`, `monthRange`, `monthItems`, `taskRange`, and `taskItems`.
- Response data also includes `monthHolidayItems` and `taskHolidayItems` for the current holiday subscription set.
- `taskRange` is derived from `selectedDate` plus the current host default range config.

## Holiday Range Method (Current)
- `calendar.getRangeHolidays` returns year/range-projected holiday records for the current holiday subscription set.
- Required params are string fields: `dateFrom` and `dateTo`.
- Response data includes `providerId` and `items`.
- Each item currently includes `date`, `name`, and `kind`.
- Response items are produced only from configured subscription entries.

## Holiday Subscription Status Method (Current)
- `system.getHolidaySubscriptionStatus` returns the current holiday subscription source load state.
- Current params may be empty, or may include `holidaySubscriptionFiles` as a `|`-separated override string for preview.
- Response data includes `files`.
- Each file item currently includes:
  - `path`
  - `resolvedPath`
  - `exists`
  - `loaded`
  - `recordCount`
  - `appliedCount`
  - `error`
- File entries are returned in the configured `holidaySubscriptionFiles` order.

## Holiday Provider Catalog Method (Current)
- `system.getHolidayProviderCatalog` returns available holiday providers from the fixed provider directory.
- Response data includes `items`.
- Each item includes:
  - `id`
  - `name`
  - `sourceType`
  - `ready`

## View Config Fields (Current)
- `system.getViewConfig` and `system.getStartupState.config` also expose:
  - `holidaySubscriptionFiles`
  - `holidaySubscriptionCatalog`
- `system.setViewConfig` accepts:
  - `holidaySubscriptionFiles`
  - `holidaySubscriptionCatalog`
- Current storage contract:
  - a single string using `|` as the separator between subscription entries
  - `holidaySubscriptionCatalog` stores saved checklist rows as `1 <path>` or `0 <path>` entries joined by `|`

## Startup Readiness Method (Current)
- `system.appReady` is sent by the WebView UI after first task view render.
- Host returns `OK` and uses the notification to end the native startup skeleton state.
- Current params are advisory and may include `phase` and `elapsedMs` as strings.

## Host Bootstrap Status (Current)
- Host initialization now verifies WebView2 loader bootstrap readiness.
- The runtime smoke output includes `bootstrapReady=true` when bundled loader bootstrap is available.


## Persistence Contract (Current)
- Storage backend: SQLite (`winsqlite3`).
- Storage path policy: executable-relative fixed path.
- Host is responsible for ensuring `<exe_dir>/data` exists before DB use.
