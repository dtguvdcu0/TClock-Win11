#include "card_document.h"
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl.h>
#include <new>
#include <strsafe.h>
#include <string>
#include <cwchar>
#include "card_api.h"
#include "card_theme.h"
#include "card_language.h"
#include "card_editor.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

struct TCARD_WUI_HOST__ {
    HWND window = nullptr;
    TCARD_WUI_STATE state{};
    std::wstring titleText;
    std::wstring textText;
    std::wstring sourceText;
    std::wstring assetRoot;
    std::wstring fontFamilyText = L"Segoe UI";
    float fontSize = 14.0f;
    bool markdown = false;
    TCARD_WUI_COMMAND_CALLBACK callback = nullptr;
    void* callbackContext = nullptr;
    TCARD_WUI_SAVE_CALLBACK saveCallback = nullptr;
    void* saveCallbackContext = nullptr;
    bool topmost = false;
    bool editing = false;
    bool editDirty = false;
    HWND titleEditor = nullptr;
    HWND sourceEditor = nullptr;
    HWND saveButton = nullptr;
    HWND cancelButton = nullptr;
    HWND appearanceButton = nullptr;
    HWND fontFamilyEditor = nullptr;
    HWND fontSizeEditor = nullptr;
    HWND closeButton = nullptr;
    bool appearanceOpen = false;
    HWND readEditor = nullptr;
    HFONT readFont = nullptr;
    HFONT uiFont = nullptr;
    HFONT buttonFont = nullptr;
    HBRUSH editorBrush = nullptr;
    HBRUSH readBrush = nullptr;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> backBrush;
    ComPtr<ID2D1SolidColorBrush> edgeBrush;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    ComPtr<IDWriteTextFormat> titleFormat;
    ComPtr<IDWriteTextFormat> bodyFormat;
};

static constexpr size_t kMaxTextUnits = 4 * 1024 * 1024;
static HINSTANCE g_instance = nullptr;
static HMODULE g_rich_edit = nullptr;
static ComPtr<ID2D1Factory> g_d2dFactory;
static ComPtr<IDWriteFactory> g_writeFactory;
static TCARD_WUI_HOST g_legacy = nullptr;

const wchar_t* tcard_text(const wchar_t* key, const wchar_t* fallback)
{
    static thread_local std::wstring value;
    value = tcard_lang::text(key, fallback);
    return value.c_str();
}
constexpr int kEditTitle = 5001;
constexpr int kEditSource = 5002;
constexpr int kEditSave = 5003;
constexpr int kEditCancel = 5004;
constexpr int kEditClose = 5005;
constexpr int kEditAppearance = 5006;
constexpr int kEditFontFamily = 5007;
constexpr int kEditFontSize = 5008;
constexpr int kEditMarkdown = 5010;
constexpr int kReadBody = 5009;

static D2D1_COLOR_F card_color(COLORREF color)
{
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f,
        GetBValue(color) / 255.0f, 1.0f);
}

static D2D1_COLOR_F card_edge(COLORREF color)
{
    return D2D1::ColorF(GetRValue(color) * 0.94f / 255.0f, GetGValue(color) * 0.94f / 255.0f,
        GetBValue(color) * 0.94f / 255.0f, 1.0f);
}

static void release_render(TCARD_WUI_HOST host)
{
    host->target.Reset();
    host->backBrush.Reset();
    host->edgeBrush.Reset();
    host->textBrush.Reset();
    host->titleFormat.Reset();
    host->bodyFormat.Reset();
}

static float card_font_size(const TCARD_WUI_HOST host)
{
    return host && host->fontSize >= 8.0f && host->fontSize <= 48.0f ? host->fontSize : 14.0f;
}

static int CALLBACK collect_font_family(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM parameter)
{
    HWND combo = reinterpret_cast<HWND>(parameter);
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, 0, reinterpret_cast<LPARAM>(font->lfFaceName)) == CB_ERR)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font->lfFaceName));
    return 1;
}

static void populate_font_families(HWND combo, HWND owner)
{
    LOGFONTW logFont{};
    logFont.lfCharSet = DEFAULT_CHARSET;
    HDC dc = GetDC(owner);
    if (dc) {
        EnumFontFamiliesExW(dc, &logFont, collect_font_family, reinterpret_cast<LPARAM>(combo), 0);
        ReleaseDC(owner, dc);
    }
    const wchar_t* fallbackFonts[] = { L"Segoe UI", L"Arial", L"Consolas" };
    for (const wchar_t* fallback : fallbackFonts) {
        if (SendMessageW(combo, CB_FINDSTRINGEXACT, 0, reinterpret_cast<LPARAM>(fallback)) == CB_ERR)
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(fallback));
    }
}

static int title_height_dip(TCARD_WUI_HOST host);

static void update_read_font(TCARD_WUI_HOST host)
{
    if (!host || !host->readEditor) return;
    if (host->readFont) { DeleteObject(host->readFont); host->readFont = nullptr; }
    const UINT dpi = GetDpiForWindow(host->window);
    const int height = -MulDiv(static_cast<int>(card_font_size(host) + 0.5f), static_cast<int>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI), USER_DEFAULT_SCREEN_DPI);
    const wchar_t* family = host->fontFamilyText.empty() ? L"Segoe UI" : host->fontFamilyText.c_str();
    host->readFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
    if (host->readFont) SendMessageW(host->readEditor, WM_SETFONT, reinterpret_cast<WPARAM>(host->readFont), TRUE);
    host->titleFormat.Reset();
    host->bodyFormat.Reset();
}

