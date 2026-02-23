# TCalendar Data Schema (v1)

## Schema Version
- `schemaVersion: 1`

## Task Record (Required)
- `id` (string)
- `date` (string, ISO 8601)
- `title` (string)
- `done` (boolean)
- `updatedAt` (string, UTC ISO 8601 with `Z`)

## Task Record (Optional)
- `startAt` (string, UTC ISO 8601 with `Z`)
- `endAt` (string, UTC ISO 8601 with `Z`)
- `memo` (string)
- `todo` (boolean)
- `alertAt` (string, UTC ISO 8601 with `Z`)

## Time Policy
- Persist UTC only.
- Convert to local time in UI rendering.

## Migration Policy
- Forward-only migration scripts.
- On migration failure:
  - keep backup
  - do not write new data
  - enter safe mode

