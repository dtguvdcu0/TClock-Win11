# Encoding Policy (Migration Phase)

Date: 2026-02-14
Scope: `source/` tree

## Policy
1. Runtime migration target is UTF-16 + W API.
2. File I/O target is UTF-8.
3. Source file target is UTF-8 (UTF-8 BOM allowed where toolchain requires).

## Non-Targets (Do Not Convert)
1. Binary assets (`.bmp`, `.png`, `.ico`, `.dll`, `.exe`, `.lib`, `.pdb`, `.obj`, `.zip`).
2. Resource/tool-generated artifacts under deferred class (example: `.aps`, selected `.rc` until dedicated phase).

## Current Migration Rule
1. Newly added source/text files must be UTF-8.
2. Edited existing legacy files should be migrated to UTF-8 when safe.
3. If safe migration is unclear, keep encoding unchanged and log defer reason in `tasks/`.

## Guard Usage
- Audit all tracked files:
  - `python source/scripts/encoding_guard.py --mode audit`
- Check currently touched files (policy check):
  - `python source/scripts/encoding_guard.py --mode touched --strict`
- Check files changed from a base commit:
  - `python source/scripts/encoding_guard.py --mode diff --base <commit> --strict`

## Notes
1. This policy explicitly excludes binary conversion.
2. Shift-JIS legacy remains during transition, but no uncontrolled new expansion is allowed.