static BOOL create_render(TCARD_WUI_HOST host)
{
    RECT rc{};
    if (!GetClientRect(host->window, &rc)) return FALSE;
    if (!g_d2dFactory && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2dFactory.GetAddressOf()))) return FALSE;
    if (!g_writeFactory && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf())))) return FALSE;
    if (!host->target) {
        const auto size = D2D1::SizeU(static_cast<UINT32>(max(1L, rc.right - rc.left)), static_cast<UINT32>(max(1L, rc.bottom - rc.top)));
        if (FAILED(g_d2dFactory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(host->window, size), &host->target))) return FALSE;
    }
    if (FAILED(host->target->CreateSolidColorBrush(card_color(host->state.backColor), &host->backBrush))) return FALSE;
    if (FAILED(host->target->CreateSolidColorBrush(card_edge(host->state.backColor), &host->edgeBrush))) return FALSE;
    if (FAILED(host->target->CreateSolidColorBrush(card_color(host->state.textColor), &host->textBrush))) return FALSE;
    const wchar_t* family = host->fontFamilyText.empty() ? L"Segoe UI" : host->fontFamilyText.c_str();
    const float bodySize = card_font_size(host);
    if (!host->titleFormat && FAILED(g_writeFactory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, bodySize * 1.6f, L"", &host->titleFormat))) return FALSE;
    if (!host->bodyFormat && FAILED(g_writeFactory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, bodySize, L"", &host->bodyFormat))) return FALSE;
    host->titleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    host->bodyFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    return TRUE;
}

static void paint_card(TCARD_WUI_HOST host)
{
    if (!create_render(host)) return;
    const D2D1_SIZE_F size = host->target->GetSize();
    const float inset = static_cast<float>(tcard_ui::kPaperInsetDip);
    const float header = static_cast<float>(tcard_ui::kPaperHeaderDip);
    const float titleTop = header + 8.0f;
    const float titleHeight = static_cast<float>(title_height_dip(host));
    host->target->BeginDraw();
    host->target->Clear(card_color(host->state.backColor));
    host->target->FillRectangle(D2D1::RectF(0, 0, size.width, header), host->edgeBrush.Get());
    host->target->DrawRectangle(D2D1::RectF(0.5f, 0.5f, max(1.0f, size.width - 0.5f), max(1.0f, size.height - 0.5f)), host->edgeBrush.Get(), 1.0f);
    if (!host->editing && !host->titleText.empty()) {
        host->target->DrawTextW(host->titleText.c_str(), static_cast<UINT32>(host->titleText.size()), host->titleFormat.Get(), D2D1::RectF(inset, titleTop, max(inset + 1.0f, size.width - inset), titleTop + titleHeight), host->textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    if (host->target->EndDraw() == D2DERR_RECREATE_TARGET) release_render(host);
}

static std::wstring read_window_text(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) return {};
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, value.data(), length + 1);
    value.resize(static_cast<size_t>(copied));
    return value;
}

static void update_vertical_scroll(HWND control)
{
    tcard_ui::update_scroll(control);
}

static void sync_read_text(TCARD_WUI_HOST host)
{
    if (!host || !host->readEditor) return;
    tcard_md::render(host->readEditor, host->textText, host->markdown, host->fontSize, host->fontFamilyText, host->assetRoot, host->state.backColor);
    update_vertical_scroll(host->readEditor);
}

static void update_read_brush(TCARD_WUI_HOST host)
{
    if (!host) return;
    if (host->readBrush) DeleteObject(host->readBrush);
    host->readBrush = CreateSolidBrush(host->state.backColor);
    if (host->readEditor && g_rich_edit) SendMessageW(host->readEditor, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(host->state.backColor));
}

static int title_height_dip(TCARD_WUI_HOST host)
{
    const int height = static_cast<int>(card_font_size(host) * 1.6f * 1.35f + 0.5f);
    return max(24, height + 4);
}

static void layout_read_editor(TCARD_WUI_HOST host, int width, int height)
{
    if (!host || !host->readEditor) return;
    const int bodyTop = host->titleText.empty() ? tcard_ui::kPaperHeaderDip + 8 : tcard_ui::kPaperHeaderDip + 8 + title_height_dip(host) + 8;
    MoveWindow(host->readEditor, tcard_ui::kPaperInsetDip, bodyTop,
        max(120, width - tcard_ui::kPaperInsetDip * 2),
        max(1, height - bodyTop - tcard_ui::kPaperInsetDip), TRUE);
}

static void sync_legacy_state(TCARD_WUI_HOST host)
{
    StringCchCopyW(host->state.title, ARRAYSIZE(host->state.title), host->titleText.c_str());
    StringCchCopyW(host->state.text, ARRAYSIZE(host->state.text), host->textText.c_str());
    StringCchCopyW(host->state.source, ARRAYSIZE(host->state.source), host->sourceText.c_str());
    host->state.cb = sizeof(host->state);
    host->state.version = TCARD_WUI_STATE_ABI_VERSION;
}

