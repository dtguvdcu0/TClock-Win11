# Holiday Provider Manifest + JS Design

## Status

- This document now describes the active cutover target and the shipped pairing rule for manifest/script-backed providers.
- Current implementation direction:
  - provider id stays extensionless in settings
  - runtime resolves `<id>.ini`
  - runtime resolves the script as the same basename `<id>.js`
  - the manifest does not declare a separate `script=` path
- Current shipped behavior outside this cutover remains documented in:
  - `holiday-provider-spec.md`
  - `config.md`

## Design Goal

- Move `jp-public-holiday` from a flat row text provider to a manifest-driven provider.
- Keep extensionless provider ids in settings and stored subscriptions.
- Split responsibilities between:
  - `INI` manifest
  - `JS` evaluator

## Why This Split Fits The Current Codebase

- The current host already persists provider selections through:
  - `HolidaySubscriptionFiles`
  - `HolidaySubscriptionCatalog`
- The current settings UI already expects a visible file/provider identity.
- Japanese holiday logic still requires algorithmic behavior:
  - equinox calculation
  - substitute holiday logic
  - citizen holiday logic
  - historical era branching

Therefore:

- `INI` should own visible/provider metadata and declared rule inventory.
- `JS` should own runtime evaluation.
- Localized strings in `INI` must be treated as opaque user-facing text.
- Runtime must not require English-only names, descriptions, or holiday labels.

## Provider Resolution Model

Provider ids remain extensionless.

Example provider id:

- `jp-public-holiday`

Planned runtime resolution order:

1. `tcalendar/providers/<id>.ini`
2. same-directory `tcalendar/providers/<id>.js`
3. explicit direct file path entries already saved by the user

## Manifest Role

The manifest is the provider entrypoint.

It should declare:

- provider identity
- visible name
- description
- source/provenance
- supported year range
- declared rule catalog
- declared postrule catalog

The manifest should be human-readable and diff-friendly.

## Manifest Schema

### Provider Section

```ini
[provider]
id=jp-public-holiday
name=Japanese Public Holidays
description=Workbook-derived historical holiday provider
type=js
range_start=1873
range_end=2150
version=1
source=workbook-vba-migrated
```

### Base Rule Sections

Supported rule kinds in the first version:

- `fixed`
- `monday_fixed`
- `limited`
- `spring_equinox`
- `autumn_equinox`

Examples:

```ini
[rule_001]
name=元日
kind=fixed
start=1949
end=9999
date=1/1

[rule_002]
name=成人の日
kind=fixed
start=1949
end=1999
date=1/15

[rule_003]
name=成人の日
kind=monday_fixed
start=2000
end=9999
month=1
nth=2
weekday=mon

[rule_004]
name=春分の日
kind=spring_equinox
start=1949
end=9999

[rule_005]
name=即位の日
kind=limited
start=2019
end=2019
date=5/1
```

### Postrule Sections

Supported postrule kinds in the first version:

- `substitute_holiday`
- `citizen_holiday`

Example:

```ini
[postrule_001]
kind=substitute_holiday
start=1973

[postrule_002]
kind=citizen_holiday
start=1948
```

## JS Evaluator Contract

### Input Contract

The host should:

- load the manifest
- parse base rule sections
- parse postrule sections
- pass normalized manifest data into the evaluator

Recommended call shape:

```js
function getHolidays(year, manifest) {
  return [
    { date: "2026-01-01", name: "元日", kind: "public" }
  ];
}
```

### Output Contract

Required record fields:

- `date`
- `name`
- `kind`

Date format:

- `YYYY-MM-DD`

Allowed `kind` values in the first version:

- `public`
- `citizen`
- `substitute`
- `closure`

### Evaluator Responsibilities

The evaluator should:

- expand base rules for the requested year
- calculate spring/autumn equinox dates
- evaluate Monday-fixed rules
- apply substitute holiday generation
- apply citizen holiday generation
- return normalized rows only

### Evaluator Restrictions

The evaluator should not:

- perform arbitrary file IO
- perform network access
- mutate host state
- depend on browser DOM APIs

The host should expose only the minimum execution surface required for deterministic holiday generation.

## JS Engine Execution Model

### Current Codebase Constraint

- The current codebase has WebView2 as its only JavaScript-capable runtime component.
- The current startup contract already requires holiday rows before first UI render:
  - `system.getStartupState`
  - `monthHolidayItems`
  - `taskHolidayItems`
- Because of that, using the visible UI WebView2 instance as the holiday evaluator is a poor fit:
  - startup becomes circular
  - provider evaluation becomes coupled to page lifecycle
  - holiday generation would depend on WebView content readiness

### Recommended Direction

- Use a dedicated embedded JS engine for provider evaluation.
- Do not use the visible UI WebView2 instance as the runtime holiday evaluator.

### Engine Recommendation

Primary recommendation:

- Duktape

Why:

- straightforward C/C++ embedding model
- compact footprint
- simple integration for a narrow provider API
- adequate language support for constrained holiday-provider scripts

Secondary option:

- QuickJS

Why:

- broader modern ECMAScript support
- still embeddable and small

Tradeoff:

- stronger language support than this provider model likely needs
- somewhat higher integration/maintenance cost for this codebase than a smaller ES5-class engine

### Practical Decision Rule

- If provider scripts are intentionally constrained to a small supported subset, choose Duktape.
- If the project explicitly wants modern JS syntax/features for provider authors, choose QuickJS.
- For the current Japanese holiday provider use case, Duktape is the better fit.

