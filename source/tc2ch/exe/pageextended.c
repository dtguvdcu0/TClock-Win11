#include "tclock.h"
#include "../common/analog_skin.h"
#include "../common/flip_clock.h"
#include "../common/led_clock.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
extern BOOL b_EnglishMenu;
typedef struct EXT_ORIGINAL { HWND window; RECT rect; } EXT_ORIGINAL;
typedef struct EXT_LED_CONTROL { HWND window; int row,x,y,width,minimum; BOOL details; } EXT_LED_CONTROL;
typedef struct EXT_PAGE {
    BOOL initializing;
    BOOL animationTimer;
    BOOL ledDirty;
    int loadedFace, loadedMode, currentMode;
    int normalFace, flipFace;
    WCHAR normalSize[32], flipSize[32], legacySize[32], nixieSize[32];
    BOOL digitalColon[2], digitalStacked[2];
    BOOL nixieBase;
    int digitalDuration[2];
    WCHAR ledSize[32];
    LED_CONTEXT* ledPreview;
    LED_OPTIONS ledOptions;
    HWND ledPanel,ledTabs,ledPlay;
    int ledSection,ledMessage; BOOL ledPlaying;
    int ledScroll,ledHeight,ledView,ledWheel;
    EXT_ORIGINAL originals[32]; int originalCount;
    EXT_LED_CONTROL ledControls[128]; int ledControlCount;
    ACS_CONTEXT* preview;
    FLP_CONTEXT* flipPreview;
} EXT_PAGE;
#include "extended_led.h"

static BOOL ext_read_value(HWND dialog, int id, int low, int high, int* value)
{
    WCHAR text[32], *end;
    ext_get_text(dialog, id, text, _countof(text));
    errno = 0;
    long parsed = wcstol(text, &end, 10);
    if (!text[0] || *end || errno == ERANGE || parsed < low || parsed > high) return FALSE;
    *value = (int)parsed;
    return TRUE;
}


// Combo ordering is presentation-only; stable mode IDs and persisted names do not change.
static int ext_read_mode(HWND dialog)
{
    LRESULT index = ext_send_control(dialog, IDC_EXT_MODE, CB_GETCURSEL, 0, 0);
    return index == CB_ERR ? -1 : (int)ext_send_control(dialog, IDC_EXT_MODE, CB_GETITEMDATA, index, 0);
}

static void ext_add_mode(HWND dialog, int mode, const WCHAR* label)
{
    LRESULT index = ext_send_control(dialog, IDC_EXT_MODE, CB_ADDSTRING, 0, (LPARAM)label);
    if (index != CB_ERR && index != CB_ERRSPACE)
        ext_send_control(dialog, IDC_EXT_MODE, CB_SETITEMDATA, index, mode);
}

static void ext_select_mode(HWND dialog, int mode)
{
    int i, count = (int)ext_send_control(dialog, IDC_EXT_MODE, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; ++i) {
        if (ext_send_control(dialog, IDC_EXT_MODE, CB_GETITEMDATA, i, 0) == mode) {
            ext_send_control(dialog, IDC_EXT_MODE, CB_SETCURSEL, i, 0);
            return;
        }
    }
}

static BOOL ext_is_digital(int mode)
{
    return mode == EXT_MODE_FLIP || mode == EXT_MODE_NIXIE;
}

static WCHAR* ext_get_size(EXT_PAGE* page, int mode)
{
    return mode == EXT_MODE_LED ? page->ledSize : mode == EXT_MODE_NIXIE ? page->nixieSize : mode == EXT_MODE_FLIP ? page->flipSize :
        mode == EXT_MODE_LEGACY ? page->legacySize : page->normalSize;
}

static void ext_keep_digital(HWND dialog, EXT_PAGE* page, int mode)
{
    if (ext_is_digital(mode)) {
        int index = mode == EXT_MODE_NIXIE;
        page->digitalColon[index] = ext_get_checked(dialog, IDC_EXT_COLON) == BST_CHECKED;
        page->digitalStacked[index] = ext_send_control(dialog, IDC_EXT_SIDE, CB_GETCURSEL, 0, 0) == 0;
        if (mode == EXT_MODE_NIXIE) page->nixieBase = ext_get_checked(dialog, IDC_EXT_BASE) == BST_CHECKED;
        page->digitalDuration[index] = (int)ext_send_control(dialog, IDC_EXT_SPEED, TBM_GETPOS, 0, 0)*50;
    }
}

static void ext_update_speed(HWND dialog);

