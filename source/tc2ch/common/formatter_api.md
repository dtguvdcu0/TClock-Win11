# Typed formatter feature

Status: the SDK body, owned host selection and in-host adapter are implemented.
The normal clock uses the selected `tclock.formatter/2` provider for the main
text and the tooltip body/title through the exe-side format bridge
(`format_bridge.h`); `/1` is not used by the clock. LED text and menu labels
always use the built-in formatter. An example provider,
SampleFormatter, is maintained in the separate TClock-Plugin repository.

## Registration and lifetime

Include `formatter_api.h`; compile `formatter_api.c` and `format_source.c`.
Register an immutable `TC_FORMATTER` through `TC_HOST_FEATURES.add` with interface
`tclock.formatter/1`, a stable feature ID, priority and plugin-owned user context.
The feature table size must equal the current native `sizeof(TC_FORMATTER)`.
The table contains process pointers and must never be serialized. The source and
result bodies contain no process pointers, HWNDs, HDCs or callback addresses.
Existing runtime/registry /1 tables and their layouts are unchanged.

The callback receives the original validated [source body](format_source.md),
its actual counted byte length, output capacity and an exclusively staged result.
It must derive the complete result from these inputs without requiring built-in
formatted text. Keep the callback pure: no collection, settings writes, script or
program execution, deferred input retention or writes after return. Input and
output pointers are borrowed only for this call. The registration owns its table
and user data through all callbacks and stop.

`pfrm_prepare` is host-private: it selects the highest priority registration,
then ordinal provider ID and feature ID, and snapshots its full interface,
stable slot, provider, registration token and activation revision. It validates
and copies static demand while the provider is pinned busy. Failure preserves
the caller's binding. The existing generic dispatch path is unchanged.

`pfrm_query` revalidates the selected owner before and after the pinned callback.
A busy winner returns `ERROR_BUSY`; it never invokes a lower priority DLL.
Observed disable/re-enable, remove/re-register, winner replacement, shutdown and
policy read failure reject stale results. A reference contains no borrowed table
or user pointer. Registration and activation exhaustion already fail closed.
Unobserved external settings changes that return to the same value between
observations are outside the runtime's observable-history contract.

## Static collection demand

`TC_FORMAT_DEMAND` is 40 bytes on x86 and x64. Set its exact size, known category
bits, 36-bit space-drive mask, 26-bit rate-drive mask and 32-bit CUSTOM mask.
SPACE, RATE and CUSTOM category presence must exactly match a nonempty respective
mask. Zero categories is valid for a time-only formatter. `consumers` is nonempty:
bit `consumer - 1` names main, tooltip body, tooltip title, LED, LED preview and menu.
`intervalMs` is 20 through 60000; both reserved values must be zero.

Demand and callback stay immutable for the registration lifetime; remove and
register a new table to change demand. Unsupported consumers return `S_FALSE`
after owner validation without invoking the DLL. Demand declaration does not
assert collection freshness. Cycle 314 implements a process-local consumer demand
union and source-clock collector connection; see [format_demand.md](format_demand.md).
The format bridge opens one main-consumer lease while a /2 provider is selected;
LED and menu consumers are not routed. Source `refreshedCategories` records attempted collection, not hardware
success. A CUSTOM demand does not authorize executing a script in this callback.

## Pure worker interface /2

Register `TC_FORMATTER2` with `tclock.formatter/2` for independently owned,
immutable snapshot execution. The native table is 72 bytes on x64 and 64 bytes
on x86 with eight-byte packing. It reuses the unchanged demand, source /1 and
frame /1 records. `/1` remains synchronous on the UI thread; its table, callback
and thread meanings are unchanged. The clock bridge negotiates /2 only and runs
it on the worker described below.

The UI calls `capture(user, &snapshot)` promptly. Exact `S_OK` transfers one
strong snapshot reference; any other result transfers none. Capture must copy
or independently retain all mutable configuration it needs. Replace the feature
registration when demand or effective configuration changes. The host copies
all callbacks before capture, since capture may remove/re-register and free the
original table/user. The original context never crosses to the worker.

`format(snapshot, source, sourceBytes, capacity, output)` runs on a host-owned
native threadpool worker, serialized per provider/module. It may read only its
snapshot and the borrowed immutable source, and write only its exclusive output.
It must not call host/registry/UI APIs, mutate live plugin state, collect sources,
run scripts, or retain input/output pointers. Ordinary commands, pages, reload
and stop can run on the UI while formatting. Synchronization of any independent
snapshot storage is the provider's responsibility.

