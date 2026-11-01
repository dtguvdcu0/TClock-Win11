# SampleActions: a small DLL plugin

This sample has one operation: **Copy ISO timestamp (DLL)** copies local time as `YYYY-MM-DDTHH:mm:ss`, without a timezone suffix. The Plugins page has the only enable switch. This sample has no per-action checkbox, private preferences or settings dialog.

## Try it

1. Run `build.bat` in this directory (Release x64; `build.bat Debug` also supports Debug), or build the TClock solution. Deploy `source/x64/Release/plugin-build/SampleActions.dll` to `source/x64/Release/plugins/SampleActions.dll`, preserving existing binaries/private INIs and comparing hashes. Replacing a loaded DLL requires restarting TClock.
2. Open Properties > Plugins. The DLL is discovered automatically through its invariant resource header. No Add or registration editor is needed. New plugins start disabled; enable the plugin and Apply to make its builtin operation available.
3. Settings is disabled because this sample has no individual settings. Plugin enablement alone controls its builtin operation.
4. In Right Click Menu, assign a builtin item to the timestamp operation. Enabling never inserts menu entries automatically. Hiding/disabling keeps assignments.

The page contains the automatic inventory, selected enablement, Settings and description. Folder changes are checked while visible and when opened. There are no Add/Unregister/Open/Refresh buttons, Name/File detail fields or Apply guidance. Known tools and existing explicit registrations remain compatible. Auto-discovered DLLs removed from disk disappear from the list while saved preferences remain for their return.

SampleActions no longer reads or writes SampleActions.ini or [Commands] CopyIsoTime. Existing files are preserved. Only TClock Plugins enablement controls availability. Other plugins may still implement tclock.settings/1 and own their UI/private INI; that host capability is unchanged.

## Source map

| File | Responsibility |
| --- | --- |
| `sample_actions.c` | Plugin identity, action count/description/execution and interface dispatch |
| `sample_actions.rc` | Description, discovery marker and no-settings hint |
| `sample_actions.def` | Public TClockGetInterface export |
| `../../common/plugin_api.h` | Host/plugin C ABI shared contract |

The sample needs no companion EXE, private INI or custom DllMain. The action formats time itself and calls the borrowed host clipboard callback, which copies the text before returning.

## Capability contract

The current sample uses `tclock.runtime/1`. Read the [plugin API contract](../../common/plugin_api.md) for lifecycle, ownership, registration, ordering and versioning rules. `sample_start` queries `tclock.host.registry/1` and registers each action from the existing count/describe catalog. `sample_run` passes the stable action ID to `sample_invoke`; `sample_stop` has no resources to release. The host copies IDs/labels during registration, while the `user` pointer refers to static `sample_ids` storage that stays valid until stop.

`TClockGetInterface` also exposes `tclock.actions/1` so older hosts can use the same catalog. Current hosts select runtime registration when that interface is present and do not enumerate legacy actions or extensions as well. A failed runtime start does not fall back to the legacy interface. The optional `tclock.settings/1` remains available for a separate plugin-owned modal settings dialog; this sample does not implement it.

The permanent timestamp key stays `dll:sample.actions/copy-iso-time`. IDs retain their meaning across ordering and translation changes. The host retains accepted DLL modules until process exit. Replacing a loaded DLL requires a host restart. Minimal/Safe mode excludes third-party providers.

## Extend the example

For another action, add a permanent ID and its description/invocation in sample_actions.c. If individual preferences become necessary, implement the optional TC_PLUGIN_SETTINGS interface and a DLL-owned dialog/private INI, then remove the none settings hint. Keep settings local to the plugin. For a different capability, such as formatting, define another versioned interface and implement its host integration. Neither arbitrary third-party mouse/hotkey actions nor format providers are implemented by this sample.

## Embedded description

`../../common/plugin_resources.h` is a resource-only public contract, usable by RC and C. SampleActions embeds a brief Japanese/English description at RT_STRING ID TC_PLUGIN_DESCRIPTION_ID. TClock reads it as data even while integration is disabled; it never calls TClockGetInterface or DllMain to obtain this text. No action/settings ABI change is needed.

The Plugins page shows the resource display name in its inventory and Description in a scrollable read-only area. Existing arguments/private INI metadata remain preserved internally without editing controls. Plugin-specific preferences belong in the DLL-owned Settings dialog and private INI.

## Automatic discovery header

The invariant LANG_NEUTRAL/SUBLANG_NEUTRAL STRINGTABLE contains:

```rc
TC_PLUGIN_HEADER_ID "tclock.plugin/1:sample.actions"
TC_PLUGIN_NAME_ID "SampleActions"
TC_PLUGIN_SETTINGS_HINT_ID "none"
```

The permanent provider ID must match tclock.plugin/1 runtime metadata. The static marker allows discovery without DllMain/interface execution. Unsupported headers, malformed IDs and ordinary dependency DLLs are ignored. Optional localized names/descriptions use resource-only reads. New registration IDs use `auto.<provider-id>`, independent of filename/order. Existing IDs are retained for matching registered paths. Duplicate provider IDs in the folder are conflicts and cannot execute or open Settings.

The optional invariant settings hint none disables the host Settings button without executing a DLL. Missing/unknown hints preserve legacy behavior. This hint does not change the action/settings ABI.
