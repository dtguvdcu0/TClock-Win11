# TClock DLL plugin contract

This document describes the current `plugin_api.h` contract and its implementation in `../exe/plugin_runtime.c`. The header defines the ABI; the runtime defines dispatch and ownership. Keep all three aligned when changing an interface. [SampleActions](../plugins/sample_actions/README.md) is the buildable command example. Page and color fragments below show registration only; they are not complete plugins.

## Identity and negotiation

Build a DLL for the host architecture. Export `HRESULT WINAPI TClockGetInterface(UINT abi, LPCWSTR id, const void** out)` using an undecorated export, as in the sample `.def`. Use the C ABI and WINAPI for every callback. Do not pass CRT-owned allocations or exceptions across this boundary. Paths and public text use UTF-16.

Return immutable, module-lifetime tables on `S_OK`. Initialize `*out` to NULL before rejecting an unsupported ABI or ID with `E_NOINTERFACE`; a NULL output argument returns `E_POINTER`. Always set structure `size` to `sizeof(structure)` and check incoming sizes before accessing fields.

| Interface | Purpose |
| --- | --- |
| `tclock.plugin/1` | Required stable provider ID and name |
| `tclock.runtime/1` | Preferred start/reload/stop lifecycle |
| `tclock.host.registry/1` | Host command/page/color registration and removal |
| `tclock.host.services/1` | Host clipboard copy, page dirty notification and logging |
| `tclock.host.features/1` | Register a typed feature table for an explicit host adapter |
| `tclock.settings/1` | Optional private modal settings UI, independent of runtime |
| `tclock.actions/1` | Legacy action catalog, used without runtime |
| `tclock.extension/1` | Legacy combined page/color adapter, used without runtime |

Use `context->query_host(context->plugin, TC_PLUGIN_ABI, interface_id, &table)` during the active lifecycle. The `plugin` value is an opaque owner identity; never dereference, manufacture or share it with another provider. Check both the HRESULT and returned table size before using its members.

A valid runtime interface is authoritative. The host does not also acquire its legacy action/extension interfaces, and does not fall back to them after a runtime failure. The optional settings interface is queried in either mode. Unsupported runtime may return `E_NOINTERFACE` to allow legacy negotiation; a malformed interface rejects the DLL.

Provider and registration IDs use 1 through 31 characters from `[a-z0-9._-]`. They are permanent, case-sensitive machine identities. Labels are nonempty and contain at most 127 UTF-16 code units. Command labels also pass through the host's bounded UTF-8 storage, so size validation can reject a label whose encoded bytes do not fit. Supply both Japanese and English labels. The host copies registration IDs and labels before registration returns.

The resource-only discovery marker must match the provider ID returned by `tclock.plugin/1`. Discovery does not execute the DLL. Duplicate provider identities cannot be acquired independently. New plugins start disabled; enabling does not insert menu assignments. The existing Plugins UI controls discovery and enablement.

## Version evolution

Freeze every existing `/1` structure layout, callback signature and meaning. A `size` member is validation, not permission to append fields under the same interface ID: current receivers require at least their compiled structure size, and the color IPC requires an exact `TC_TEXT_CONTEXT` size. An older sender cannot satisfy a larger receiver merely by keeping ABI 1.

Introduce a new versioned interface for a changed layout or semantic contract. Query the new ID first and explicitly fall back to the old ID when unsupported. Keep the old table/layout available when compatibility is required. Do not silently change packing, architecture assumptions, constants or existing ID meanings. No automatic compatibility with arbitrary future hosts is promised.

## Lifecycle and ownership

All existing `/1` runtime callbacks and registry operations belong on the host UI thread. Return promptly; color callbacks must not perform disk/network I/O, open modal UI or wait for work that needs this thread. The separately negotiated [formatter/2](formatter_api.md) feature explicitly runs only its pure `format` callback on a host worker; its `capture` and `release` callbacks remain on UI. This adds no worker permission to any `/1` callback or host service. Do not perform host calls from DllMain. In-process callbacks have no crash or hang isolation.