static BOOL ext_read_options(HWND dialog, ACS_OPTIONS* options, BOOL report)
{
    const int ids[] = {IDC_EXT_SIZE, IDC_EXT_X, IDC_EXT_Y};
    int* values[] = {&options->diameter, &options->offsetX, &options->offsetY};
    int i;
    ZeroMemory(options, sizeof(*options));
    options->enabled = ext_get_checked(dialog, IDC_EXT_ENABLE) == BST_CHECKED;
    options->nixieBase = ext_get_checked(dialog, IDC_EXT_BASE) == BST_CHECKED;
    options->seconds = ext_get_checked(dialog, IDC_EXT_SECONDS) == BST_CHECKED;
    options->colon = ext_get_checked(dialog, IDC_EXT_COLON) == BST_CHECKED;
    options->flipStacked = ext_send_control(dialog, IDC_EXT_SIDE, CB_GETCURSEL, 0, 0) == 0;
    options->flipDuration = (int)ext_send_control(dialog, IDC_EXT_SPEED, TBM_GETPOS, 0, 0)*50;
    options->mode = ext_read_mode(dialog);
    options->face = (int)ext_send_control(dialog, IDC_EXT_FACE, CB_GETCURSEL, 0, 0);
    if (options->mode == EXT_MODE_CLASSIC || options->mode == EXT_MODE_LEGACY || options->mode == EXT_MODE_NIXIE || options->mode == EXT_MODE_LED) options->face = 0;
    options->trailing = ext_send_control(dialog, IDC_EXT_PLACE, CB_GETCURSEL, 0, 0) == 1;
    for (i = 0; i < 3; ++i) {
        if(i&&options->mode==EXT_MODE_LED&&ext_get_checked(dialog,IDC_LED_DETAILS)!=BST_CHECKED){*values[i]=0;continue;}
        if (!ext_read_value(dialog, ids[i], i ? -1000 : 0, i ? 1000 : 256, values[i]) ||
            (!i && options->diameter > 0 && options->diameter < 16)) {
            if (report) {
                if(options->mode==EXT_MODE_LED){EXT_PAGE* page=(EXT_PAGE*)GetWindowLongPtrW(dialog,DWLP_USER);page->ledSection=0;TabCtrl_SetCurSel(page->ledTabs,0);ext_layout_led(dialog,page,TRUE);}
                MessageBoxW(dialog, b_EnglishMenu ?
                    L"Size: 0 (auto) or 16-256 DIP. Offsets: -1000 to 1000 DIP." :
                    L"サイズ: 0 (自動) または 16～256 DIP。位置補正: -1000～1000 DIP。",
                    b_EnglishMenu ? L"Extended Display" : L"拡張表示", MB_OK | MB_ICONWARNING);
                SetFocus(ext_get_control(dialog, ids[i]));
                ext_send_control(dialog, ids[i], EM_SETSEL, 0, -1);
            }
            return FALSE;
        }
    }
    if(options->mode==EXT_MODE_LED&&ext_get_checked(dialog,IDC_LED_DETAILS)!=BST_CHECKED)
        options->offsetX=options->offsetY=0;
    return options->mode >= EXT_MODE_NORMAL && options->mode <= EXT_MODE_LED &&
        options->face >= 0 && options->face <= ((options->mode == EXT_MODE_FLIP || options->mode == EXT_MODE_NORMAL) ? 1 : 0);
}

static void ext_stop_preview(HWND dialog, EXT_PAGE* page)
{
    KillTimer(dialog, 2);
    if (page) page->animationTimer = FALSE;
}