static void end_edit(TCARD_WUI_HOST host, bool save)
{
    if (!host || !host->editing) return;
    if (save) {
        double validatedSize = 0;
        if (!tcard_ui::valid_size(read_window_text(host->fontSizeEditor), validatedSize)) {
            if (!host->appearanceOpen) SendMessageW(host->window, WM_COMMAND, kEditAppearance, 0);
            SetWindowTextW(GetDlgItem(host->window, 5024), tcard_text(L"message.font_size", L"Enter a font size from 8 to 48."));
            SetFocus(host->fontSizeEditor);
            SendMessageW(host->fontSizeEditor, EM_SETSEL, 0, -1);
            return;
        }
        host->titleText = read_window_text(host->titleEditor);
        host->sourceText = read_window_text(host->sourceEditor);
        host->textText = host->sourceText;
        host->markdown = SendMessageW(GetDlgItem(host->window, kEditMarkdown), CB_GETCURSEL, 0, 0) == 1;
        HWND colors = GetDlgItem(host->window, 5025);
        const LRESULT colorIndex = SendMessageW(colors, CB_GETCURSEL, 0, 0);
        if (colorIndex != CB_ERR) host->state.backColor = static_cast<COLORREF>(SendMessageW(colors, CB_GETITEMDATA, colorIndex, 0));
        update_read_brush(host);
        const std::wstring family = read_window_text(host->fontFamilyEditor);
        host->fontFamilyText = family.empty() ? L"Segoe UI" : family;
        const std::wstring sizeText = read_window_text(host->fontSizeEditor);
        wchar_t* sizeEnd = nullptr;
        const float requestedSize = wcstof(sizeText.c_str(), &sizeEnd);
        if (sizeEnd != sizeText.c_str() && *sizeEnd == L'\0' && requestedSize >= 8.0f && requestedSize <= 48.0f) host->fontSize = requestedSize;
        update_read_font(host);
        release_render(host);
        sync_legacy_state(host);
        sync_read_text(host);
        if (host->saveCallback) {
            if (!host->saveCallback(host->saveCallbackContext)) {
                SetWindowTextW(GetDlgItem(host->window, 5024), tcard_text(L"message.save_failed", L"Could not save. Your draft is still open."));
                return;
            }
        } else if (host->callback) {
            host->callback(3, host->callbackContext);
        }
    }
    DestroyWindow(host->titleEditor);
    DestroyWindow(host->sourceEditor);
    DestroyWindow(host->saveButton);
    DestroyWindow(host->cancelButton);
    DestroyWindow(host->appearanceButton);
    DestroyWindow(host->fontFamilyEditor);
    DestroyWindow(host->fontSizeEditor);
    DestroyWindow(GetDlgItem(host->window, kEditMarkdown));
    for (int id : {5021, 5022, 5023, 5024, 5025, 5026}) DestroyWindow(GetDlgItem(host->window, id));
    if (host->editorBrush) { DeleteObject(host->editorBrush); host->editorBrush = nullptr; }
    host->titleEditor = nullptr;
    host->sourceEditor = nullptr;
    host->saveButton = nullptr;
    host->cancelButton = nullptr;
    host->appearanceButton = nullptr;
    host->fontFamilyEditor = nullptr;
    host->fontSizeEditor = nullptr;
    host->appearanceOpen = false;
    host->editing = false;
    host->editDirty = false;
    if (host->readEditor) {
        sync_read_text(host);
        ShowWindow(host->readEditor, SW_SHOW);
        RECT client{};
        GetClientRect(host->window, &client);
        layout_read_editor(host, client.right - client.left, client.bottom - client.top);
    }
    InvalidateRect(host->window, nullptr, FALSE);
}

