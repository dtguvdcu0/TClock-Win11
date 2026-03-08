# TCalendar Overview

## Purpose
TCalendar is a Windows-focused calendar application built as a native C++ host with a WebView2 front end.

## Main Areas
- `host/`: native bootstrap, bridge handling, JSON validation, and task storage
- `web/default/`: default HTML, CSS, JavaScript, and manifest assets
- `web/user/`: user override area for customized web assets
- `data/`: runtime storage area for SQLite databases
- `third_party/webview2/`: bundled WebView2 SDK files required by the build

## Runtime Modes
- `TCalendar.exe`: normal windowed application mode
- `TCalendar.exe --smoke`: non-GUI bridge smoke mode for manual or CI verification

## Host Responsibilities
- initialize the native process and WebView2 runtime
- validate bridge payloads and envelope format
- expose calendar and task methods to the web layer
- manage persistent storage through SQLite

## UI Responsibilities
- render calendar and task views
- call host bridge methods using the documented API contract
- convert persisted UTC timestamps into local-time display values

## Runtime Notes
- The application verifies bundled WebView2 bootstrap readiness.
- Smoke output includes `bootstrapReady=true` when the bundled bootstrap path is available.
- Runtime data is stored under the executable-relative `data/` directory.
- Normal mode uses `data/tasks.db`.
- Smoke mode uses `data/tasks-smoke.db`.
- Persist UTC timestamps only and convert them to local time in the UI layer.
- Database migration is forward-only.

## Integration
- TClock integration is launch-only.
- TClock is expected to start `TCalendar.exe` and then let TCalendar manage its own window lifecycle.
- Requests and responses follow the JSON envelope defined in `api-contract.md`.
- Persistent task data follows `data-schema.md`.
- Error responses and code mapping follow `error-codes.md`.

## Dependency Boundary
- The build expects the bundled WebView2 SDK under `third_party/webview2/`.
- Missing required SDK files should fail the TCalendar build early.