1. `start` receives `TC_PLUGIN_CONTEXT` and registers capabilities. `start` and `stop` are required; `reload` may be NULL. Only exact `S_OK` publishes a successful start. Partial registrations remain unavailable to ordinary dispatch until start succeeds. On failure the host removes registrations and calls `stop`; make stop safe after partial initialization.
2. `reload`, when supplied, refreshes the current context/configuration. It is called on a refresh when no page lease is held, rather than on every draw. Return exact `S_OK`; another result deactivates the provider and schedules cleanup. A NULL reload leaves current state intact. Avoid duplicate registration of existing active IDs on every reload.
3. Disabling prevents new dispatch. Existing callbacks and created pages delay stop. Re-enabling before deferred stop can resume the existing instance. A private INI path change waits for old leases, stops the old context and starts a new one.
4. `stop` cancels plugin-owned work and releases plugin resources. The host hides registrations before stop. During host shutdown all providers are deactivated before any stop callback; host-owned page windows are destroyed before final provider cleanup. Shutdown is idempotent. Accepted DLL modules remain loaded until process exit; disabling does not hot-unload them.

Treat context pointers, strings and callback arguments as borrowed for the call. Copy any path/configuration needed afterward into plugin-owned storage. Keep negotiated host tables and the copied owner/query values only for the active lifecycle; release references at stop. An HWND does not guarantee window lifetime: use the page HWND only while that page exists. Never retain stack `TC_EXTENSION_CONTEXT`, `TC_ACTION_CONTEXT` or `TC_TEXT_CONTEXT` addresses.

Registration callback pointers and `user` storage must remain valid through their final use. For commands/colors this includes an executing callback after removal. For pages it includes existing page WndProc and Apply calls until window destruction, even after removing the registration. Do not free page state merely because remove returned success. Plugin threads, if introduced, must not call UI-thread registry methods and must be stopped/joined before stop returns. The host calls stop synchronously on its runtime thread: a worker that waits for that thread (for example, by sending a synchronous window message) can deadlock the join. Cancellation and worker exit must not depend on runtime-thread message processing. An asynchronous notification alone does not transfer payload ownership or guarantee delivery after stop; do not post borrowed pointers or assume the owner window remains valid.

The host suppresses command/color dispatch into a provider already executing a managed callback. Modal UI can pump messages and cause lifecycle changes; do not rely on arbitrary reentrant host calls succeeding. Keep shared host recovery ownership in the host: HideClock policy restoration, Explorer restart/wait, startup/shutdown and native-clock/tray geometry are not plugin-owned effects.

## Registration and removal

Zero-initialize each descriptor, set size and required callbacks, then call the corresponding registry method with the current plugin owner and an output `TC_REGISTRATION`. The token is nonzero on success. Store it when explicit removal is needed. The host automatically hides the owner's registrations at stop.

| Operation/result | Meaning |
| --- | --- |
| Registration `S_OK` | Descriptor accepted; output token assigned |
| `E_POINTER` | Missing token output |
| `E_OUTOFMEMORY` | Host could not allocate registration storage |
| `E_ACCESSDENIED` | Invalid owner/thread or registration outside the accepted lifecycle |
| `E_INVALIDARG` | Invalid size, ID, label, mode or required callback |
| `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)` | Same owner/type/ID already active, or a page of that identity still exists |
| `HRESULT_FROM_WIN32(ERROR_TOO_MANY_NAMES)` | Session identity capacity exhausted |
| Remove `S_OK` | Matching owner token deactivated |
| Remove `E_INVALIDARG` | Invalid owner or zero token |
| Remove `HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE)` | Unknown, replaced or other-owner token |

Removal disables future creation/dispatch; it does not cancel the current call or destroy an existing page. Removing an already inactive matching token can still succeed. Registering an inactive identity again issues a new token, invalidating the previous token. IDs are scoped by owner and capability type; use stable IDs across restarts and reloads.