static void begin_edit(TCARD_WUI_HOST host)
{
    if (!host || host->editing) return;
    RECT client{};
    GetClientRect(host->window, &client);
    const int width = max(120L, client.right - client.left);
    const int height = max(140L, client.bottom - client.top);
    host->editing = true;
    host->editDirty = false;
    if (host->readEditor) ShowWindow(host->readEditor, SW_HIDE);
    host->editorBrush = CreateSolidBrush(host->state.backColor);
    host->appearanceOpen = false;
    if (!host->uiFont) host->uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    if (!host->buttonFont) host->buttonFont = CreateFontW(tcard_lang::g_code == L"ja" ? -12 : -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    tcard_ui::create_label(host->window, 5021, tcard_text(L"label.font", L"Font"), host->uiFont);
    tcard_ui::create_label(host->window, 5022, tcard_text(L"label.size", L"Size"), host->uiFont);
    tcard_ui::create_label(host->window, 5023, tcard_text(L"label.text_format", L"Text format"), host->uiFont);
    tcard_ui::create_label(host->window, 5026, tcard_text(L"label.paper_color", L"Paper color"), host->uiFont);
    HWND colors = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        0, 0, 160, 240, host->window, reinterpret_cast<HMENU>(5025), g_instance, nullptr);
    SendMessageW(colors, WM_SETFONT, reinterpret_cast<WPARAM>(host->uiFont), TRUE);
    const std::wstring names[] = {tcard_lang::text(L"color.current", L"Current color"), tcard_lang::text(L"color.yellow", L"Yellow"), tcard_lang::text(L"color.green", L"Green"), tcard_lang::text(L"color.blue", L"Blue"), tcard_lang::text(L"color.pink", L"Pink"), tcard_lang::text(L"color.lavender", L"Lavender"), tcard_lang::text(L"color.neutral", L"Neutral")};
    const COLORREF values[] = {host->state.backColor, RGB(255,245,168), RGB(217,242,217), RGB(205,235,255), RGB(244,213,232), RGB(233,221,247), RGB(243,241,235)};
    for (int i = 0; i < 7; ++i) {
        SendMessageW(colors, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(names[i].c_str()));
        SendMessageW(colors, CB_SETITEMDATA, i, values[i]);
    }
    SendMessageW(colors, CB_SETCURSEL, 0, 0);
    ShowWindow(tcard_ui::create_label(host->window, 5024, L"", host->uiFont), SW_SHOW);
    host->titleEditor = CreateWindowExW(0, L"EDIT", host->titleText.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, tcard_ui::kPaperInsetDip, tcard_ui::kPaperHeaderDip + 8, width - tcard_ui::kPaperInsetDip * 2, 28, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditTitle)), g_instance, nullptr);
    host->sourceEditor = tcard_edit::create(host->window, kEditSource, kEditSave, host->sourceText.c_str(), tcard_ui::kPaperInsetDip, tcard_ui::kPaperHeaderDip + 40, width - tcard_ui::kPaperInsetDip * 2, max(80, height - tcard_ui::kPaperHeaderDip - tcard_ui::kEditorCommandDip - 40), g_instance);
    tcard_ui::attach_scroll(host->sourceEditor, host->state.backColor);
    tcard_edit::attach(host->titleEditor, false, kEditSave);
    SendMessageW(host->sourceEditor, EM_SETBKGNDCOLOR, 0, host->state.backColor);
    host->appearanceButton = CreateWindowExW(0, L"BUTTON", tcard_text(L"button.appearance", L"Appearance"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, tcard_ui::kPaperInsetDip, 2, 100, 28, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditAppearance)), g_instance, nullptr);
    host->fontFamilyEditor = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr, WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, tcard_ui::kPaperInsetDip, tcard_ui::kPaperHeaderDip + 40, 180, 240, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditFontFamily)), g_instance, nullptr);
    populate_font_families(host->fontFamilyEditor, host->window);
    SendMessageW(host->fontFamilyEditor, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(host->fontFamilyText.c_str()));
    wchar_t fontSizeText[32]{};
    swprintf_s(fontSizeText, ARRAYSIZE(fontSizeText), L"%.1f", card_font_size(host));
    host->fontSizeEditor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", fontSizeText, WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, tcard_ui::kPaperInsetDip + 188, tcard_ui::kPaperHeaderDip + 40, 68, 24, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditFontSize)), g_instance, nullptr);
    HWND formatEditor = CreateWindowExW(0, L"COMBOBOX", nullptr, WS_CHILD | WS_VSCROLL | CBS_DROPDOWNLIST | WS_TABSTOP, tcard_ui::kPaperInsetDip, tcard_ui::kPaperHeaderDip + 68, 140, 180, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditMarkdown)), g_instance, nullptr);
    SendMessageW(formatEditor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tcard_text(L"format.plain", L"Plain text")));
    SendMessageW(formatEditor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(tcard_text(L"format.markdown", L"Markdown")));
    SendMessageW(formatEditor, CB_SETCURSEL, host->markdown ? 1 : 0, 0);
    SendMessageW(host->fontFamilyEditor, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(tcard_text(L"font.family_placeholder", L"Font family")));
    SendMessageW(host->fontSizeEditor, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(tcard_text(L"label.size", L"Size")));
    ShowWindow(host->fontFamilyEditor, SW_HIDE);
    ShowWindow(host->fontSizeEditor, SW_HIDE);
    host->saveButton = CreateWindowExW(0, L"BUTTON", tcard_text(L"button.save", L"Save"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, max(16, width - 96), max(60, height - 40), 80, 28, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditSave)), g_instance, nullptr);
    host->cancelButton = CreateWindowExW(0, L"BUTTON", tcard_text(L"button.cancel", L"Cancel"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, max(16, width - 184), max(60, height - 40), 80, 28, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditCancel)), g_instance, nullptr);
    SetFocus(host->sourceEditor);
    SendMessageW(host->sourceEditor, EM_SETLIMITTEXT, tcard_clip::max_source, 0);
    for (HWND control : {host->titleEditor, host->sourceEditor, host->saveButton, host->cancelButton, host->appearanceButton, host->fontFamilyEditor, host->fontSizeEditor, GetDlgItem(host->window, kEditMarkdown)})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(host->uiFont), TRUE);
    for (HWND button : {host->saveButton, host->cancelButton, host->appearanceButton}) {
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(host->buttonFont ? host->buttonFont : host->uiFont), TRUE);
        SendMessageW(button, BM_SETSTYLE, BS_OWNERDRAW, TRUE);
        SetWindowSubclass(button, tcard_ui::button_proc, 1, 0);
    }
    SendMessageW(host->titleEditor, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(tcard_text(L"title.placeholder", L"Title")));
    SendMessageW(host->fontSizeEditor, EM_SETLIMITTEXT, 16, 0);
    tcard_edit::attach(host->fontSizeEditor, false, kEditSave);
    RECT bounds{};
    GetWindowRect(host->window, &bounds);
    SetWindowPos(host->window, nullptr, 0, 0, max(320L, bounds.right - bounds.left),
        max(340L, bounds.bottom - bounds.top), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetClientRect(host->window, &client);
    SendMessageW(host->window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
}

static LRESULT CALLBACK card_wnd_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* host = reinterpret_cast<TCARD_WUI_HOST>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        host = reinterpret_cast<TCARD_WUI_HOST>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
        host->window = hwnd;
    }
    if (!host) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCHITTEST:
        {
            POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            ScreenToClient(hwnd, &point);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int edge = 6;
            const bool left = point.x < edge, right = point.x >= rc.right - edge;
            const bool top = point.y < edge, bottom = point.y >= rc.bottom - edge;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (point.y < 36 && point.x < rc.right - 48) return HTCAPTION;
            return HTCLIENT;
        }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = host->editing ? POINT{320, 340} : POINT{260, 200};
        return 0;
    case WM_DRAWITEM:
        {
            auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID != kEditClose && item->CtlID != kEditSave && item->CtlID != kEditCancel && item->CtlID != kEditAppearance) break;
            if (item->CtlID != kEditClose) {
                tcard_ui::paint_button(*item, host->state.backColor, host->state.textColor, host->buttonFont ? host->buttonFont : host->uiFont, item->CtlID == kEditSave);
                return TRUE;
            }
            HBRUSH brush = CreateSolidBrush(tcard_ui::shade_color(host->state.backColor, (item->itemState & ODS_SELECTED) ? 0.85f : 0.94f));
            FillRect(item->hDC, &item->rcItem, brush);
            DeleteObject(brush);
            HBRUSH edge = CreateSolidBrush(tcard_ui::shade_color(host->state.backColor, 0.80f));
            FrameRect(item->hDC, &item->rcItem, edge); DeleteObject(edge);
            if (item->CtlID != kEditClose) {
                SetBkMode(item->hDC, TRANSPARENT);
                SetTextColor(item->hDC, host->state.textColor);
                HGDIOBJ font = SelectObject(item->hDC, host->uiFont ? host->uiFont : GetStockObject(DEFAULT_GUI_FONT));
                RECT label = item->rcItem;
                const std::wstring text = read_window_text(item->hwndItem);
                DrawTextW(item->hDC, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(item->hDC, font);
                if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &label);
                return TRUE;
            }
            HPEN pen = CreatePen(PS_SOLID, 1, host->state.textColor);
            HGDIOBJ old = SelectObject(item->hDC, pen);
            const int x = (item->rcItem.right + item->rcItem.left) / 2;
            const int y = (item->rcItem.bottom + item->rcItem.top) / 2;
            MoveToEx(item->hDC, x - 5, y - 5, nullptr); LineTo(item->hDC, x + 6, y + 6);
            MoveToEx(item->hDC, x + 5, y - 5, nullptr); LineTo(item->hDC, x - 6, y + 6);
            SelectObject(item->hDC, old); DeleteObject(pen);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
    case WM_COMMAND:
        if (LOWORD(wParam) == kEditClose) SendMessageW(hwnd, WM_CLOSE, 0, 0);
        else if (LOWORD(wParam) == kEditAppearance) {
            host->appearanceOpen = !host->appearanceOpen;
            ShowWindow(host->fontFamilyEditor, host->appearanceOpen ? SW_SHOW : SW_HIDE);
            ShowWindow(host->fontSizeEditor, host->appearanceOpen ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(hwnd, kEditMarkdown), host->appearanceOpen ? SW_SHOW : SW_HIDE);
            for (int id : {5021, 5022, 5023, 5025, 5026}) ShowWindow(GetDlgItem(hwnd, id), host->appearanceOpen ? SW_SHOW : SW_HIDE);
            ShowWindow(host->titleEditor, host->appearanceOpen ? SW_HIDE : SW_SHOW);
            ShowWindow(host->sourceEditor, host->appearanceOpen ? SW_HIDE : SW_SHOW);
            SetWindowTextW(host->appearanceButton, host->appearanceOpen ? tcard_text(L"button.back_to_note", L"Back to note") : tcard_text(L"button.appearance", L"Appearance"));
            RECT client{};
            GetClientRect(hwnd, &client);
            SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
        }
        else if (LOWORD(wParam) == kEditSave) end_edit(host, true);
        else if (LOWORD(wParam) == kEditCancel) end_edit(host, false);
        else if ((LOWORD(wParam) == kEditTitle || LOWORD(wParam) == kEditSource || LOWORD(wParam) == kEditFontFamily || LOWORD(wParam) == kEditFontSize) && HIWORD(wParam) == EN_CHANGE) {
            host->editDirty = true;
            if (LOWORD(wParam) == kEditSource) update_vertical_scroll(host->sourceEditor);
        }
        else if (LOWORD(wParam) == kEditMarkdown && HIWORD(wParam) == CBN_SELCHANGE) host->editDirty = true;
        else if (LOWORD(wParam) == kEditFontFamily && HIWORD(wParam) == CBN_SELCHANGE) host->editDirty = true;
        else if (LOWORD(wParam) == 5025 && HIWORD(wParam) == CBN_SELCHANGE) host->editDirty = true;
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        {
            const HWND control = reinterpret_cast<HWND>(lParam);
            if (control == host->readEditor && host->readBrush) {
                SetBkColor(reinterpret_cast<HDC>(wParam), host->state.backColor);
                SetTextColor(reinterpret_cast<HDC>(wParam), host->state.textColor);
                return reinterpret_cast<LRESULT>(host->readBrush);
            }
            if (host->editorBrush && (control == host->titleEditor || control == host->sourceEditor ||
                (GetDlgCtrlID(control) >= 5021 && GetDlgCtrlID(control) <= 5026))) {
                SetBkColor(reinterpret_cast<HDC>(wParam), host->state.backColor);
                SetTextColor(reinterpret_cast<HDC>(wParam), host->state.textColor);
                return reinterpret_cast<LRESULT>(host->editorBrush);
            }
            break;
        }
    case WM_CLOSE:
        if (host->editing && host->editDirty) {
            const int choice = MessageBoxW(hwnd, tcard_text(L"message.save_changes", L"Save changes to this card?"), tcard_text(L"app.title", L"TCard"), MB_ICONQUESTION | MB_YESNOCANCEL | MB_DEFBUTTON3);
            if (choice == IDCANCEL) return 0;
            if (choice == IDYES) {
                end_edit(host, true);
                if (host->editing) return 0;
            } else {
                end_edit(host, false);
            }
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCRBUTTONUP:
        SendMessageW(hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), lParam);
        return 0;
    case WM_CONTEXTMENU:
        {
            HMENU menu = CreatePopupMenu();
            if (!menu) return 0;
            AppendMenuW(menu, MF_STRING, 1, tcard_text(L"context.edit", L"Edit"));
            AppendMenuW(menu, MF_STRING | (host->topmost ? MF_CHECKED : 0), 2, tcard_text(L"context.always_on_top", L"Always on top"));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 4, tcard_text(L"context.show_list", L"Show list"));
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 5, tcard_text(L"context.delete", L"Delete"));
            HMENU clip = CreatePopupMenu();
            if (clip) {
                AppendMenuW(clip, MF_STRING, 6, tcard_text(L"clip.new", L"Create new note"));
                AppendMenuW(clip, MF_STRING, 7, tcard_text(L"clip.append", L"Append to this note"));
                AppendMenuW(clip, MF_STRING, 8, tcard_text(L"clip.replace", L"Replace this note"));
                AppendMenuW(clip, MF_STRING, 9, tcard_text(L"clip.undo", L"Undo last import"));
                AppendMenuW(menu, MF_POPUP | (host->editing ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(clip), tcard_text(L"clip.menu", L"Import from clipboard"));
            }
            POINT point{ static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)) };
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, hwnd, nullptr);
            DestroyMenu(menu);
            if (command >= 6 && command <= 9) { TCardWuiExecuteCommand(host, command); return 0; }
            if (command == 5) { TCardWuiExecuteCommand(host, command); return 0; }
            if (command) TCardWuiExecuteCommand(host, command);
            if (host->editing) return 0;
        }
        return 0;
    case WM_NOTIFY:
        return tcard_md::notify(lParam);
    case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            paint_card(host);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_SIZE:
        if (host->target) host->target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            if (host->closeButton) MoveWindow(host->closeButton, max(0, width - 44), 2, 36, 32, TRUE);
            if (host->editing) {
                MoveWindow(host->titleEditor, tcard_ui::kPaperInsetDip, tcard_ui::kPaperHeaderDip + 8, max(120, width - tcard_ui::kPaperInsetDip * 2), 28, TRUE);
                const int sourceTop = tcard_ui::kPaperHeaderDip + 44;
                const int settingsWidth = min(360, width - 32);
                tcard_ui::layout_setting(GetDlgItem(hwnd, 5021), host->fontFamilyEditor, 16, 60, settingsWidth, true);
                tcard_ui::layout_setting(GetDlgItem(hwnd, 5022), host->fontSizeEditor, 16, 100, 176, false);
                tcard_ui::layout_setting(GetDlgItem(hwnd, 5023), GetDlgItem(hwnd, kEditMarkdown), 16, 140, settingsWidth, true);
                tcard_ui::layout_setting(GetDlgItem(hwnd, 5026), GetDlgItem(hwnd, 5025), 16, 180, settingsWidth, true);
                MoveWindow(GetDlgItem(hwnd, 5024), 16, height - 76, width - 32, 28, TRUE);
                MoveWindow(host->sourceEditor, tcard_ui::kPaperInsetDip, sourceTop, max(1, width - 32), max(1, height - sourceTop - 84), TRUE);
                MoveWindow(host->cancelButton, max(16, width - 184), max(60, height - 40), 80, 28, TRUE);
                MoveWindow(host->saveButton, max(16, width - 96), max(60, height - 40), 80, 28, TRUE);
                update_vertical_scroll(host->sourceEditor);
            } else {
                layout_read_editor(host, width, height);
                update_vertical_scroll(host->readEditor);
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        if (host->buttonFont) { DeleteObject(host->buttonFont); host->buttonFont = nullptr; }
        if (host->uiFont) { DeleteObject(host->uiFont); host->uiFont = nullptr; }
        if (host->editorBrush) { DeleteObject(host->editorBrush); host->editorBrush = nullptr; }
        if (host->readBrush) { DeleteObject(host->readBrush); host->readBrush = nullptr; }
        if (host->readFont) { DeleteObject(host->readFont); host->readFont = nullptr; }
        host->window = nullptr;
        host->target.Reset();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static BOOL register_card_class()
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = g_instance;
    wc.lpfnWndProc = card_wnd_proc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TCardWuiWindow";
    return RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH) g_instance = instance;
    return TRUE;
}