static void ext_switch_mode(HWND dialog, EXT_PAGE* page, int mode)
{
    const WCHAR* facesJa[] = {L"アラビア数字", L"目盛り"};
    const WCHAR* facesEn[] = {L"Arabic numerals", L"Ticks"};
    int face, i;
    page->initializing = TRUE;
    if (page->currentMode >= 0) {
        if (page->currentMode == EXT_MODE_NORMAL)
            page->normalFace = (int)ext_send_control(dialog, IDC_EXT_FACE, CB_GETCURSEL, 0, 0);
        if (page->currentMode == EXT_MODE_FLIP)
            page->flipFace = (int)ext_send_control(dialog, IDC_EXT_FACE, CB_GETCURSEL, 0, 0);
        ext_get_text(dialog, IDC_EXT_SIZE, ext_get_size(page, page->currentMode), 32);
        ext_keep_digital(dialog, page, page->currentMode);
    }
    ext_layout_led(dialog, page, FALSE);
    ext_send_control(dialog, IDC_EXT_FACE, CB_RESETCONTENT, 0, 0);
    if (mode == EXT_MODE_FLIP) {
        ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"White" : L"白地"));
        ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"Black" : L"黒地"));
        face = page->flipFace;
    } else if (mode == EXT_MODE_LED) {
        ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0, (LPARAM)L"LED"); face = 0;
    } else if (mode == EXT_MODE_NIXIE) {
        ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0,
            (LPARAM)(b_EnglishMenu ? L"Amber" : L"\u30a2\u30f3\u30d0\u30fc"));
        face = 0;
    } else if (mode == EXT_MODE_LEGACY) {
        ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0,
            (LPARAM)(b_EnglishMenu ? L"Standard" : L"\u6a19\u6e96"));
        face = 0;
    } else {
        for (i = 0; i < (mode == EXT_MODE_CLASSIC ? 1 : 2); ++i) ext_send_control(dialog, IDC_EXT_FACE, CB_ADDSTRING, 0,
            (LPARAM)(b_EnglishMenu ? facesEn[i] : facesJa[i]));
        face = mode == EXT_MODE_CLASSIC ? 0 : page->normalFace;
    }
    ext_send_control(dialog, IDC_EXT_FACE, CB_SETCURSEL, face, 0);
    ext_set_text(dialog, IDC_EXT_SIZE, ext_get_size(page, mode));
    ext_set_text(dialog, IDC_EXT_FACE_LABEL, mode == EXT_MODE_NIXIE ?
        (b_EnglishMenu ? L"Glow" : L"\u767a\u5149") : mode == EXT_MODE_FLIP ?
        (b_EnglishMenu ? L"Card" : L"カード") : (b_EnglishMenu ? L"Face" : L"文字盤"));
    ext_set_text(dialog, IDC_EXT_SIZE_LABEL, mode == EXT_MODE_NIXIE ?
        (b_EnglishMenu ? L"Tube height (DIP)" : L"\u7ba1\u306e\u9ad8\u3055 (DIP)") : mode == EXT_MODE_FLIP ?
        (b_EnglishMenu ? L"Card height (DIP)" : L"カード高 (DIP)") :
        (b_EnglishMenu ? L"Size (DIP)" : L"サイズ (DIP)"));
    ext_set_text(dialog, IDC_EXT_SECONDS, ext_is_digital(mode) ?
        (b_EnglishMenu ? L"Seconds" : L"秒を表示") :
        (b_EnglishMenu ? L"Second hand" : L"秒針を表示"));
    if (ext_is_digital(mode)) {
        int index = mode == EXT_MODE_NIXIE;
        ext_check_control(dialog, IDC_EXT_COLON, page->digitalColon[index] ? BST_CHECKED : BST_UNCHECKED);
        ext_send_control(dialog, IDC_EXT_SIDE, CB_SETCURSEL, page->digitalStacked[index] ? 0 : 1, 0);
        ext_send_control(dialog, IDC_EXT_SPEED, TBM_SETPOS, TRUE, (page->digitalDuration[index]+25)/50);
        ext_set_text(dialog, IDC_EXT_SPEED_LABEL, mode == EXT_MODE_NIXIE ?
            (b_EnglishMenu ? L"Transition" : L"\u5207\u66ff\u6642\u9593") :
            (b_EnglishMenu ? L"Flip duration" : L"\u3081\u304f\u308b\u901f\u3055"));
        ext_update_speed(dialog);
    }
    ext_check_control(dialog, IDC_EXT_BASE, page->nixieBase ? BST_CHECKED : BST_UNCHECKED);
    ShowWindow(ext_get_control(dialog, IDC_EXT_BASE), mode == EXT_MODE_NIXIE ? SW_SHOW : SW_HIDE);
    page->currentMode = mode;
    ShowWindow(ext_get_control(dialog, IDC_EXT_COLON), ext_is_digital(mode) ? SW_SHOW : SW_HIDE);
    {
        const int ids[] = {IDC_EXT_SIDE, IDC_EXT_SIDE_LABEL, IDC_EXT_SPEED, IDC_EXT_SPEED_LABEL, IDC_EXT_SPEED_VALUE};
        for (i = 0; i < (int)_countof(ids); ++i)
            ShowWindow(ext_get_control(dialog, ids[i]), ext_is_digital(mode) ? SW_SHOW : SW_HIDE);
    }
    if (mode == EXT_MODE_LED) ext_layout_led(dialog, page, TRUE);
    if (mode == EXT_MODE_LED) {
        ext_set_text(dialog, IDC_EXT_SECONDS, b_EnglishMenu ? L"Seconds" : L"\u79d2\u3092\u8868\u793a");
    }
    ext_stop_preview(dialog, page);
    flp_reset(page->flipPreview);
    led_reset(page->ledPreview);
    page->initializing = FALSE;
}

static BOOL ext_load_preview(EXT_PAGE* page, int mode, int face)
{
    if (mode == EXT_MODE_LED) return page->ledPreview != NULL;
    if ((page->preview || page->flipPreview) && page->loadedMode == mode && page->loadedFace == face)
        return TRUE;
    ACS_CONTEXT* analog = NULL;
    FLP_CONTEXT* flip = NULL;
    if (mode == EXT_MODE_NIXIE) flip = flp_create_nixie(GetModuleHandleW(NULL));
    else if (mode == EXT_MODE_FLIP) flip = flp_create(GetModuleHandleW(NULL), face);
    else analog = acs_create(GetModuleHandleW(NULL), mode, face);
    if (!analog && !flip) return FALSE;
    acs_destroy(page->preview);
    flp_destroy(page->flipPreview);
    page->preview = analog;
    page->flipPreview = flip;
    page->loadedFace = face;
    page->loadedMode = mode;
    return TRUE;
}

static void ext_enable_controls(HWND dialog)
{
    BOOL enabled = ext_get_checked(dialog, IDC_EXT_ENABLE) == BST_CHECKED;
    int mode = ext_read_mode(dialog);
    ShowWindow(ext_get_control(dialog,IDC_EXT_DETACH),SW_SHOW);
    EnableWindow(ext_get_control(dialog,IDC_EXT_DETACH),enabled);
    const int ids[] = {IDC_EXT_MODE, IDC_EXT_SECONDS, IDC_EXT_SIZE, IDC_EXT_PLACE, IDC_EXT_X, IDC_EXT_Y};
    unsigned i;
    for (i = 0; i < _countof(ids); ++i) EnableWindow(ext_get_control(dialog, ids[i]), enabled);
    EnableWindow(ext_get_control(dialog, IDC_EXT_FACE), enabled && mode != EXT_MODE_CLASSIC && mode != EXT_MODE_LEGACY && mode != EXT_MODE_NIXIE);
    EnableWindow(ext_get_control(dialog, IDC_EXT_BASE), enabled && mode == EXT_MODE_NIXIE);
    EnableWindow(ext_get_control(dialog, IDC_EXT_COLON), enabled && ext_is_digital(mode));
    EnableWindow(ext_get_control(dialog, IDC_EXT_SIDE), enabled && ext_is_digital(mode));
    EnableWindow(ext_get_control(dialog, IDC_EXT_SPEED), enabled && ext_is_digital(mode));
    EnableWindow(GetDlgItem(dialog, IDC_EXT_LED_PANEL), enabled && mode == EXT_MODE_LED);
    EXT_PAGE* page=(EXT_PAGE*)GetWindowLongPtrW(dialog,DWLP_USER);
    if(page&&mode==EXT_MODE_LED)ext_enable_led(dialog,page,enabled);
}