The host grows provider, command, page and color storage as needed, subject to memory and respective index limits; accepted DLLs and allocated registration blocks remain until process exit. Page registration returns E_OUTOFMEMORY when storage cannot be allocated. If the settings tree cannot show every page, it offers a retry or asks the user to reopen settings. Inactive command and color slots can be reused by other identities. An inactive page slot can be reused once it has no live page window or in-flight creation callback; stale rows without an existing created page are rejected until refreshed. Use stable IDs across refreshes.

### Commands

Register `TC_COMMAND_REGISTRATION` with `id`, both labels, `user` and `invoke`. The saved operation key is `dll:<provider-id>/<command-id>`. Return the operation HRESULT; do not change an ID when translating its label or moving it within a catalog. `TC_ACTION_CONTEXT.copy_text` copies the supplied text before returning, so a local string buffer is sufficient. See `sample_start`, `sample_run` and `sample_invoke` for a complete example with legacy fallback exports.

### Property pages

Register each page independently; one DLL can supply several pages. Set `create` and `apply`. For example, inside start after negotiating `registry`:

```c
TC_PAGE_REGISTRATION page = { sizeof(page) };
TC_REGISTRATION page_token;
page.id = L"appearance";
page.japanese = L"Appearance"; /* Supply a Japanese label in a real plugin. */
page.english = L"Appearance";
page.user = &plugin_state;
page.create = create_page;
page.apply = apply_page;
return registry->page(context->plugin, &page, &page_token);
```

`create(void* user, const TC_EXTENSION_CONTEXT* context)` returns a live child HWND whose parent is `context->owner`, or NULL on failure. The host tracks one window per page identity, subclasses it for a lifetime lease and owns its destruction. Keep plugin state until `WM_NCDESTROY`; returning an invalid window or a different parent is rejected. Copy private paths into page state and call `context->changed(page_hwnd)` when edits make Apply necessary.

`apply(void* user, HWND page)` saves validated page settings. A failed HRESULT prevents successful host Apply and reports a save error; non-failing results are accepted. Preserve pending edits on save failure so the user can retry. Avoid writes on page creation or Cancel. Page removal/disable does not invalidate an existing page's Apply lease.

`tclock.settings/1` is a separate modal Settings button contract: reload reads configuration, show opens an owner-modal dialog using the supplied private INI path, returning `S_OK` after saving, `S_FALSE` on Cancel or failure on error. Keep preferences in the plugin's private INI; do not overwrite the host INI or user data with templates.

### Color transforms and replacement

Register `TC_COLOR_REGISTRATION` with stable ID, priority, mode, user and query. For example:

```c
TC_COLOR_REGISTRATION color = { sizeof(color) };
TC_REGISTRATION color_token;
color.id = L"accent";
color.priority = 10;
color.mode = TC_COLOR_TRANSFORM;
color.user = &plugin_state;
color.query = query_color;
return registry->color(context->plugin, &color, &color_token);
```

The callback signature is `HRESULT WINAPI query_color(void* user, const TC_TEXT_CONTEXT* context, COLORREF* color)`. Use a cached configuration snapshot. Text kinds currently include ordinary text (1), `TC_TEXT_DATE` (2), `TC_TEXT_WEEKDAY` (4), `TC_TEXT_TIME` (8) and `TC_TEXT_ACCENT` (88); treat these as category values, not a bitmask. Preserve or decline unknown kinds. The context also supplies time, holiday and VPN state.

Providers run by descending numeric priority, then ascending case-sensitive provider ID, then registration ID. Each callback receives the current accumulated color in both `context->base` and `*color`. Exact `S_OK` accepts the output. `S_FALSE` or another non-failing result declines without changing it; failure is logged and also preserves the previous color. A successful `TC_COLOR_TRANSFORM` continues; a successful `TC_COLOR_REPLACE` ends the chain. A declined/failed replacement allows later providers to run. With no accepted output the original base survives. If the host cannot allocate a complete callback snapshot, it invokes no callbacks and the drawing side falls back to its base color. Only RGB bytes are returned by the IPC.

