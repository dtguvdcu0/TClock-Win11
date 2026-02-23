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
- `task.get`
- `task.create`
- `task.updateTitle`
- `task.delete`
- `task.toggleDone`
- `settings.getTokens`
- `system.getVersion`

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

## Host Bootstrap Status (Current)
- Host initialization now verifies WebView2 loader bootstrap readiness.
- The runtime smoke output includes `bootstrapReady=true` when bundled loader bootstrap is available.


## Persistence Contract (Current)
- Storage backend: SQLite (`winsqlite3`).
- Storage path policy: executable-relative fixed path.
- Host is responsible for ensuring `<exe_dir>/data` exists before DB use.
