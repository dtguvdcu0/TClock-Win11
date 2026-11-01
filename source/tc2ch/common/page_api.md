# Settings page substitution

Status: implemented for the Chime settings entry (Cycle 331). A plugin offers one of its
settings pages as the replacement of a built-in entry; the user selects it explicitly.
Include `page_api.h`. Example provider: SampleChime in the separate TClock-Plugin repository.

## Offering a page

Register the page as usual through `TC_HOST_REGISTRY.page` (`create`/`apply`, see
[plugin_api.md](plugin_api.md)). Then register an immutable `TC_PAGE_SUBSTITUTE` through
`TC_HOST_FEATURES.add` with interface `tclock.page/1`, `target` = `TC_PAGE_TARGET_CHIME`
and `page` = the id of that registered page. Feature ids share one namespace per plugin
with other features, so use a distinct id. A page named by a substitution is not listed
as an extra page; it appears only in place of its built-in entry.

## Selection and ownership

The Plugins page shows "Use as Chime settings page" for the selected plugin when it
offers a Chime page and carries the automatic discovery header (its provider ID is the
stored identity, see plugin_resources.h). Checking it and applying stores the plugin ID in the TClock INI
(`[Plugins] Page.chime`); clearing it stores an empty value. Only one plugin can own the
entry. The selection is resolved when the Chime entry is first opened in a settings
dialog and is kept until that dialog closes, so applying a different selection never
replaces a page that may hold edits: it takes effect the next time the dialog opens.

When the selected plugin is disabled, missing, stopped or does not offer the page, the
built-in Chime page is shown, its tree item says "plugin page unavailable", and the
stored selection is kept for the plugin's return. The built-in page and its `[Chime]`
settings are never changed by a selected plugin page; clearing the selection shows them
again unchanged.

## Apply and Cancel

On Apply the host calls the plugin page's `apply` in place of the built-in page's
validation and save. A failed result keeps the settings dialog open on the Chime entry
with the page and its edits, after a save error message; applying again retries. If the
plugin's page window no longer exists at Apply (the plugin was stopped), nothing is
saved for it and the entry is resolved again the next time it is opened. Cancel writes
nothing. Settings that the page owns belong in the plugin's private INI.