static void ext_paint_preview(HWND dialog, EXT_PAGE* page, HDC dc, RECT rect)
{
    ACS_OPTIONS options;
    SYSTEMTIME time;
    int diameter, cross, margin, saved, x, y;
    RECT trayRect, slot;
    WCHAR label[96];
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    UINT dpi = acs_get_dpi(tray);
    BOOL vertical = FALSE;
    FillRect(dc, &rect, GetSysColorBrush(COLOR_WINDOW));
    if (!ext_read_options(dialog, &options, FALSE) || !ext_load_preview(page, options.mode, options.face)) {
        const WCHAR* error = b_EnglishMenu ? L"Clock images unavailable" : L"時計画像を読み込めません";
        ext_stop_preview(dialog, page);
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, error, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    cross = MulDiv(48, dpi, 96);
    if (tray && GetWindowRect(tray, &trayRect)) {
        cross = min(trayRect.right-trayRect.left, trayRect.bottom-trayRect.top);
        vertical = trayRect.bottom-trayRect.top > trayRect.right-trayRect.left;
    }
    GetLocalTime(&time);
    if (options.mode == EXT_MODE_LED) {
        LED_OPTIONS led; SIZE actual,shown;
        if (!ext_read_led(dialog,page,&led,FALSE)) { ext_stop_preview(dialog,page); return; }
        led_configure(page->ledPreview,&led);
        if (!led_get_size(options.diameter,cross,dpi,vertical,led_get_columns(page->ledPreview,&time),&actual)) return;
        shown=actual;
        double scale=min(1.0,min((rect.right-rect.left-4)/(double)actual.cx,(rect.bottom-rect.top-4)/(double)actual.cy));
        shown.cx=max(1,(LONG)(actual.cx*scale));shown.cy=max(1,(LONG)(actual.cy*scale));
        if(led_render_preview(page->ledPreview,shown,&time,GetTickCount64(),page->ledSection==2?page->ledMessage:-1,(page->ledSection==2&&page->ledPlaying)&&IsWindowVisible(dialog))){
            x=rect.left+(rect.right-rect.left-shown.cx)/2+MulDiv(options.offsetX,(int)(dpi*scale),96);
            y=rect.top+(rect.bottom-rect.top-shown.cy)/2+MulDiv(options.offsetY,(int)(dpi*scale),96);
            saved=SaveDC(dc);IntersectClipRect(dc,rect.left,rect.top,rect.right,rect.bottom);
            led_draw(page->ledPreview,dc,x,y);RestoreDC(dc,saved);
            if((page->ledSection==2&&page->ledPlaying)&&IsWindowVisible(dialog)&&led_is_active(page->ledPreview)){
                if(!page->animationTimer)page->animationTimer=SetTimer(dialog,2,LED_FRAME_MS,NULL)!=0;
            }else ext_stop_preview(dialog,page);
        }
        swprintf_s(label,_countof(label),b_EnglishMenu?L"Actual size: %ld x %ld px":L"\u5b9f\u969b\u306e\u30b5\u30a4\u30ba: %ld x %ld px",actual.cx,actual.cy);
    } else if (ext_is_digital(options.mode)) {
        SIZE size;
        int shown;
        diameter = flp_get_mode_height(options.mode, options.diameter, cross, dpi, vertical, options.flipStacked, options.seconds, options.colon);
        vertical = vertical && options.flipStacked;
        if (!diameter || !flp_get_mode_size(options.mode, diameter, vertical, options.seconds, options.colon, &size)) return;
        swprintf_s(label, _countof(label), b_EnglishMenu ? L"Actual size: %ld x %ld px" : L"実際のサイズ: %ld x %ld px", size.cx, size.cy);
        shown = diameter;
        while (shown > 1 && (size.cx > rect.right-rect.left-4 || size.cy > rect.bottom-rect.top-4)) {
            --shown;
            flp_get_mode_size(options.mode, shown, vertical, options.seconds, options.colon, &size);
        }
        slot.left = rect.left+(rect.right-rect.left-size.cx)/2;
        slot.top = rect.top+(rect.bottom-rect.top-size.cy)/2;
        slot.right = slot.left+size.cx; slot.bottom = slot.top+size.cy;
        x = slot.left+MulDiv(MulDiv(options.offsetX, dpi, 96), shown, diameter);
        y = slot.top+MulDiv(MulDiv(options.offsetY, dpi, 96), shown, diameter);
        flp_set_base(page->flipPreview, options.nixieBase);
        if (flp_render(page->flipPreview, shown, vertical, options.seconds, options.colon,
            &time, GetTickCount64(), TRUE, options.flipDuration)) {
            if (flp_is_active(page->flipPreview) && IsWindowVisible(dialog)) {
                if (!page->animationTimer) page->animationTimer = SetTimer(dialog, 2, 33, NULL) != 0;
                if (!page->animationTimer)
                    flp_render(page->flipPreview, shown, vertical, options.seconds, options.colon,
                        &time, GetTickCount64(), FALSE, options.flipDuration);
            } else ext_stop_preview(dialog, page);
            saved = SaveDC(dc);
            IntersectClipRect(dc, slot.left, slot.top, slot.right, slot.bottom);
            flp_draw(page->flipPreview, dc, x, y);
            RestoreDC(dc, saved);
        } else ext_stop_preview(dialog, page);
    } else {
        ext_stop_preview(dialog, page);
        diameter = acs_get_diameter(options.diameter, cross, dpi);
        swprintf_s(label, _countof(label), b_EnglishMenu ? L"Actual diameter: %d px" : L"実際の直径: %d px", diameter);
        if (!diameter) return;
        // Fit the preview while reporting the real taskbar diameter above.
        {
            int shown = min(diameter, min(rect.right-rect.left-4, rect.bottom-rect.top-4));
            if (shown < 4) return;
            margin = 1;
            slot.left = rect.left+(rect.right-rect.left-shown-margin*2)/2;
            slot.top = rect.top+(rect.bottom-rect.top-shown-margin*2)/2;
            slot.right = slot.left+shown+margin*2; slot.bottom = slot.top+shown+margin*2;
            x = slot.left+margin+MulDiv(MulDiv(options.offsetX, dpi, 96), shown, diameter);
            y = slot.top+margin+MulDiv(MulDiv(options.offsetY, dpi, 96), shown, diameter);
            saved = SaveDC(dc);
            IntersectClipRect(dc, slot.left, slot.top, slot.right, slot.bottom);
            acs_draw(page->preview, dc, x, y, shown, &time, options.seconds);
        }
        RestoreDC(dc, saved);
    }
    {
        WCHAR previous[96];
        ext_get_text(dialog, IDC_EXT_EFFECTIVE, previous, _countof(previous));
        if (lstrcmpW(previous, label)) ext_set_text(dialog, IDC_EXT_EFFECTIVE, label);
    }
}

static void ext_store_size(const WCHAR* text, const char* key)
{
    WCHAR* end;
    long value;
    errno = 0;
    value = wcstol(text, &end, 10);
    if (text[0] && !*end && errno != ERANGE && (value == 0 || (value >= 16 && value <= 256)))
        SetMyRegLong("ExtendedDisplay", key, value);
}

static void ext_update_speed(HWND dialog)
{
    WCHAR text[32];
    int duration = (int)ext_send_control(dialog, IDC_EXT_SPEED, TBM_GETPOS, 0, 0)*50;
    swprintf_s(text, _countof(text), b_EnglishMenu ? L"%.2f s" : L"%.2f \u79d2", duration/1000.0);
    ext_set_text(dialog, IDC_EXT_SPEED_VALUE, text);
}

INT_PTR CALLBACK PageExtendedProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    EXT_PAGE* page = (EXT_PAGE*)GetWindowLongPtrW(dialog, DWLP_USER);
    ACS_OPTIONS options;
    switch (message) {
    case WM_INITDIALOG:
    {
        char skin[64];
        int mode = EXT_MODE_NORMAL, i;
        page = (EXT_PAGE*)calloc(1, sizeof(*page));
        if (!page) return FALSE;
        page->initializing = TRUE;
        page->loadedFace = page->loadedMode = page->currentMode = -1;
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)page);
        ext_add_mode(dialog, EXT_MODE_NORMAL, (b_EnglishMenu ? L"Clock (Normal)" : L"\u6642\u8a08\uff08\u901a\u5e38\uff09"));
        ext_add_mode(dialog, EXT_MODE_CLASSIC, (b_EnglishMenu ? L"Clock (Classic)" : L"\u6642\u8a08\uff08\u30af\u30e9\u30b7\u30c3\u30af\uff09"));
        ext_add_mode(dialog, EXT_MODE_FLIP, (b_EnglishMenu ? L"Flip Clock" : L"パラパラ時計"));
        ext_add_mode(dialog, EXT_MODE_NIXIE, (b_EnglishMenu ? L"Nixie Clock" : L"\u30cb\u30ad\u30b7\u30fc\u7ba1\u6642\u8a08"));
        ext_add_mode(dialog, EXT_MODE_LED, b_EnglishMenu ? L"LED Display" : L"\u96fb\u5149\u63b2\u793a\u677f");
        // Product rule: always append Legacy last; add future modes above this entry.
        ext_add_mode(dialog, EXT_MODE_LEGACY, (b_EnglishMenu ? L"Legacy Analog Clock" : L"\u65e7\u30a2\u30ca\u30ed\u30b0\u6642\u8a08"));
        GetMyRegStr("ExtendedDisplay", "Mode", skin, sizeof(skin), "Normal");
        if (!strcmp(skin, "Classic")) mode = EXT_MODE_CLASSIC;
        else if (!strcmp(skin, "Flip")) mode = EXT_MODE_FLIP;
        else if (!strcmp(skin, "Legacy")) mode = EXT_MODE_LEGACY;
        else if (!strcmp(skin, "Nixie")) mode = EXT_MODE_NIXIE;
        else if (!strcmp(skin, "LED")) mode = EXT_MODE_LED;
        GetMyRegStr("ExtendedDisplay", "SkinId", skin, sizeof(skin), "metal-arabic");
        // Retired Roman settings use the default Arabic face; tick IDs remain stable.
        if (!strcmp(skin, "metal-ticks")) page->normalFace = 1;
        GetMyRegStr("ExtendedDisplay", "FlipTheme", skin, sizeof(skin), "White");
        page->flipFace = !strcmp(skin, "Black");
        swprintf_s(page->normalSize, _countof(page->normalSize), L"%ld", GetMyRegLong("ExtendedDisplay", "DiameterDip", 0));
        swprintf_s(page->flipSize, _countof(page->flipSize), L"%ld", GetMyRegLong("ExtendedDisplay", "FlipHeightDip", 0));
        swprintf_s(page->legacySize, _countof(page->legacySize), L"%ld", GetMyRegLong("ExtendedDisplay", "LegacyDiameterDip", 0));
        swprintf_s(page->ledSize, _countof(page->ledSize), L"%ld", GetMyRegLong("ExtendedDisplay", "LedHeightDip", 0));
        ext_init_led(dialog, page);
        swprintf_s(page->nixieSize, _countof(page->nixieSize), L"%ld", GetMyRegLong("ExtendedDisplay", "NixieHeightDip", 0));
        page->nixieBase = GetMyRegLong("ExtendedDisplay", "NixieShowBase", 1) != 0;
        for (i = 0; i < 2; ++i) {
            page->digitalColon[i] = GetMyRegLong("ExtendedDisplay", i ? "NixieShowColon" : "FlipShowColon", 1) != 0;
            page->digitalStacked[i] = GetMyRegLong("ExtendedDisplay", i ? "NixieSideStacked" : "FlipSideStacked", 1) != 0;
            page->digitalDuration[i] = (int)GetMyRegLong("ExtendedDisplay", i ? "NixieDurationMs" : "FlipDurationMs", i ? 150 : 300);
            if (page->digitalDuration[i] < 100 || page->digitalDuration[i] > 900) page->digitalDuration[i] = i ? 150 : 300;
        }
        ext_select_mode(dialog, mode);
        ext_send_control(dialog, IDC_EXT_PLACE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"Left / above" : L"左 / 上"));
        ext_send_control(dialog, IDC_EXT_PLACE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"Right / below" : L"右 / 下"));
        GetMyRegStr("ExtendedDisplay", "Placement", skin, sizeof(skin), "Left");
        ext_send_control(dialog, IDC_EXT_PLACE, CB_SETCURSEL, !strcmp(skin, "Right"), 0);
        ext_check_control(dialog, IDC_EXT_ENABLE, GetMyRegLong("ExtendedDisplay", "Enabled", 0) ? BST_CHECKED : BST_UNCHECKED);
        ext_check_control(dialog, IDC_EXT_DETACH, GetMyRegLong("ExtendedDisplay", "DetachEnabled", 0) ? BST_CHECKED : BST_UNCHECKED);
        ext_check_control(dialog, IDC_EXT_SECONDS, GetMyRegLong("ExtendedDisplay", "ShowSeconds", 1) ? BST_CHECKED : BST_UNCHECKED);
        ext_check_control(dialog, IDC_EXT_COLON, GetMyRegLong("ExtendedDisplay", "FlipShowColon", 1) ? BST_CHECKED : BST_UNCHECKED);
        ext_send_control(dialog, IDC_EXT_SIDE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"Stacked" : L"\u7e26\u4e26\u3073"));
        ext_send_control(dialog, IDC_EXT_SIDE, CB_ADDSTRING, 0, (LPARAM)(b_EnglishMenu ? L"Inline (same as top/bottom)" : L"\u6a2a\u4e26\u3073\uff08\u4e0a\u4e0b\u3068\u540c\u3058\uff09"));
        ext_send_control(dialog, IDC_EXT_SIDE, CB_SETCURSEL, GetMyRegLong("ExtendedDisplay", "FlipSideStacked", 1) ? 0 : 1, 0);
        ext_send_control(dialog, IDC_EXT_SPEED, TBM_SETRANGE, FALSE, MAKELPARAM(2, 18));
        ext_send_control(dialog, IDC_EXT_SPEED, TBM_SETPAGESIZE, 0, 2);
        {
            int duration = (int)GetMyRegLong("ExtendedDisplay", "FlipDurationMs", 300);
            if (duration < 100 || duration > 900) duration = 300;
            ext_send_control(dialog, IDC_EXT_SPEED, TBM_SETPOS, TRUE, (duration+25)/50);
        }
        ext_update_speed(dialog);
        ext_set_number(dialog, IDC_EXT_X, GetMyRegLong("ExtendedDisplay", "OffsetXDip", 0), TRUE);
        ext_set_number(dialog, IDC_EXT_Y, GetMyRegLong("ExtendedDisplay", "OffsetYDip", 0), TRUE);
        for (i = IDC_EXT_SIZE; i <= IDC_EXT_Y; ++i) ext_send_control(dialog, i, EM_SETLIMITTEXT, 6, 0);
        ext_switch_mode(dialog, page, mode);
        ext_load_preview(page, mode, mode == EXT_MODE_NIXIE ? 0 : mode == EXT_MODE_FLIP ? page->flipFace : page->normalFace);
        ext_enable_controls(dialog);
        page->initializing = FALSE;
        return TRUE;
    }
    case WM_VSCROLL:
        if(lParam)break;
        // Match the Tooltip page: the page owns its right-edge scrollbar.
        if(page&&page->currentMode==EXT_MODE_LED){SendMessageW(page->ledPanel,message,wParam,lParam);return TRUE;}
        break;
    case WM_MOUSEWHEEL:
        if(page&&page->currentMode==EXT_MODE_LED){SendMessageW(page->ledPanel,message,wParam,lParam);return TRUE;}
        break;
    case ACS_VALIDATE:
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, FALSE);
        if (!page || !ext_read_options(dialog, &options, TRUE)) return TRUE;
        if (options.mode == EXT_MODE_LED && !ext_read_led(dialog,page,&page->ledOptions,TRUE)) return TRUE;
        if (options.enabled && !ext_load_preview(page, options.mode, options.face)) {
            MessageBoxW(dialog, b_EnglishMenu ? L"Cannot load the selected clock skin. Check the bundled images and skin.ini." :
                L"clock-skins を読み込めません。画像と skin.ini を確認してください。",
                b_EnglishMenu ? L"Extended Display" : L"拡張表示", MB_OK | MB_ICONERROR);
            return TRUE;
        }
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
        return TRUE;
    case WM_COMMAND:
        if (page && !page->initializing &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE)) {
            if (LOWORD(wParam) == IDC_EXT_MODE && HIWORD(wParam) == CBN_SELCHANGE) {
                int mode = ext_read_mode(dialog);
                if (mode != page->currentMode) ext_switch_mode(dialog, page, mode);
            }
            int id=LOWORD(wParam);
            if(id==IDC_LED_PLAY||(id>=IDC_LED_MESSAGE_FIRST&&id<IDC_LED_MESSAGE_FIRST+3)){
                if(id==IDC_LED_PLAY)page->ledPlaying=!page->ledPlaying;
                else {page->ledMessage=id-IDC_LED_MESSAGE_FIRST;page->ledPlaying=FALSE;}
                led_reset(page->ledPreview);ext_layout_led(dialog,page,TRUE);
                ext_enable_controls(dialog);InvalidateRect(ext_get_control(dialog,IDC_EXT_PREVIEW),NULL,FALSE);return TRUE;
            }
            page->ledPlaying=FALSE;
            if (LOWORD(wParam)>=IDC_LED_DETAILS && LOWORD(wParam)<=IDC_LED_TEXT_FIRST+23) page->ledDirty=TRUE;
            if(ext_read_mode(dialog)==EXT_MODE_LED)ext_layout_led(dialog,page,TRUE);
            ext_stop_preview(dialog, page);
            led_reset(page->ledPreview);
            flp_reset(page->flipPreview);
            ext_enable_controls(dialog);
            InvalidateRect(ext_get_control(dialog, IDC_EXT_PREVIEW), NULL, FALSE);
            SendMessageW(GetParent(dialog), PSM_CHANGED, (WPARAM)dialog, 0);
        }
        return TRUE;
    case WM_HSCROLL:
        if (page && !page->initializing && (HWND)lParam == ext_get_control(dialog, IDC_EXT_SPEED)) {
            ext_update_speed(dialog);
            InvalidateRect(ext_get_control(dialog, IDC_EXT_PREVIEW), NULL, FALSE);
            SendMessageW(GetParent(dialog), PSM_CHANGED, (WPARAM)dialog, 0);
        }
        return TRUE;
    case WM_SHOWWINDOW:
        if (wParam) {
            if(page && page->currentMode==EXT_MODE_LED) ext_layout_led(dialog,page,TRUE);
            flp_reset(page ? page->flipPreview : NULL); SetTimer(dialog, 1, 1000, NULL);
        }
        else { KillTimer(dialog, 1); ext_stop_preview(dialog, page); flp_reset(page ? page->flipPreview : NULL); }
        return TRUE;
    case WM_TIMER:
        if (wParam == 1 || wParam == 2)
            InvalidateRect(ext_get_control(dialog, IDC_EXT_PREVIEW), NULL, FALSE);
        return TRUE;
    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* item = (DRAWITEMSTRUCT*)lParam;
        HDC memory;
        HBITMAP bitmap;
        HGDIOBJ previous;
        RECT rect;
        int width, height;
        if (!page || item->CtlID != IDC_EXT_PREVIEW) break;
        width = item->rcItem.right-item->rcItem.left;
        height = item->rcItem.bottom-item->rcItem.top;
        if (width <= 0 || height <= 0) return TRUE;
        memory = CreateCompatibleDC(item->hDC);
        bitmap = CreateCompatibleBitmap(item->hDC, width, height);
        if (memory && bitmap) {
            previous = SelectObject(memory, bitmap);
            SetRect(&rect, 0, 0, width, height);
            ext_paint_preview(dialog, page, memory, rect);
            // Present a complete frame; the background clear never reaches the visible control.
            BitBlt(item->hDC, item->rcItem.left, item->rcItem.top, width, height,
                memory, 0, 0, SRCCOPY);
            SelectObject(memory, previous);
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        return TRUE;
    }

    case WM_NOTIFY:
        if(page&&((NMHDR*)lParam)->idFrom==IDC_LED_TABS&&((NMHDR*)lParam)->code==TCN_SELCHANGE){
            page->ledSection=TabCtrl_GetCurSel(page->ledTabs);page->ledPlaying=FALSE;
            led_reset(page->ledPreview);ext_stop_preview(dialog,page);ext_layout_led(dialog,page,TRUE);
            ext_enable_controls(dialog);InvalidateRect(ext_get_control(dialog,IDC_EXT_PREVIEW),NULL,FALSE);return TRUE;
        }
        if (page && ((NMHDR*)lParam)->code == PSN_APPLY && ext_read_options(dialog, &options, FALSE)) {
            const char* skins[] = {"metal-arabic", "metal-ticks"};
            const char* modes[] = {"Normal", "Classic", "Flip", "Legacy", "Nixie", "LED"};
            LED_OPTIONS led = {0};
            BOOL saveLed=options.mode==EXT_MODE_LED || page->ledDirty;
            if(saveLed && !ext_read_led(dialog,page,&led,options.mode==EXT_MODE_LED)){SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE);return TRUE;}
            if(saveLed && !led_store(&led,SetMyRegLong,SetMyRegStr)){
                MessageBoxW(dialog,b_EnglishMenu?L"Cannot save LED settings.":L"\u96fb\u5149\u63b2\u793a\u677f\u306e\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002",L"TClock",MB_OK|MB_ICONERROR);
                SetWindowLongPtrW(dialog,DWLP_MSGRESULT,PSNRET_INVALID_NOCHANGEPAGE);return TRUE;
            }
            if(saveLed){page->ledOptions=led;page->ledDirty=FALSE;}
            if (options.mode == EXT_MODE_NORMAL) page->normalFace = options.face;
            if (options.mode == EXT_MODE_FLIP) page->flipFace = options.face;
            ext_get_text(dialog, IDC_EXT_SIZE, ext_get_size(page, options.mode), 32);
            ext_keep_digital(dialog, page, options.mode);
            // The independent analog path is retired; retain its other historical values.
            SetMyRegLong("AnalogClock", "UseAnalogClock", FALSE);
            SetMyRegLong("ExtendedDisplay", "Enabled", options.enabled);
            SetMyRegLong("ExtendedDisplay", "DetachEnabled", ext_get_checked(dialog,IDC_EXT_DETACH)==BST_CHECKED);
            SetMyRegStr("ExtendedDisplay", "Kind", "ImageClock");
            SetMyRegStr("ExtendedDisplay", "Mode", modes[options.mode]);
            {
                SetMyRegStr("ExtendedDisplay", "SkinId", skins[page->normalFace]);
                SetMyRegStr("ExtendedDisplay", "FlipTheme", page->flipFace ? "Black" : "White");
                ext_store_size(page->normalSize, "DiameterDip");
                ext_store_size(page->flipSize, "FlipHeightDip");
                ext_store_size(page->legacySize, "LegacyDiameterDip");
                ext_store_size(page->nixieSize, "NixieHeightDip");
                ext_store_size(page->ledSize, "LedHeightDip");
                SetMyRegStr("ExtendedDisplay", "LegacySkinId", "legacy-default");
                SetMyRegStr("ExtendedDisplay", "Placement", options.trailing ? "Right" : "Left");
                SetMyRegLong("ExtendedDisplay", "OffsetXDip", options.offsetX);
                SetMyRegLong("ExtendedDisplay", "OffsetYDip", options.offsetY);
                SetMyRegLong("ExtendedDisplay", "ShowSeconds", options.seconds);
                SetMyRegLong("ExtendedDisplay", "FlipShowColon", page->digitalColon[0]);
                SetMyRegLong("ExtendedDisplay", "FlipSideStacked", page->digitalStacked[0]);
                SetMyRegLong("ExtendedDisplay", "FlipDurationMs", page->digitalDuration[0]);
                SetMyRegLong("ExtendedDisplay", "NixieShowBase", page->nixieBase);
                SetMyRegLong("ExtendedDisplay", "NixieShowColon", page->digitalColon[1]);
                SetMyRegLong("ExtendedDisplay", "NixieSideStacked", page->digitalStacked[1]);
                SetMyRegLong("ExtendedDisplay", "NixieDurationMs", page->digitalDuration[1]);
            }
            g_bApplyClock = TRUE;
        }
        return TRUE;
    case WM_DESTROY:
        KillTimer(dialog, 1);
        ext_stop_preview(dialog, page);
        if (page) {
            led_destroy(page->ledPreview);
            acs_destroy(page->preview); flp_destroy(page->flipPreview);
            free(page); SetWindowLongPtrW(dialog, DWLP_USER, 0);
        }
        return TRUE;
    }
    return FALSE;
}