### Frozen Choice For This Migration

- Use Duktape for the first implementation.
- Do not add QuickJS in parallel.
- Revisit engine choice only if a concrete provider requirement exceeds the supported JS subset.

### Host API Surface For The Embedded Engine

The embedded engine should expose only:

- manifest/rule data input
- `year`
- optional helper bindings if strictly necessary

The engine should not expose:

- filesystem access
- network access
- arbitrary native host object access
- UI-facing bridge APIs

## Host Parse Structures

The host should introduce explicit parse/result structures instead of extending the legacy flat-row path.

Recommended structures:

```cpp
struct HolidayManifestRule {
    std::wstring section;
    std::wstring name;
    std::wstring kind;
    int start_year = 0;
    int end_year = 0;
    std::wstring date_spec;
    int month = 0;
    int nth = 0;
    std::wstring weekday;
};

struct HolidayManifestPostRule {
    std::wstring section;
    std::wstring kind;
    int start_year = 0;
};

struct HolidayProviderManifest {
    std::wstring provider_id;
    std::wstring display_name;
    std::wstring description;
    std::wstring source;
    std::wstring script_file;
    int range_start = 0;
    int range_end = 0;
    std::vector<HolidayManifestRule> rules;
    std::vector<HolidayManifestPostRule> postrules;
};
```

These names are a design target, not a locked ABI.

## Host Runtime Flow

Recommended runtime sequence for a subscribed provider id:

1. resolve provider id to manifest path
2. parse manifest
3. resolve script path relative to the manifest directory
4. execute evaluator for each requested year
5. merge returned rows into the existing subscription aggregation map
6. preserve current later-entry-wins merge behavior

## Recommended Host Entry Points

Recommended internal split:

- `ResolveProviderManifestPath(...)`
- `LoadHolidayProviderManifest(...)`
- `EvaluateHolidayJsProvider(...)`
- `MergeHolidayManifestProvider(...)`

Recommended cutover target:

- replace direct `jp-public-holiday` alias -> `.txt`
- with manifest resolution inside the existing subscription merge pipeline

## Duktape Invocation Contract

Recommended host-side call shape:

1. create Duktape heap
2. load script file as UTF-8 text
3. evaluate script once
4. push:
   - `year`
   - manifest JSON or equivalent normalized object
5. call global `getHolidays`
6. validate returned array items into `HolidayItem`
7. destroy heap or reuse per provider evaluation scope if profiling later justifies it

Required validation after script return:

- array root required
- each item must contain:
  - `date`
  - `name`
  - `kind`
- `date` must be normalized `YYYY-MM-DD`
- out-of-range rows must be rejected

## Error / Status Model

`system.getHolidaySubscriptionStatus` should distinguish at least:

- `Loaded`
- `Manifest read failed`
- `Manifest parse failed`
- `Script missing`
- `Script read failed`
- `Script execution failed`
- `Invalid result`
- `Missing`

Recommended extra status fields:

- `sourceType`
  - `ini+js`
  - `txt`
- `providerId`
- `manifestPath`
- `scriptPath`

## Settings Cutover Requirements

Current UI-specific behavior to replace:

- `jp-public-holiday` normalization directly to `jp-public-holiday.txt`

Planned replacement:

- treat `jp-public-holiday` as provider id
- let host resolve it to manifest/script
- settings should display:
  - provider id
  - source type
  - manifest/script-backed load result

The checklist UX itself can remain unchanged.

## Mapping From Existing C++ Logic

The current dead-code C++ path maps into the new model like this:

- `kJpPublicHolidayRules[]`
  - source for manifest rule inventory
- `GetSpringEquinoxDay(...)`
  - JS evaluator function
- `GetAutumnEquinoxDay(...)`
  - JS evaluator function
- `GetNthMondayDateKey(...)`
  - JS evaluator helper
- `IsCitizenHoliday(...)`
  - JS postrule helper
- `GetSubstituteHolidayName(...)`
  - JS postrule helper

## Runtime Status / UI Implications

Settings should continue to show:

- extensionless provider id
- enabled/disabled state
- delete action
- load status

Planned source-type visibility:

- `ini+js`
- `missing`
- `read failed`

The UI should not assume `jp-public-holiday.txt` directly once this design is implemented.

## Compatibility Strategy

- keep generic direct text-row subscriptions for explicit file paths when needed
- do not keep `jp-public-holiday` alias fallback to `.txt`
- migrate old saved `jp-public-holiday.txt` entries to the extensionless provider id in Settings
- keep explicit file path subscriptions working
- keep extensionless provider ids stable

After parity is proven:

- remove direct `.txt` assumptions from alias resolution and UI normalization

## Verification Plan

The new runtime must be verified against:

- the currently shipped historical provider output
- the VBA/workbook-derived validation stock
- representative edge years:
  - pre-1900 equinox years
  - 1926
  - 1948 transition years
  - 1973 substitute-holiday start
  - modern special-year exceptions

## Recommended Implementation Order

1. Finalize manifest schema.
2. Finalize JS evaluator contract.
3. Build `jp-public-holiday.ini`.
4. Port C++ holiday logic into `jp-public-holiday.js`.
5. Add host manifest loader.
6. Add host JS execution path.
7. Run side-by-side verification against current provider output.
8. Cut alias resolution from `.txt` to `.ini + .js`.