extern "C" TCARD_WUI_HOST WINAPI TCardWuiCreateCard(HWND owner)
{
    UNREFERENCED_PARAMETER(owner);
    if (!register_card_class()) return nullptr;
    auto* host = new (std::nothrow) TCARD_WUI_HOST__;
    if (!host) return nullptr;
    host->state.cb = sizeof(host->state);
    host->state.version = TCARD_WUI_STATE_ABI_VERSION;
    host->state.backColor = RGB(255, 245, 168);
    host->titleText = host->state.title;
    host->textText = host->state.text;
    host->sourceText = host->state.source;
    host->state.textColor = RGB(37, 37, 37);
    host->state.secondaryColor = RGB(102, 102, 102);
    update_read_brush(host);
    g_rich_edit = LoadLibraryW(L"Msftedit.dll");
    tcard_lang::initialize();
    host->window = CreateWindowExW(WS_EX_TOOLWINDOW, L"TCardWuiWindow", tcard_text(L"app.title", L"TCard"), WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_CLIPCHILDREN,
        120, 120, 340, 320, nullptr, nullptr, g_instance, host);
    if (!host->window) { delete host; return nullptr; }
    host->readEditor = CreateWindowExW(0, g_rich_edit ? L"RICHEDIT50W" : L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL | WS_TABSTOP, 0, 0, 120, 80, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReadBody)), g_instance, nullptr);
    if (!host->readEditor) { DestroyWindow(host->window); delete host; return nullptr; }
    tcard_ui::attach_scroll(host->readEditor, host->state.backColor);
    update_read_font(host);
    SendMessageW(host->readEditor, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    host->closeButton = CreateWindowExW(0, L"BUTTON", tcard_text(L"button.close", L"Close"), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP, 0, 2, 36, 32, host->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditClose)), g_instance, nullptr);
    RECT client{};
    GetClientRect(host->window, &client);
    layout_read_editor(host, client.right - client.left, client.bottom - client.top);
    MoveWindow(host->closeButton, max(0L, client.right - 44), 2, 36, 32, TRUE);
    return host;
}

