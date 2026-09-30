# WinUI text renderer comparison

This INI switch selects the text renderer inside the existing WinUI-labelled clock host. Both DirectWrite modes are retained for user choice. The unchecked GDI backend keeps its existing drawing path; the WinUI host uses DirectWrite/Direct2D rather than XAML text controls.

In the active clock INI, use:

```ini
[Win11]
ExperimentalTextRenderer=1
```

| Value | Renderer |
| --- | --- |
| 0 | Existing GDI+ baseline (also used for invalid values) |
| 1 | DirectWrite natural layout, grayscale antialiasing, color emoji |
| 2 | DirectWrite GDI-compatible layout, grayscale antialiasing, color emoji |

Enable the existing WinUI rendering checkbox. When ExperimentalTextRenderer is missing, WinUI defaults to mode 1; the unchecked GDI backend retains its existing drawing path. Restart TClock after editing the INI so the setting is reloaded. Keep the same format, font, font size, and monitor scaling when comparing 1 and 2. Check small digits, Japanese text, adjacent custom fields, emoji, shadows, and borders. The existing caller's horizontal run positions and clock sizing are retained, so compare spacing as well as letter shapes. In both DirectWrite modes, mixed fonts and sizes align to the line's shared baseline.

Use Segoe UI Emoji for a field requiring emoji. Color glyphs retain their intrinsic palette; the foreground setting still colors ordinary monochrome glyphs. Shadow and border passes remain monochrome. DirectWrite failure uses the existing GDI+ fallback and emits an OutputDebugStringW diagnostic with the selected mode and HRESULT.

The setting name remains ExperimentalTextRenderer for compatibility. Modes 0, 1, and 2 remain available; explicit values are preserved, and the missing-key default is selected at runtime without writing the INI. Comparison artifacts are recorded in tasks/2026-09-27-renderer-comparison.
