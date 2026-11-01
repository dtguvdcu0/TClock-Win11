# Chime decision feature

Status: implemented for the clock chime (Cycle 327). A selected `tclock.chime/1`
provider decides when the clock chimes; the host still plays the configured sounds
and blinks. Include `chime_api.h`; no helper source is needed. Example provider:
SampleChime in the separate TClock-Plugin repository.

## Ownership and selection

Register an immutable `TC_CHIME_PROVIDER` through `TC_HOST_FEATURES.add` with
interface `tclock.chime/1`. The host selects the highest priority registration, then
provider ID and feature ID, like other features. Only the selected provider is called.
While its plan is current, the built-in decision is suppressed entirely: primary and
secondary chimes, blink-on-chime, the hour range, weekdays and offsets. The Chime page
pause (chime disabled with Paused set) still skips events. No built-in event runs in
addition to the plan.

## Plans

About once a minute, and right away when the selection, the provider registration or
the clock window changes, the host calls `plan(user, request, output)` on its UI thread.
The request covers `[from, from + windowSeconds)` (currently 300 seconds) in local wall
clock time. Return at most 32 events, each with:

- a nonzero `id`, unique in the plan, that names one decision;
- a local `time` inside the window (seconds; milliseconds are ignored);
- `effects`: any of `TC_CHIME_PRIMARY`, `TC_CHIME_SECONDARY`, `TC_CHIME_BLINK`;
- `strikes`: 1-24 with `TC_CHIME_PRIMARY`, otherwise 0.

A failed call or an invalid plan accepts nothing; the previous plan stays until its
window ends. A new plan replaces the pending events of the previous one: events it no
longer contains are cancelled if they have not started. The plan stops owning the
decision when its window ends without renewal (for example when TClock exits), and the
built-in decision returns.

## Execution and outcomes

The clock executes the plan itself and never waits for the exe. An event starts at its
time, or up to 2 seconds later if the clock tick was late; otherwise it is skipped.
Primary strikes play one per second. An event that becomes due while another is still
striking is skipped. Once an event started, the host does not accept its id from the
same registration again while it remembers it (the last 128 settled events).

`report(user, id, outcome)` is called at most once for every accepted event:

| Outcome | Meaning |
| --- | --- |
| `TC_CHIME_COMPLETED` | All effects were issued |
| `TC_CHIME_UNKNOWN` | Started, but completion was not observed (route ended mid-strike, clock lost, pause); never replayed |
| `TC_CHIME_SKIPPED` | Not started: its time was missed, another event was striking, or chime is paused |
| `TC_CHIME_CANCELLED` | Not started: dropped by a later plan, or the route ended (owner change, disable, shutdown) first |

A cancelled event may be planned again with the same or a new id. Reports are delivered
only while the registration that planned the event is still the selected owner: after it
is disabled, stopped or replaced by a higher priority provider, its remaining reports are
dropped. After about 65,000 accepted events the route starts a new generation: events not
yet started are reported cancelled and requested again, one striking is reported unknown. A report that finds the
provider busy (inside one of its own callbacks) is retried about once a second until it
is delivered or the route ends; the event itself is never executed again. Both callbacks run on
the host UI thread, must return promptly and must not call back into the host.