extern "C" void WINAPI TCardWuiDestroyCard(TCARD_WUI_HOST host)
{
    if (!host) return;
    if (host->window && IsWindow(host->window)) DestroyWindow(host->window);
    delete host;
}

extern "C" BOOL WINAPI TCardWuiSetCardState(TCARD_WUI_HOST host, const TCARD_WUI_STATE* state)
{
    if (!host || !state) return FALSE;
    const SIZE_T size = min(static_cast<SIZE_T>(state->cb), sizeof(host->state));
    ZeroMemory(&host->state, sizeof(host->state));
    CopyMemory(&host->state, state, size);
    host->state.cb = sizeof(host->state);
    host->state.version = TCARD_WUI_STATE_ABI_VERSION;
    host->titleText = host->state.title;
    host->textText = host->state.text;
    host->sourceText = host->state.source;
    host->fontFamilyText = L"Segoe UI";
    host->fontSize = 14.0f;
    update_read_brush(host);
    sync_read_text(host);
    release_render(host);
    if (host->window) {
        SetWindowTextW(host->window, host->state.title[0] ? host->state.title : tcard_text(L"app.title", L"TCard"));
        InvalidateRect(host->window, nullptr, FALSE);
    }
    return TRUE;
}

extern "C" BOOL WINAPI TCardWuiGetCardState(TCARD_WUI_HOST host, TCARD_WUI_STATE* state)
{
    if (!host || !state) return FALSE;
    const SIZE_T size = min(static_cast<SIZE_T>(state->cb), sizeof(host->state));
    CopyMemory(state, &host->state, size);
    state->cb = sizeof(host->state);
    return TRUE;
}

