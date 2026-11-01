# Process-local formatter demand and clock collection

Status: implemented. In normal clock operation the format bridge (`format_bridge.h`)
opens a collection lease for the selected provider's demand while its route is
enabled and closes it when the route ends.

## Ownership

`format_demand.h/.c` is host-private native state, not a packet or a plugin
callback table. One owner thread serializes calls. A process holds at most 64
live consumer leases; admission failure preserves every existing owner and
falls back rather than evicting one. A lease lasts through an actual clock,
tooltip, LED context, preview or menu lifetime, not one paint/snapshot invocation.
The bridge must bind that lifetime and close it at teardown.

Opening a lease checks the selected consumer against the immutable formatter's
supported-consumer mask. Handles increase monotonically and are not reused.
Replacement validates before mutation, increments that owner's revision and
makes its next sample eligible immediately. An identical replacement is S_FALSE
with no change to its deadline. Closing a stale handle cannot remove a later
owner reusing the storage slot. Handle/revision exhaustion rejects the attempted
change without wrapping or partially changing state.

Snapshots merge category, 36-bit space, 26-bit rate and 32-bit CUSTOM demand.
Only actual consumer bits are merged; the minimum requested interval is retained.
Taking due work returns separate active and due unions plus owned
lease/revision/consumer tickets, and advances each due deadline independently.
An empty union has zero interval/consumers and is not a valid registration table.
Elapsed subtraction is bounded; a clock rollback makes owners due once.

## Clock adapter and paths

`dll/format_collect.h/.c` owns one process-local scheduler and binds its mutation
thread on first successful admission. Wrong-thread calls cannot mutate it.
Minimal/Safe mode denies admission and replacement. Reset preserves the serial
across clock teardown, clears live leases, frees mount storage and permits the
next clock owner thread. It does not invoke a provider.

Selected additional mount paths are captured through `tc_read_valueW` into an
owned UTF-16 snapshot at admission/replacement or settings reload. The reader
uses the existing scoped UTF-8/legacy ingress decoder, returns literal Unicode
values, performs no settings writes or UTF8Hex cleanup, and preserves output on
missing/error/overflow. It never converts a Unicode path to ACP. Missing mount
slots remain unavailable; the mount adapter removes one enclosing quote pair.
Complete paths fit 32767 UTF-16 units plus NUL. Existing INI-context path limits
of the scoped reader still apply.

Admission/replacement stages both demand and mount data before committing.
Read/allocation failure retains the prior owner, path snapshot and caller's
lease output. Closing requires no INI access, including while the INI is locked.
The last mount owner frees the snapshot. Reload of changed mount data advances
the affected lease revisions; a failed reload during normal ReadData retires the
leases rather than continuing with stale path data. Legacy
`strAdditionalMountPath` readers and their compatibility behavior remain.

## Actual collector connection

The existing SYSINFO timer now calls `CollectFormatSources`. It merges due
provider categories with the existing battery/memory/network/CPU/volume/GPU/
temperature collection flags and invokes the original collectors. The no-lease
path retains those flags and the original hardware calls. Connection polling,
selected CUSTOM refresh and GIP refresh stay on this source-timer boundary, not
in paint, source capture or a provider callback. Existing CUSTOM file/script
deadlines and execution policy apply. The old normal CUSTOM tick still processes
its configured set; this change does not silently stop existing scheduled work.

Formatting cadence is distinct from hardware sampling. The scheduler implements
the requested 20..60000 ms eligibility, but this clock adapter is currently
polled by the existing approximately one-second SYSINFO timer. A 20 ms request
does not promise 20 ms hardware samples. Normal-clock frame submission cadence,
queue ownership, latency budgets and all transient-process adapters still need
the bridge integration.

Provider-only space and rate masks are independent of `actdvl`; never clear or
overwrite that legacy accumulating scan state. `Hdd_sweep` queries selected
provider space slots through GetDiskFreeSpaceExW, including the wide mount
snapshot, and preserves unrelated legacy cache slots. The original Hdd_get
entry still performs its legacy collection and also serves selected provider
slots.

Disk rate polling records legacy requirements only when the actual legacy
DiskRate_get entry samples them. Provider-only DiskRate_sweep cannot acquire
an unrelated historical drive merely because its scan bit exists. It retains
already established legacy ownership and overlapping provider owners. Removing
the last provider owner retires its counters on the next source poll even when
ordinary HDD collection is disabled.

Retirement tracks each remaining handle, suppresses all formatted reads for a
retiring slot, and retries failed removals before re-registering. Failed partial
registration rolls back through the same tracked path. Unaffected owners retain
their counters and sample history. Query-close failure retains the query,
handles and module; start cannot overwrite them and a later close can retry.
This ownership retention does not prove successful unload after an unrecoverable
PDH close failure; final teardown/failure qualification remains open.

Existing DiskRate_read_counter behavior is retained: failed/warmup reads expose
zero, and re-registration needs another sample. No per-drive hardware validity
or age guarantee is inferred from a finite cached value or
source.refreshedCategories. Such metadata/failure acceptance remains part of
the complete P3 source/publication contract.

## Evidence and limits

Maintained C/C++ x64 and x86 tests compile the real scheduler, wide reader,
clock adapter, hdd.c and diskrate.c. Each passes 160 checks, including 64-owner
admission, overlap/replacement/retirement, independent deadlines, wrap rejection,
clock rollback, actual wide/long/emoji mount lookup, locked-INI failure atomicity,
mode rejection, failed removals, partial-add rollback and failed-close retry.
PDH failures are deterministic injected return values, not claims that a real
PDH failure was reproduced. Existing real-PDH two-sample evidence is retained.

A private copy of the actual clock DLL invokes the actual SYSINFO collector
function without HookStart or live settings. It verifies real memory capture,
wrong-thread denial, selected CUSTOM indexes, actual Unicode mount collection
and final-rate-owner cleanup through the timer function. Each expanded run
passes 127 checks; counts include repeated capture loops. Temporary probes are
removed before the final normal build.

Three controlled runs measure no-owner collector overhead and raw source
construction with no plugin/IPC/rendering: ordinary 279048-byte captures and
the 945392-byte, 512-calendar-state maximum reachable grammar. These observations
do not establish a UI p99, multiple-context load, slow-provider behavior or a
full application latency baseline. No formatter capability is advertised to
normal clocks by this module.
