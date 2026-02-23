# TCalendar Error Codes (v1)

## Standard Codes
- `OK`
- `VALIDATION_ERROR`
- `NOT_FOUND`
- `CONFLICT`
- `INTERNAL_ERROR`
- `UNSUPPORTED_API_VERSION`
- `NOT_IMPLEMENTED`

## Mapping Rules
- Validation failures: malformed payload, missing required fields, invalid type/size.
- Not found: target `id` does not exist.
- Conflict: optimistic update conflict or duplicate constraint.
- Internal error: unexpected host/storage failure.
- Unsupported version: `apiVersion` cannot be processed by current host.