static bool assign_text(std::wstring& target, const WCHAR* value, DWORD length)
{
    if (length > kMaxTextUnits || (length && !value)) return false;
    target.assign(value ? value : L"", length);
    return true;
}

static void copy_text(WCHAR* destination, DWORD capacity, DWORD& length, const std::wstring& source)
{
    length = static_cast<DWORD>(source.size());
    if (!destination || capacity == 0) return;
    const DWORD copied = min(length, capacity - 1);
    if (copied) CopyMemory(destination, source.data(), static_cast<SIZE_T>(copied) * sizeof(WCHAR));
    destination[copied] = L'\0';
}

extern "C" BOOL WINAPI TCardWuiSetCardAssetRoot(TCARD_WUI_HOST host, const WCHAR* root, DWORD length)
{
    if (!host || length > 32767 || (!root && length)) return FALSE;
    try {
        std::wstring value = length ? std::wstring(root, length) : std::wstring{};
        if (value.find(L'\0') != std::wstring::npos) return FALSE;
        if (host->assetRoot != value) { host->assetRoot = std::move(value); if (!host->editing) sync_read_text(host); }
        return TRUE;
    } catch (...) { return FALSE; }
}

extern "C" BOOL WINAPI TCardWuiSetCardTextState(TCARD_WUI_HOST host, const TCARD_WUI_TEXT_STATE* state)
{
    if (!host || !state || state->cb < TCARD_WUI_TEXT_STATE_MIN_CB || state->version < TCARD_WUI_STATE_ABI_VERSION) return FALSE;
    try {
        std::wstring title, text, source, family;
        if (!assign_text(title, state->title, state->titleLength) ||
            !assign_text(text, state->text, state->textLength) ||
            !assign_text(source, state->source, state->sourceLength) ||
            !assign_text(family, state->fontFamily, state->fontFamilyLength)) return FALSE;
        if (family.empty()) family = L"Segoe UI";
        const float size = state->fontSize >= 8.0f && state->fontSize <= 48.0f ? state->fontSize : 14.0f;
        const bool titleChanged = host->titleText != title;
        const bool textChanged = host->textText != text;
        const bool fontChanged = !host->editing && (host->fontFamilyText != family || host->fontSize != size);
        const bool formatChanged = host->markdown != (state->markdown != FALSE);
        const bool colorChanged = host->state.backColor != state->backColor ||
            host->state.textColor != state->textColor || host->state.secondaryColor != state->secondaryColor;
        const bool sourceChanged = host->sourceText != source;
        if (!titleChanged && !textChanged && !fontChanged && !formatChanged && !colorChanged && !sourceChanged) return TRUE;
        host->titleText = std::move(title);
        host->textText = std::move(text);
        host->sourceText = std::move(source);
        host->state.backColor = state->backColor;
        host->state.textColor = state->textColor;
        host->state.secondaryColor = state->secondaryColor;
        host->markdown = state->markdown != FALSE;
        if (fontChanged) {
            host->fontFamilyText = std::move(family);
            host->fontSize = size;
            update_read_font(host);
        }
        if (colorChanged) update_read_brush(host);
        sync_legacy_state(host);
        if (textChanged || formatChanged || fontChanged || colorChanged) sync_read_text(host);
        if (colorChanged || fontChanged) release_render(host);
        if (host->window) {
            if (titleChanged) SetWindowTextW(host->window, host->titleText.empty() ? tcard_text(L"app.title", L"TCard") : host->titleText.c_str());
            if (!host->editing && (titleChanged || fontChanged)) {
                RECT client{};
                GetClientRect(host->window, &client);
                layout_read_editor(host, client.right, client.bottom);
            }
            if (titleChanged || fontChanged || colorChanged) InvalidateRect(host->window, nullptr, FALSE);
        }
        return TRUE;
    } catch (...) {
        return FALSE;
    }
}

