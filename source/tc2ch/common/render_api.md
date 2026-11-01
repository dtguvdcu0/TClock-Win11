# Clock render feature

Status: implemented (Cycles 328-330). A selected `tclock.render/1` provider draws the
clock text content as pixels; the host keeps the background, graphs, bar meters and the
small analog clock and composes the provider image over them. Include `render_api.h`;
no helper source is needed. Example provider: SampleRender in the separate TClock-Plugin repository.

## Supported modes

| Mode | Status |
| --- | --- |
| GDI display backend, horizontal taskbar | Supported and verified (opaque and taskbar-matched alpha background, 96 DPI) |
| WinUI display backend, horizontal taskbar | Supported and verified (the image is composed into the layer WinUI presents) |
| Extended clock faces (analog, flip, nixie, LED) | Out of scope by user decision: built-in output stays, logged as "not applied, extended display mode" |
| Vertical taskbar | Implemented (TC_RENDER_VERTICAL, same composition), not verified; out of the verified scope by user decision (Windows 11 offers no vertical taskbar) |
| Secondary displays | Implemented through the existing mirror of the main clock image, not verified; out of the verified scope by user decision (one display available) |
| DPI other than 96 | The request carries the clock window DPI, not verified; out of the verified scope by user decision (96 DPI only) |

## Ownership and selection

Register an immutable `TC_RENDER_PROVIDER` through `TC_HOST_FEATURES.add` with
interface `tclock.render/1`. The host selects the highest priority registration, then
provider ID and feature ID. Plugins page Apply and a new clock window switch the route
immediately; otherwise selection is re-checked about every 10 seconds.

## Requests and output

For each displayed second, and immediately when the image size, DPI, colors, text or
taskbar orientation change, the clock publishes a request and TClock calls
`render(user, request, output)` on its UI thread with the newest request only. The
request carries the image `width` x `height` in pixels (the clock buffer, at most
1024 x 512), the clock window `dpi`, the configured text and background colors, the local
display time and the clock text produced by the built-in formatter or a selected
`tclock.formatter/2` provider (lines separated by CR LF).

Write exactly `width * height` premultiplied BGRA pixels (0xAARRGGBB with R, G, B not
above A), top row first, into the zero-filled host buffer. `preferredWidth` and
`preferredHeight` (both or neither, at most the maximum size) replace the built-in content
measurement while frames are current. Up to 8 `regions` (inside the image, zone 1-3) own
the Mouse page click zones of bindings with more than one zone: a click inside a region
uses its zone, a click outside every region does nothing. Return no regions to keep the
configured zone split. Any other result than `S_OK`, or an output that breaks these rules,
shows the built-in text.

## Display and fallback

The clock never waits for TClock. It composes an image only when it belongs to the current
route, has exactly the current buffer size and arrives within 2 seconds of its request;
older images are rejected. Otherwise, and when the provider is disabled or TClock exits,
the clock draws its built-in text and size again and repaints immediately, so no provider
pixels remain. `render` runs on the TClock UI thread: return promptly, never draw to a
window, never keep the request or the pixel buffer, and never call back into the host.