`release(snapshot)` runs on the UI after computation, even if output is rejected,
the provider was removed/disabled/stopped, or another runtime instance started.
Release must depend only on the strong snapshot and retained module code; it
must not access an expired runtime context. Accepted modules remain loaded until
process exit. This feature does not provide crash/hang isolation or preemption.
A provider with nonterminating native code can retain its pending task until exit.

The host-private `plugin_format_async` adapter stages a deep validated input copy
on an I/O or UI thread. UI launch selects/validates the owner and takes one
monotonic module hold; duplicate provider work is rejected without switching to
another provider. UI polling never waits for computation. Completion validates
the whole result on the worker, releases the snapshot/hold on UI, rechecks current
selection/activation/policy, and commits only an exact successful whole frame.
Errors, decline, invalid output, cancellation and stale identity preserve output.
A NULL finish destination explicitly discards a result. Finish is one-shot;
disposal rejects running or unsettled tasks. Ownership calls are serialized by
the caller. Cancellation suppresses publication and leaves storage owned until
native computation completes; it cannot interrupt a callback.

The hold pool admits at most 64 simultaneous provider jobs. The format bridge keeps
one task in flight, serves pending consumer slots in order, rejects results of an old
generation or older than the capture age limit, and settles a running task before
its own teardown.

## Complete output

Call `TcfInitFrame(output, source->revision)` before constructing a successful
result. `TC_FORMAT_FRAME` is exactly 55328 bytes with eight-byte packing, version,
exact bytes, echoed source revision, UTF-16 text, one role byte per UTF-16 unit,
and at most 256 ordered named spans. Capacity is 1 through 4096 UTF-16 units
including NUL; declared text length excludes NUL. Reserved values must be zero.
Unused storage is not semantically inspected; the initializer clears all storage.

Roles are TEXT=1, DATE=2, WEEKDAY=4, TIME=8 and ACCENT=88. Metadata terminates at
the same length as text. Embedded NUL, isolated surrogates and different roles
on halves of one surrogate pair are invalid. Spans cannot overlap or split a
surrogate pair. Empty named slots are valid. Selectors start with an ASCII letter
or underscore, continue with letters/digits/underscores, and fit 31 units plus
NUL. Occurrences count earlier spans with the same case-sensitive selector,
starting at zero. A span may contain mixed roles; its zone is the default role
for a later replacement, not a claim that all contained role bytes are equal.

Partial styles use explicit field bits, 24-bit RGB values, font face up to 31
UTF-16 units, font size 5 through 256, shadow range 0 through 10 and independent
bold/italic/shadow/border traits. Only active numeric/font fields are interpreted;
unknown field or trait bits are rejected. No GDI handles are accepted. Applying
host overrides while preserving other provider properties remains integration
work; this adapter does not apply Explorer-owned style rules in the host process.

`TcfAppendFrame` appends one complete UTF-16 chunk and optionally one named span,
computing occurrence and retaining a partial style. A rejected append leaves the
frame unchanged. This builder requires an exclusively owned initialized frame;
it does not validate all preceding content on each append. Final host validation
is mandatory, including for a provider that builds the frame without this helper.

Return exactly `S_OK` for a complete result, `S_FALSE` to decline or a failing
HRESULT for failure. A zeroed untouched result accompanied by S_OK is invalid:
it cannot accidentally blank a clock. The host uses a separate zeroed allocation,
checks the complete frame against the original revision and capacity, rechecks
the selected owner, then copies the whole result only on S_OK. Errors, declines,
overflow and stale ownership preserve the destination. Host `invoked` reports
the actual DLL format callback, including when its result is rejected.

## Evidence and remaining integration

Cycle 313 local C/C++ x64 and x86 tests exercise exact layout, bounded demand,
Unicode/roles/spans/styles, capacity and failure-atomic construction. Two
independently parsed SDK-only qualification DLLs use changing raw CPU/battery/
CUSTOM/time inputs through the actual normal host runtime in a private early
entry; they do not receive built-in formatted output. Runtime evidence includes
priority replacement, busy recursion, re-registration during callbacks,
disable/re-enable, shutdown and complete-result rejection.

Cycle 315 qualifies the /2 adapter with two SDK-only, independently parsed DLLs
and actual registered host lifecycle/command dispatch in a private early entry.
Its controlled blocked callback permits native message/command processing while
source snapshots and monotonic holds remain owned. Capture/release thread IDs,
worker execution, input copy, invalid/declined output, staged/running/completed
cancellation, remove/re-register, disable/stop/restart, release-after-stop,
duplicate hold rejection and foreign-thread denial are exercised.

These are adapter and actual-function qualifications. They do not establish
normal clock rendering, sizing, network extraction, tooltip/LED/menu routing,
26H2 taskbar behavior, bounded UI latency, transport authentication or final P3
acceptance. Maintained public alternative examples and cross-domain compatibility
qualification remain P6 work.