The drawing side caches responses by kind for roughly one second and sends cache misses synchronously with a 50 ms sender timeout. A timeout does not cancel an executing callback. This API ordering describes color selection; independent per-kind colors on every rendering/background/style path still require renderer conformance tests. The host preserves differing category colors through the existing per-run renderer even without a matching format rule. Native GDI pixel tests cover transparent/opaque output, styles, shadows/borders and three font rasterization modes; full WinUI/taskbar presentation and arbitrary providers require their own integration evidence. Registration success alone does not verify visible output.

Rendering fallback and repaint are defined by the render feature ([render_api.md](render_api.md)): when a provider frame is missing, late or invalid, or the route ends, the clock redraws and re-measures its built-in output for both opaque and transparent backgrounds.

## Host cross-process transport

`plugin_transport.h` is a host-internal bounded `WM_COPYDATA` helper used by the existing color bridge. `TPI_NOT_SENT` means local argument validation rejected the request before sending, `TPI_REPLIED` means the window procedure returned a scalar result, and `TPI_UNKNOWN` means no reply was obtained: it does not prove whether the receiver ran. The helper does not retry or cancel work. Each domain must define its own pointer-free, bounded and versioned packet, validate its contents and establish sender/owner identity as needed. A successful window-message result is not an acknowledgement of arbitrary application semantics. `WM_COPYDATA` is not authentication, a variable-size reply mechanism or a guarantee of exactly-once effects. A domain with side effects must define acknowledgement, idempotence, timeout recovery and fallback independently.

Provider DLLs load only into the TClock executable; the clock DLL inside Explorer never loads one. Each feature domain uses its own host-private bridge between the executable and the clock (`format_bridge.h`, `chime_bridge.h`, `render_bridge.h`): the executable creates the named section and owns the route generation, the clock posts notifications and never waits for the executable, and replies use `WM_COPYDATA` with a bounded send timeout. A new generation on Plugins Apply, owner change, clock attach or shutdown invalidates older results, and the clock falls back to its built-in behavior when a result is missing, late or invalid. These bridges are not a plugin interface.

The clock startup still passes window handles through shared DLL data and a `WM_USER` announcement without validating the sender. The feature bridges bind their peer themselves: section names carry the executable's process ID, the clock checks the window that sends each reply, and a new generation on clock attach, replacement or shutdown invalidates older results.

## Typed feature foundation

The optional `tclock.host.features/1` table registers `TC_FEATURE_REGISTRATION` entries without changing older registry layouts. `id` is a stable registration ID; `interface_id` is an ASCII lowercase versioned ID such as `example.policy/1` (maximum 95 UTF-16 units; the suffix after the only slash contains decimal digits). `table` points to an immutable domain-specific C table, and `user` is the plugin-owned callback context. Both must remain valid until all invocations have returned and stop has finished. IDs are copied; use the existing registry `remove` method and returned token for explicit removal.

A host adapter must explicitly support the interface ID, validate the table size and callbacks and define the domain's input, result and side-effect rules. The foundation does not add an existing feature hook or execute unknown tables by itself. Host-side `prt_call_feature` selects one enabled registration by descending priority, then ordinal provider ID and registration ID. It invokes a type-specific adapter while the provider is busy. A busy selected owner returns `ERROR_BUSY`; it does not switch to a different provider. `invoked` distinguishes a missing owner or pre-dispatch error from an adapter HRESULT, including `E_NOINTERFACE`. The adapter/domain decides whether fallback is safe after an invocation; automatic retries can duplicate side effects. Selection and dispatch use the host UI thread and do not provide process isolation or cross-process transport.

