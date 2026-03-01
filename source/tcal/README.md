# TCalendar

TCalendar is a Windows-focused minimal calendar application built with a native C++ host and a WebView2 UI.

## Layout
- `host/`: native host and bridge skeleton
- `web/default/`: default template (`index.html`, `app.js`, `styles.css`, `manifest.json`)
- `web/user/`: user-customized template location
- `data/`: local storage files (runtime)
- `docs/`: API/data/error contracts

## Current Status
- Phase 0 bootstrap files are created.
- Host and bridge are skeleton implementations.
- Template and contract documents are ready for Phase 1 implementation.

## Build Commands
- Build TCalendar only: `source/build_tcalendar.bat`
- Build TClock only: `source/build_tclock.bat`
- Router build entry: `source/build.bat tcalendar` or `source/build.bat tclock`

Notes:
- TCalendar deploy target is `source/x64/Release/TCalendar.exe`.
- Verification command (quick): `python source/scripts/migration_gate.py --mode touched --skip-build`
- Verification command (full): `set BUILD_TARGET=tcalendar && python source/scripts/migration_gate.py --mode touched`

## Bundled WebView2 SDK
- SDK files are vendored under `source/tcal/third_party/webview2/`.
- Required files:
  - `include/WebView2.h`
  - `include/WebView2EnvironmentOptions.h`
  - `lib/x64/WebView2LoaderStatic.lib`
  - `bin/x64/WebView2Loader.dll`
- `source/tcal/build.bat` fails fast if any required bundled file is missing.

## Process Model
- TCalendar runs as an independent process.
- TClock integration contract is launch-only (spawn TCalendar process).
- TCalendar window lifecycle is managed inside TCalendar process itself.

## Runtime Modes
- `TCalendar.exe` : standalone window mode.
- `TCalendar.exe --smoke` : non-GUI bridge smoke mode for CI/manual verification.


## Storage Policy
- Task persistence uses SQLite (`winsqlite3`).
- DB location is fixed to executable-relative path:
  - normal: `<exe_dir>/data/tasks.db`
  - smoke: `<exe_dir>/data/tasks-smoke.db`
- This project intentionally keeps exe-relative storage as the current policy.
- Migration from exe-relative to other locations is out of scope unless policy changes in a future decision cycle.
