#include <windows.h>
#include <d3d11.h>
#include <d2d1_2.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <stdio.h>
#include "wui_text.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;
static ComPtr<ID2D1DeviceContext> g_wuiContext;
static ComPtr<IDWriteFactory> g_wuiWrite;
static ComPtr<ID2D1Bitmap1> g_wuiTargetBitmap;
static ComPtr<ID2D1Bitmap1> g_wuiReadBitmap;
static HRESULT g_wuiLastError = S_OK;

void wui_reset_text(void)
{
    if (g_wuiContext) g_wuiContext->SetTarget(NULL);
    g_wuiReadBitmap.Reset();
    g_wuiTargetBitmap.Reset();
    g_wuiContext.Reset();
    g_wuiWrite.Reset();
    g_wuiLastError = S_OK;
}

static HRESULT wui_create_text(UINT width, UINT height)
{
    HRESULT hr;
    if (!g_wuiContext) {
        ComPtr<ID3D11Device> device;
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<ID2D1Device> d2d;
        // WARP keeps the comparison independent of adapter/remote-session changes.
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
            device.GetAddressOf(), NULL, NULL);
        if (FAILED(hr)) return hr;
        hr = device.As(&dxgi);
        if (FAILED(hr)) return hr;
        hr = D2D1CreateDevice(dxgi.Get(), NULL, d2d.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, g_wuiContext.GetAddressOf());
        if (FAILED(hr)) return hr;
        g_wuiContext->SetDpi(96.0f, 96.0f); // Caller supplies physical pixel metrics.
        g_wuiContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
    if (!g_wuiWrite) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(g_wuiWrite.GetAddressOf()));
        if (FAILED(hr)) return hr;
    }
    if (g_wuiTargetBitmap) {
        D2D1_SIZE_U size = g_wuiTargetBitmap->GetPixelSize();
        if (size.width == width && size.height == height && g_wuiReadBitmap) return S_OK;
    }
    g_wuiContext->SetTarget(NULL);
    g_wuiTargetBitmap.Reset();
    g_wuiReadBitmap.Reset();
    D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    hr = g_wuiContext->CreateBitmap(D2D1::SizeU(width, height), NULL, 0, properties,
        g_wuiTargetBitmap.GetAddressOf());
    if (FAILED(hr)) return hr;
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    return g_wuiContext->CreateBitmap(D2D1::SizeU(width, height), NULL, 0, properties,
        g_wuiReadBitmap.GetAddressOf());
}

static D2D1_COLOR_F wui_get_color(COLORREF color)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
        GetBValue(color) / 255.0f, 1.0f);
}

static HRESULT wui_make_layout(const WCHAR* text, UINT length,
    const TC_DISPLAY_BACKEND_STYLE& style, DWORD renderer, FLOAT width,
    IDWriteTextLayout** result)
{
    ComPtr<IDWriteTextFormat> format;
    FLOAT size = (FLOAT)abs(style.fontHeight);
    if (size <= 0.0f) size = 12.0f;
    // Property choices are GDI face names, including weight/stretch aliases.
    // Resolve the same selection through GDI interop before creating a DirectWrite layout.
    LOGFONTW logical = {};
    logical.lfHeight = style.fontHeight;
    logical.lfWeight = style.fontWeight > 0 ? style.fontWeight : FW_NORMAL;
    logical.lfItalic = style.fontItalic;
    logical.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(logical.lfFaceName, style.fontFace[0] ? style.fontFace : L"Segoe UI", LF_FACESIZE);
    ComPtr<IDWriteGdiInterop> interop;
    ComPtr<IDWriteFont> selected;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteLocalizedStrings> names;
    WCHAR familyName[256];
    HRESULT hr = g_wuiWrite->GetGdiInterop(interop.GetAddressOf());
    if (SUCCEEDED(hr)) hr = interop->CreateFontFromLOGFONT(&logical, selected.GetAddressOf());
    if (SUCCEEDED(hr)) hr = selected->GetFontFamily(family.GetAddressOf());
    if (SUCCEEDED(hr)) hr = family->GetFamilyNames(names.GetAddressOf());
    if (SUCCEEDED(hr)) hr = names->GetString(0, familyName, _countof(familyName));
    if (FAILED(hr)) return hr;
    hr = g_wuiWrite->CreateTextFormat(familyName, NULL, selected->GetWeight(),
        selected->GetStyle(), selected->GetStretch(), size, L"ja-jp", format.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(hr)) return hr;
    if (renderer == 2) {
        return g_wuiWrite->CreateGdiCompatibleTextLayout(text, length, format.Get(),
            width, 4096.0f, 1.0f, NULL, FALSE, result);
    }
    return g_wuiWrite->CreateTextLayout(text, length, format.Get(), width, 4096.0f, result);
}