Registration follows the existing provider start publication, disable, remove and shutdown lifecycle. Remove prevents later invocations but cannot cancel an active one; the host holds the provider busy until the adapter returns. Re-registering the same owner/ID yields a fresh token. Feature registrations use dynamically allocated storage; inactive slots can be reused by another identity with a fresh token. Registration returns E_OUTOFMEMORY when storage cannot be allocated, and an index limit remains. No format, drawing, scheduling, input or replacement adapter is implied by this foundation; each needs a separately versioned typed contract and explicit host integration.

## Formatter source representation

The versioned, pointer-free format_source.h body includes the original format, time basis, cached telemetry, Unicode labels/CUSTOM values and counted locale rows. Its complete maximum is 946696 bytes. See [format_source.md](format_source.md) for exact capacities, validation, refresh ownership and helper contracts.

The normal clock routes the selected `tclock.formatter/2` provider to the main text and the tooltip body/title; see [formatter_api.md](formatter_api.md). Existing /1 layouts and normal clock-DLL exports are unchanged.

## Domain support matrix

| Domain | Interface | Host consumer | Selection | Example | Fallback and limits |
| --- | --- | --- | --- | --- | --- |
| Commands | registry `command` (`tclock.runtime/1`); legacy `tclock.actions/1` table | right-click menu, mouse zones and clicks, hotkeys | explicit binding `-1\|dll:provider/command` | `plugins/sample_actions` | a disabled or missing provider leaves the binding inert; no other command or built-in operation runs |
| Property pages | registry `page` | extra pages of the settings dialog | provider enabled | SampleChime (TClock-Plugin) | page leases end with the dialog |
| Chime page substitution | `tclock.page/1` ([page_api.md](page_api.md)) | Chime entry of the settings dialog | explicit `[Plugins] Page.chime` | SampleChime (TClock-Plugin) | built-in page, marked unavailable, while the owner is missing |
| Text color | registry `color`; legacy `tclock.extension/1` | date, weekday, time and accent text | priority | none | built-in color after a decline, error or 50 ms timeout |
| Clock text | `tclock.formatter/2` ([formatter_api.md](formatter_api.md)) | main text, tooltip body and title | highest priority | SampleFormatter (TClock-Plugin) | built-in text while a result is missing, late or invalid; LED/preview text and menu labels stay built-in; `/1` is not routed |
| Chime decision | `tclock.chime/1` ([chime_api.md](chime_api.md)) | hourly and secondary chime decision and effects | highest priority | SampleChime (TClock-Plugin) | built-in decision when no plan is current |
| Clock rendering | `tclock.render/1` ([render_api.md](render_api.md)) | clock image, size and mouse hit regions (GDI and WinUI) | highest priority | SampleRender (TClock-Plugin) | built-in drawing; extended clock faces stay built-in; vertical taskbar, secondary displays and DPI other than 96 are implemented but not verified |

A provider disabled on the Plugins page or by an outside edit of the INI file is no longer called from its next dispatch. A feature route then hands over to the next provider: the chime route at the next one-second tick, the text and render routes within ten seconds with built-in output meanwhile. Build a provider from the public headers only with `source/build_plugin.bat project.vcxproj [Release|Debug]` (x64); `plugins/sample_actions/build.bat` calls it. The examples marked TClock-Plugin are maintained in the separate TClock-Plugin repository.

## Extension and validation checklist

Add a typed, versioned domain interface when a real feature needs a new host operation. Arbitrary mouse/hotkey interception, general drawing outside the render contract and event subscription are not exposed.

Before shipping a host/plugin pair, verify unsupported-interface fallback, failed partial start/stop, duplicate and stale registrations, disable/re-enable, reload, open-page shutdown and private-settings save failure. For color, verify multiple transforms/replacements, equal-priority ordering, declines/errors, slow callbacks and actual output across rendering modes. Include old DLLs when changing negotiation. Keep runtime evidence separate from compile/ABI checks and preserve live user settings during deployment.