static bool valid_output_buffer(const WCHAR* buffer, DWORD capacity)
{
    return capacity == 0 || buffer != nullptr;
}

extern "C" BOOL WINAPI TCardWuiGetCardTextState(TCARD_WUI_HOST host, TCARD_WUI_TEXT_STATE_BUFFER* state)
{
    if (!host || !state || state->cb < TCARD_WUI_TEXT_STATE_BUFFER_MIN_CB || state->version < TCARD_WUI_STATE_ABI_VERSION) return FALSE;
    if (!valid_output_buffer(state->title, state->titleCapacity) ||
        !valid_output_buffer(state->text, state->textCapacity) ||
        !valid_output_buffer(state->source, state->sourceCapacity)) return FALSE;
    state->cb = sizeof(*state);
    state->version = TCARD_WUI_STATE_ABI_VERSION;
    state->backColor = host->state.backColor;
    state->textColor = host->state.textColor;
    state->secondaryColor = host->state.secondaryColor;
    copy_text(state->title, state->titleCapacity, state->titleLength, host->titleText);
    copy_text(state->text, state->textCapacity, state->textLength, host->textText);
    copy_text(state->source, state->sourceCapacity, state->sourceLength, host->sourceText);
    copy_text(state->fontFamily, state->fontFamilyCapacity, state->fontFamilyLength, host->fontFamilyText);
    state->fontSize = card_font_size(host);
    state->markdown = host->markdown ? TRUE : FALSE;
    return TRUE;
}

extern "C" BOOL WINAPI TCardWuiShowCard(TCARD_WUI_HOST host, BOOL visible)
{
    if (!host || !host->window || !IsWindow(host->window)) return FALSE;
    ShowWindow(host->window, visible ? SW_SHOW : SW_HIDE);
    if (visible) UpdateWindow(host->window);
    return TRUE;
}

extern "C" HWND WINAPI TCardWuiGetCardWindow(TCARD_WUI_HOST host)
{
    return host ? host->window : nullptr;
}

extern "C" void WINAPI TCardWuiSetCardCommandCallback(TCARD_WUI_HOST host, TCARD_WUI_COMMAND_CALLBACK callback, void* context)
{
    if (!host) return;
    host->callback = callback;
    host->callbackContext = context;
}

extern "C" void WINAPI TCardWuiSetCardSaveCallback(TCARD_WUI_HOST host, TCARD_WUI_SAVE_CALLBACK callback, void* context)
{
    if (!host) return;
    host->saveCallback = callback;
    host->saveCallbackContext = context;
}

extern "C" BOOL WINAPI TCardWuiExecuteCommand(TCARD_WUI_HOST host, UINT command)
{
    if (!host || !host->window) return FALSE;
    if (command == 1) { begin_edit(host); return TRUE; }
    if (command >= 6 && command <= 9) {
        if (host->callback && !host->editing) { host->callback(command, host->callbackContext); return TRUE; }
        return FALSE;
    }
    if (command == 3) {
        if (host->saveCallback) return host->saveCallback(host->saveCallbackContext);
        if (host->callback) host->callback(3, host->callbackContext);
        return TRUE;
    }
    if (command == 2) {
        host->topmost = !host->topmost;
        SetWindowPos(host->window, host->topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return TRUE;
    }
    if (command == 4) {
        if (host->callback) { host->callback(4, host->callbackContext); return TRUE; }
        return FALSE;
    }
    if (command == 5) {
        if (host->callback) { host->callback(5, host->callbackContext); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

extern "C" BOOL WINAPI TCardWuiIsTopmost(TCARD_WUI_HOST host)
{
    return host && host->topmost ? TRUE : FALSE;
}

extern "C" BOOL WINAPI TCardWuiIsCardEditing(TCARD_WUI_HOST host)
{
    return host && host->editing ? TRUE : FALSE;
}

extern "C" BOOL WINAPI TCardWuiCreateHost(HWND owner)
{
    if (g_legacy) return TRUE;
    g_legacy = TCardWuiCreateCard(owner);
    return g_legacy != nullptr;
}

extern "C" void WINAPI TCardWuiDestroyHost(void)
{
    TCardWuiDestroyCard(g_legacy);
    g_legacy = nullptr;
}

extern "C" BOOL WINAPI TCardWuiSetState(const TCARD_WUI_STATE* state)
{
    return TCardWuiSetCardState(g_legacy, state);
}

extern "C" BOOL WINAPI TCardWuiShow(BOOL visible)
{
    return TCardWuiShowCard(g_legacy, visible);
}

extern "C" HWND WINAPI TCardWuiGetWindow(void)
{
    return TCardWuiGetCardWindow(g_legacy);
}

extern "C" void WINAPI TCardWuiSetCommandCallback(TCARD_WUI_COMMAND_CALLBACK callback, void* context)
{
    TCardWuiSetCardCommandCallback(g_legacy, callback, context);
}