static HRESULT wui_paint_layout(IDWriteTextLayout* layout, D2D1_POINT_2F origin,
    const TC_DISPLAY_BACKEND_STYLE& style)
{
    ComPtr<ID2D1SolidColorBrush> foreground;
    ComPtr<ID2D1SolidColorBrush> shadow;
    HRESULT hr = g_wuiContext->CreateSolidColorBrush(wui_get_color(style.textColor), foreground.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = g_wuiContext->CreateSolidColorBrush(wui_get_color(style.shadowColor), shadow.GetAddressOf());
    if (FAILED(hr)) return hr;
    // Shadow/border intentionally use monochrome outlines, not the emoji palette.
    if (style.clockShadow) {
        g_wuiContext->DrawTextLayout(D2D1::Point2F(origin.x + style.shadowRange,
            origin.y + style.shadowRange), layout, shadow.Get());
    }
    if (style.clockBorder) {
        static const int offsets[][2] = { {-1, 1}, {1, -1}, {1, 1}, {0, -1}, {1, 0}, {-1, -1} };
        for (const auto& offset : offsets) {
            g_wuiContext->DrawTextLayout(D2D1::Point2F(origin.x + offset[0], origin.y + offset[1]),
                layout, shadow.Get());
        }
    }
    g_wuiContext->DrawTextLayout(origin, layout, foreground.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    return S_OK;
}

static HRESULT wui_paint_text(const TC_DISPLAY_BACKEND_RENDER_STATE& state, const RECT& content)
{
    FLOAT width = (FLOAT)(content.right - content.left + 32);
    if (width <= 0.0f) return E_INVALIDARG;
    if (state.styleVersion == TC_WUI_STYLE_VERSION && state.styleCount && state.runCount) {
        if (state.styleCount > TC_WUI_MAX_STYLES || state.runCount > TC_WUI_MAX_RUNS) return E_INVALIDARG;
        for (DWORD i = 0; i < state.runCount; ++i) {
            const TC_DISPLAY_BACKEND_RUN& run = state.runs[i];
            if (run.styleIndex >= state.styleCount || run.textStart < 0 || run.textLength <= 0 ||
                run.textStart >= (LONG)_countof(state.text) ||
                run.textLength >= (LONG)_countof(state.text) - run.textStart) return E_INVALIDARG;
            ComPtr<IDWriteTextLayout> layout;
            const TC_DISPLAY_BACKEND_STYLE& style = state.styles[run.styleIndex];
            HRESULT hr = wui_make_layout(state.text + run.textStart, (UINT)run.textLength, style,
                state.textRenderer, width, layout.GetAddressOf());
            if (FAILED(hr)) return hr;
            FLOAT y = (FLOAT)run.y;
            if (state.cb >= sizeof(state) && state.baselineVersion == TC_WUI_BASELINE_VERSION) {
                DWRITE_LINE_METRICS metrics = {};
                UINT32 lineCount = 0;
                hr = layout->GetLineMetrics(&metrics, 1, &lineCount);
                if (FAILED(hr) || lineCount != 1) return FAILED(hr) ? hr : E_INVALIDARG;
                // GDI top coordinates are not DirectWrite layout tops. Keep the
                // caller's common baseline, including font fallback and size changes.
                y = (FLOAT)state.runBaselines[i] - metrics.baseline;
            }
            hr = wui_paint_layout(layout.Get(), D2D1::Point2F((FLOAT)(content.left + run.x), y), style);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    }
    TC_DISPLAY_BACKEND_STYLE style = {};
    style.textColor = state.textColor;
    style.shadowColor = state.shadowColor;
    style.shadowRange = state.shadowRange;
    style.fontHeight = state.fontHeight;
    if (!style.fontHeight) style.fontHeight = content.bottom - content.top - 2;
    style.fontWeight = state.fontWeight;
    style.fontItalic = state.fontItalic;
    style.clockShadow = state.clockShadow;
    style.clockBorder = state.clockBorder;
    lstrcpynW(style.fontFace, state.fontFace, LF_FACESIZE);
    WCHAR buffer[_countof(state.text)];
    lstrcpynW(buffer, state.text, _countof(buffer));
    WCHAR* cursor = NULL;
    WCHAR* line = wcstok_s(buffer, L"\r\n", &cursor);
    int lines = 0;
    while (line) { ++lines; line = wcstok_s(NULL, L"\r\n", &cursor); }
    int pixelHeight = abs(style.fontHeight);
    int step = pixelHeight + state.lineHeight;
    if (step <= 0) step = pixelHeight;
    FLOAT y = (FLOAT)((content.bottom - content.top - pixelHeight - (max(lines, 1) - 1) * step) / 2 + state.vertPos);
    lstrcpynW(buffer, state.text, _countof(buffer));
    line = wcstok_s(buffer, L"\r\n", &cursor);
    while (line) {
        ComPtr<IDWriteTextLayout> layout;
        HRESULT hr = wui_make_layout(line, (UINT)wcslen(line), style, state.textRenderer, width, layout.GetAddressOf());
        if (FAILED(hr)) return hr;
        DWRITE_TEXT_METRICS metrics = {};
        hr = layout->GetMetrics(&metrics);
        if (FAILED(hr)) return hr;
        FLOAT x = 0.0f;
        FLOAT available = (FLOAT)(content.right - content.left);
        if (state.textPos == 2) x = available - metrics.widthIncludingTrailingWhitespace;
        else if (state.textPos != 1) x = (available - metrics.widthIncludingTrailingWhitespace) / 2.0f;
        hr = wui_paint_layout(layout.Get(), D2D1::Point2F(content.left + x, y), style);
        if (FAILED(hr)) return hr;
        y += step;
        line = wcstok_s(NULL, L"\r\n", &cursor);
    }
    return S_OK;
}

BOOL wui_render_text(const TC_DISPLAY_BACKEND_RENDER_STATE& state, const RECT& content,
    LONG width, LONG height, BYTE* pixels)
{
    if (!pixels || width <= 0 || height <= 0 || state.textRenderer < 1 || state.textRenderer > 2) return FALSE;
    HRESULT hr = wui_create_text((UINT)width, (UINT)height);
    if (SUCCEEDED(hr)) {
        g_wuiContext->SetTarget(g_wuiTargetBitmap.Get());
        g_wuiContext->BeginDraw();
        // Preserve the host's nearly transparent input surface outside the text.
        g_wuiContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f / 255.0f));
        hr = wui_paint_text(state, content);
        HRESULT endResult = g_wuiContext->EndDraw();
        g_wuiContext->SetTarget(NULL);
        if (SUCCEEDED(hr)) hr = endResult;
    }
    if (SUCCEEDED(hr)) hr = g_wuiReadBitmap->CopyFromBitmap(NULL, g_wuiTargetBitmap.Get(), NULL);
    if (SUCCEEDED(hr)) {
        D2D1_MAPPED_RECT mapped = {};
        hr = g_wuiReadBitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
        if (SUCCEEDED(hr)) {
            for (LONG y = 0; y < height; ++y) {
                memcpy(pixels + (size_t)y * width * 4u, mapped.bits + (size_t)y * mapped.pitch, (size_t)width * 4u);
            }
            g_wuiReadBitmap->Unmap();
        }
    }
    if (FAILED(hr)) {
        HRESULT previous = g_wuiLastError;
        wui_reset_text();
        g_wuiLastError = hr;
        if (hr != previous) {
            WCHAR message[160];
            swprintf_s(message, L"[TClock][TextRenderer] mode=%lu HRESULT=0x%08lX; using GDI+ fallback.\n",
                state.textRenderer, (unsigned long)hr);
            OutputDebugStringW(message);
        }
        return FALSE;
    }
    g_wuiLastError = S_OK;
    return TRUE;
}
