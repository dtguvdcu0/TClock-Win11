/*-------------------------------------------
  pagecustomvars.c
  "Custom Format" page for [CustomVars]
---------------------------------------------*/

#include "tclock.h"
#include "..\common\text_codec.h"

#define CV_N_MIN 1
#define CV_N_MAX 32

#define CV_MODE_LINE 0
#define CV_MODE_JSON 1

#define CV_EXEC_TYPE_COMMAND 0
#define CV_EXEC_TYPE_SHELL 1

#define CV_EXEC_START_STARTUP 0
#define CV_EXEC_START_INTERVAL 1
#define CV_EXEC_START_BOTH 2
#define CV_EXEC_START_TIME 3

typedef struct {
    char path[1024];
    int refreshSec;
    int maxChars;
    char failValue[256];
    char whitespace[32];
    int mode;
    char jsonDefault[256];
    int jsonNullAsEmpty;
    char jsonValue[4096];
    int execEnable;
    int execType;
    int execStart;
    int execIntervalSec;
    char execTime[16];
    char execCommand[1024];
} CV_ITEM;

static int g_selectedN = 1;

static void cv_send_ps_changed(HWND hDlg);

static void cv_normalize_utf8_in_place_no_writeback(char* value, int valueBytes)
{
    WCHAR wbuf[1024];
    char utf8[1024];
    if (!value || valueBytes <= 0 || value[0] == '\0') return;
    if (tc_utf8_to_utf16(value, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
    if (tc_utf16_to_utf8(wbuf, utf8, (int)sizeof(utf8)) <= 0) return;
    lstrcpyn(value, utf8, valueBytes);
}

static void cv_browse_item_path(HWND hDlg)
{
    char filter[80];
    char deffile[1024];
    char fname[1024];

    filter[0] = filter[1] = 0;
    str0cat(filter, MyStringUTF8(IDS_ALLFILE));
    str0cat(filter, "*.*");

    GetDlgItemTextUTF8(hDlg, IDC_CV_ITEM_PATH, deffile, (int)sizeof(deffile));
    if (!SelectMyFileUTF8(hDlg, filter, 0, deffile, fname, (int)sizeof(fname))) {
        return;
    }

    cv_normalize_utf8_in_place_no_writeback(fname, (int)sizeof(fname));
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_ITEM_PATH, fname);
    PostMessage(hDlg, WM_NEXTDLGCTL, 1, FALSE);
    cv_send_ps_changed(hDlg);
}

static void cv_send_ps_changed(HWND hDlg)
{
    g_bApplyClock = TRUE;
    SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)hDlg, 0);
}

static int cv_clamp_int(int v, int minv, int maxv)
{
    if (v < minv) return minv;
    if (v > maxv) return maxv;
    return v;
}

static void cv_build_key(int n, const char* suffix, char* out, int outBytes)
{
    if (!out || outBytes <= 0) return;
    wsprintf(out, "Custom%d%s", n, suffix);
}

static void cv_get_reg_str(const char* key, char* out, int outBytes, const char* defv)
{
    if (!out || outBytes <= 0) return;
    if (GetMyRegStr("CustomVars", key, out, outBytes, defv ? defv : "") <= 0) {
        lstrcpyn(out, defv ? defv : "", outBytes);
    }
}

static int cv_mode_from_str(const char* s)
{
    if (s && lstrcmpi(s, "json") == 0) return CV_MODE_JSON;
    return CV_MODE_LINE;
}

static const char* cv_mode_to_str(int mode)
{
    return (mode == CV_MODE_JSON) ? "json" : "line";
}

static int cv_exec_type_from_str(const char* s)
{
    if (s && lstrcmpi(s, "shell") == 0) return CV_EXEC_TYPE_SHELL;
    return CV_EXEC_TYPE_COMMAND;
}

static const char* cv_exec_type_to_str(int v)
{
    return (v == CV_EXEC_TYPE_SHELL) ? "shell" : "command";
}

static int cv_exec_start_from_str(const char* s)
{
    if (s && lstrcmpi(s, "startup") == 0) return CV_EXEC_START_STARTUP;
    if (s && lstrcmpi(s, "interval") == 0) return CV_EXEC_START_INTERVAL;
    if (s && lstrcmpi(s, "both") == 0) return CV_EXEC_START_BOTH;
    if (s && lstrcmpi(s, "time") == 0) return CV_EXEC_START_TIME;
    return CV_EXEC_START_INTERVAL;
}

static const char* cv_exec_start_to_str(int v)
{
    if (v == CV_EXEC_START_STARTUP) return "startup";
    if (v == CV_EXEC_START_BOTH) return "both";
    if (v == CV_EXEC_START_TIME) return "time";
    return "interval";
}

static int cv_whitespace_to_sel(const char* v)
{
    return (v && lstrcmpi(v, "trim_edges") == 0) ? 1 : 0;
}

static const char* cv_whitespace_from_sel(int sel)
{
    return (sel == 1) ? "trim_edges" : "keep";
}

static int cv_combo_find_text(HWND hDlg, int id, const char* text)
{
    int i, count;
    char buf[128];
    count = CBGetCount(hDlg, id);
    for (i = 0; i < count; ++i) {
        CBGetLBText(hDlg, id, i, buf);
        if (lstrcmpi(buf, text) == 0) return i;
    }
    return 0;
}

static void cv_fill_combo_defaults(HWND hDlg)
{
    CBResetContent(hDlg, IDC_CV_GLOBAL_WHITESPACE);
    CBAddString(hDlg, IDC_CV_GLOBAL_WHITESPACE, (LPARAM)"off");
    CBAddString(hDlg, IDC_CV_GLOBAL_WHITESPACE, (LPARAM)"on");

    CBResetContent(hDlg, IDC_CV_ITEM_WHITESPACE);
    CBAddString(hDlg, IDC_CV_ITEM_WHITESPACE, (LPARAM)"off");
    CBAddString(hDlg, IDC_CV_ITEM_WHITESPACE, (LPARAM)"on");

    CBResetContent(hDlg, IDC_CV_ITEM_MODE);
    CBAddString(hDlg, IDC_CV_ITEM_MODE, (LPARAM)"line");
    CBAddString(hDlg, IDC_CV_ITEM_MODE, (LPARAM)"json");

    CBResetContent(hDlg, IDC_CV_EXEC_TYPE);
    CBAddString(hDlg, IDC_CV_EXEC_TYPE, (LPARAM)"shell");
    CBAddString(hDlg, IDC_CV_EXEC_TYPE, (LPARAM)"command");

    CBResetContent(hDlg, IDC_CV_EXEC_START);
    CBAddString(hDlg, IDC_CV_EXEC_START, (LPARAM)"startup");
    CBAddString(hDlg, IDC_CV_EXEC_START, (LPARAM)"interval");
    CBAddString(hDlg, IDC_CV_EXEC_START, (LPARAM)"both");
    CBAddString(hDlg, IDC_CV_EXEC_START, (LPARAM)"time");

    CBResetContent(hDlg, IDC_CV_SELECT_N);
    {
        int i;
        char label[32];
        for (i = CV_N_MIN; i <= CV_N_MAX; ++i) {
            wsprintf(label, "Custom%d", i);
            CBAddString(hDlg, IDC_CV_SELECT_N, (LPARAM)label);
        }
    }
}

static void cv_read_global(HWND hDlg)
{
    int v;
    char s[256];

    v = (int)GetMyRegLong("CustomVars", "RefreshSec", 300);
    SetDlgItemInt(hDlg, IDC_CV_GLOBAL_REFRESH, (UINT)cv_clamp_int(v, 1, 86400), FALSE);

    v = (int)GetMyRegLong("CustomVars", "MaxChars", 20);
    SetDlgItemInt(hDlg, IDC_CV_GLOBAL_MAXCHARS, (UINT)cv_clamp_int(v, 1, 4096), FALSE);

    cv_get_reg_str("FailValue", s, (int)sizeof(s), "N/A");
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_GLOBAL_FAILVALUE, s);

    cv_get_reg_str("Whitespace", s, (int)sizeof(s), "trim_edges");
    CBSetCurSel(hDlg, IDC_CV_GLOBAL_WHITESPACE, cv_whitespace_to_sel(s));

}

static void cv_read_item(int n, CV_ITEM* it)
{
    char key[64];
    char tmp[64];
    if (!it) return;
    ZeroMemory(it, sizeof(*it));

    cv_build_key(n, "Path", key, (int)sizeof(key));
    cv_get_reg_str(key, it->path, (int)sizeof(it->path), "");

    cv_build_key(n, "RefreshSec", key, (int)sizeof(key));
    it->refreshSec = cv_clamp_int((int)GetMyRegLong("CustomVars", key, 300), 1, 86400);

    cv_build_key(n, "MaxChars", key, (int)sizeof(key));
    it->maxChars = cv_clamp_int((int)GetMyRegLong("CustomVars", key, 20), 1, 4096);

    cv_build_key(n, "FailValue", key, (int)sizeof(key));
    cv_get_reg_str(key, it->failValue, (int)sizeof(it->failValue), "N/A");

    cv_build_key(n, "Whitespace", key, (int)sizeof(key));
    cv_get_reg_str(key, it->whitespace, (int)sizeof(it->whitespace), "trim_edges");

    cv_build_key(n, "Mode", key, (int)sizeof(key));
    cv_get_reg_str(key, tmp, (int)sizeof(tmp), "line");
    it->mode = cv_mode_from_str(tmp);

    cv_build_key(n, "JsonDefault", key, (int)sizeof(key));
    cv_get_reg_str(key, it->jsonDefault, (int)sizeof(it->jsonDefault), "");

    cv_build_key(n, "JsonNullAsEmpty", key, (int)sizeof(key));
    it->jsonNullAsEmpty = GetMyRegLong("CustomVars", key, 1) ? 1 : 0;

    cv_build_key(n, "JsonValue", key, (int)sizeof(key));
    cv_get_reg_str(key, it->jsonValue, (int)sizeof(it->jsonValue), "");

    cv_build_key(n, "ExecEnable", key, (int)sizeof(key));
    it->execEnable = GetMyRegLong("CustomVars", key, 0) ? 1 : 0;

    cv_build_key(n, "ExecType", key, (int)sizeof(key));
    cv_get_reg_str(key, tmp, (int)sizeof(tmp), "command");
    it->execType = cv_exec_type_from_str(tmp);

    cv_build_key(n, "ExecStart", key, (int)sizeof(key));
    cv_get_reg_str(key, tmp, (int)sizeof(tmp), "interval");
    it->execStart = cv_exec_start_from_str(tmp);

    cv_build_key(n, "ExecIntervalSec", key, (int)sizeof(key));
    it->execIntervalSec = cv_clamp_int((int)GetMyRegLong("CustomVars", key, 60), 1, 86400);

    cv_build_key(n, "ExecTime", key, (int)sizeof(key));
    cv_get_reg_str(key, it->execTime, (int)sizeof(it->execTime), "00:00");

    cv_build_key(n, "ExecCommand", key, (int)sizeof(key));
    cv_get_reg_str(key, it->execCommand, (int)sizeof(it->execCommand), "");
}

static void cv_set_json_visibility(HWND hDlg, int mode)
{
    const int isJson = (mode == CV_MODE_JSON);
    ShowDlgItem(hDlg, IDC_CV_LBL_JSON_VALUE, isJson);
    ShowDlgItem(hDlg, IDC_CV_JSON_VALUE, isJson);
}

static void cv_set_exec_controls_enabled(HWND hDlg, int enabled)
{
    EnableDlgItem(hDlg, IDC_CV_EXEC_TYPE, enabled);
    EnableDlgItem(hDlg, IDC_CV_EXEC_START, enabled);
    EnableDlgItem(hDlg, IDC_CV_EXEC_INTERVAL, enabled);
    EnableDlgItem(hDlg, IDC_CV_SPIN_EXEC_INTERVAL, enabled);
    EnableDlgItem(hDlg, IDC_CV_EXEC_TIME, enabled);
    EnableDlgItem(hDlg, IDC_CV_EXEC_COMMAND, enabled);
}

static void cv_fill_item_controls(HWND hDlg, const CV_ITEM* it)
{
    if (!it) return;
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_ITEM_PATH, it->path);
    SetDlgItemInt(hDlg, IDC_CV_ITEM_REFRESH, (UINT)it->refreshSec, FALSE);
    SetDlgItemInt(hDlg, IDC_CV_ITEM_MAXCHARS, (UINT)it->maxChars, FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_ITEM_FAILVALUE, it->failValue);
    CBSetCurSel(hDlg, IDC_CV_ITEM_WHITESPACE, cv_whitespace_to_sel(it->whitespace));
    CBSetCurSel(hDlg, IDC_CV_ITEM_MODE, it->mode == CV_MODE_JSON ? 1 : 0);

    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_JSON_VALUE, it->jsonValue);

    CheckDlgButton(hDlg, IDC_CV_EXEC_ENABLE, it->execEnable ? BST_CHECKED : BST_UNCHECKED);
    CBSetCurSel(hDlg, IDC_CV_EXEC_TYPE, it->execType == CV_EXEC_TYPE_SHELL ? 0 : 1);
    CBSetCurSel(hDlg, IDC_CV_EXEC_START, cv_clamp_int(it->execStart, 0, 3));
    SetDlgItemInt(hDlg, IDC_CV_EXEC_INTERVAL, (UINT)it->execIntervalSec, FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_EXEC_TIME, it->execTime);
    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_EXEC_COMMAND, it->execCommand);

    cv_set_exec_controls_enabled(hDlg, it->execEnable ? 1 : 0);
    cv_set_json_visibility(hDlg, it->mode);
}

static void cv_load_selected_item(HWND hDlg)
{
    CV_ITEM it;
    cv_read_item(g_selectedN, &it);
    cv_fill_item_controls(hDlg, &it);
}

static void cv_update_hint(HWND hDlg)
{
    int n = g_selectedN;
    char key[64];
    char v[1024];
    int configured = 0;

    cv_build_key(n, "Path", key, (int)sizeof(key));
    cv_get_reg_str(key, v, (int)sizeof(v), "");
    if (v[0]) configured = 1;

    if (!configured) {
        cv_build_key(n, "JsonValue", key, (int)sizeof(key));
        cv_get_reg_str(key, v, (int)sizeof(v), "");
        if (v[0]) configured = 1;
    }

    SetDlgItemTextUTF8Strict(hDlg, IDC_CV_HINT, configured ? "Status: configured" : "Status: empty");
}

static void cv_on_init(HWND hDlg)
{
    cv_fill_combo_defaults(hDlg);

    SendDlgItemMessage(hDlg, IDC_CV_SPIN_GLOBAL_REFRESH, UDM_SETRANGE, 0, MAKELONG(86400, 1));
    SendDlgItemMessage(hDlg, IDC_CV_SPIN_GLOBAL_MAXCHARS, UDM_SETRANGE, 0, MAKELONG(4096, 1));
    SendDlgItemMessage(hDlg, IDC_CV_SPIN_ITEM_REFRESH, UDM_SETRANGE, 0, MAKELONG(86400, 1));
    SendDlgItemMessage(hDlg, IDC_CV_SPIN_ITEM_MAXCHARS, UDM_SETRANGE, 0, MAKELONG(4096, 1));
    SendDlgItemMessage(hDlg, IDC_CV_SPIN_EXEC_INTERVAL, UDM_SETRANGE, 0, MAKELONG(86400, 1));

    cv_read_global(hDlg);
    g_selectedN = 1;
    CBSetCurSel(hDlg, IDC_CV_SELECT_N, 0);
    cv_load_selected_item(hDlg);
    cv_update_hint(hDlg);
}

static int cv_get_int(HWND hDlg, int id, int defv, int minv, int maxv)
{
    BOOL ok = FALSE;
    UINT u = GetDlgItemInt(hDlg, id, &ok, FALSE);
    int v = ok ? (int)u : defv;
    return cv_clamp_int(v, minv, maxv);
}

static void cv_get_combo_text(HWND hDlg, int id, char* out, int outBytes, const char* defv)
{
    int sel;
    if (!out || outBytes <= 0) return;
    sel = CBGetCurSel(hDlg, id);
    if (sel >= 0) {
        CBGetLBText(hDlg, id, sel, out);
        if (out[0]) return;
    }
    lstrcpyn(out, defv ? defv : "", outBytes);
}

static void cv_write_item(HWND hDlg, int n)
{
    char key[64];
    char s[4096];
    int mode;

    GetDlgItemTextUTF8(hDlg, IDC_CV_ITEM_PATH, s, (int)sizeof(s));
    cv_build_key(n, "Path", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, s);

    {
        int refresh = cv_get_int(hDlg, IDC_CV_ITEM_REFRESH, 300, 1, 86400);
        mode = CBGetCurSel(hDlg, IDC_CV_ITEM_MODE) == 1 ? CV_MODE_JSON : CV_MODE_LINE;
        if (mode == CV_MODE_JSON && refresh < 5) refresh = 5;
        cv_build_key(n, "RefreshSec", key, (int)sizeof(key));
        SetMyRegLong("CustomVars", key, (DWORD)refresh);
    }

    cv_build_key(n, "MaxChars", key, (int)sizeof(key));
    SetMyRegLong("CustomVars", key, (DWORD)cv_get_int(hDlg, IDC_CV_ITEM_MAXCHARS, 20, 1, 4096));

    GetDlgItemTextUTF8(hDlg, IDC_CV_ITEM_FAILVALUE, s, (int)sizeof(s));
    cv_build_key(n, "FailValue", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, s);

    cv_build_key(n, "Whitespace", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, cv_whitespace_from_sel(CBGetCurSel(hDlg, IDC_CV_ITEM_WHITESPACE)));

    cv_build_key(n, "Mode", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, cv_mode_to_str(mode));


    GetDlgItemTextUTF8(hDlg, IDC_CV_JSON_VALUE, s, (int)sizeof(s));
    cv_build_key(n, "JsonValue", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, s);

    cv_build_key(n, "ExecEnable", key, (int)sizeof(key));
    SetMyRegLong("CustomVars", key, IsDlgButtonChecked(hDlg, IDC_CV_EXEC_ENABLE) ? 1 : 0);

    cv_build_key(n, "ExecType", key, (int)sizeof(key));
    cv_get_combo_text(hDlg, IDC_CV_EXEC_TYPE, s, (int)sizeof(s), "command");
    SetMyRegStr("CustomVars", key, s);

    cv_build_key(n, "ExecStart", key, (int)sizeof(key));
    cv_get_combo_text(hDlg, IDC_CV_EXEC_START, s, (int)sizeof(s), "interval");
    SetMyRegStr("CustomVars", key, s);

    cv_build_key(n, "ExecIntervalSec", key, (int)sizeof(key));
    SetMyRegLong("CustomVars", key, (DWORD)cv_get_int(hDlg, IDC_CV_EXEC_INTERVAL, 60, 1, 86400));

    GetDlgItemTextUTF8(hDlg, IDC_CV_EXEC_TIME, s, (int)sizeof(s));
    cv_build_key(n, "ExecTime", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, s);

    GetDlgItemTextUTF8(hDlg, IDC_CV_EXEC_COMMAND, s, (int)sizeof(s));
    cv_build_key(n, "ExecCommand", key, (int)sizeof(key));
    SetMyRegStr("CustomVars", key, s);
}

static void cv_on_apply(HWND hDlg)
{
    SetMyRegLong("CustomVars", "RefreshSec", (DWORD)cv_get_int(hDlg, IDC_CV_GLOBAL_REFRESH, 300, 1, 86400));
    SetMyRegLong("CustomVars", "MaxChars", (DWORD)cv_get_int(hDlg, IDC_CV_GLOBAL_MAXCHARS, 20, 1, 4096));

    // Keep global FailValue/Whitespace runtime support, but do not overwrite from UI.
    // UI intentionally exposes only per-item Fail/Whitespace to avoid duplicate settings.

    cv_write_item(hDlg, g_selectedN);
}

BOOL CALLBACK PageCustomVarsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        cv_on_init(hDlg);
        return TRUE;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_CV_SELECT_N && code == CBN_SELCHANGE) {
            int sel = CBGetCurSel(hDlg, IDC_CV_SELECT_N);
            if (sel >= 0) {
                g_selectedN = sel + 1;
                cv_load_selected_item(hDlg);
                cv_update_hint(hDlg);
                cv_send_ps_changed(hDlg);
            }
            return TRUE;
        }

        if (id == IDC_CV_ITEM_PATH_BROWSE && code == BN_CLICKED) {
            cv_browse_item_path(hDlg);
            return TRUE;
        }

        if (id == IDC_CV_ITEM_MODE && code == CBN_SELCHANGE) {
            int mode = (CBGetCurSel(hDlg, IDC_CV_ITEM_MODE) == 1) ? CV_MODE_JSON : CV_MODE_LINE;
            cv_set_json_visibility(hDlg, mode);
            cv_send_ps_changed(hDlg);
            return TRUE;
        }

        switch (id) {
        case IDC_CV_EXEC_ENABLE:
            cv_set_exec_controls_enabled(hDlg, IsDlgButtonChecked(hDlg, IDC_CV_EXEC_ENABLE) == BST_CHECKED);
            cv_send_ps_changed(hDlg);
            return TRUE;
        default:
            if (code == EN_CHANGE || code == CBN_SELCHANGE) {
                cv_send_ps_changed(hDlg);
                return TRUE;
            }
            break;
        }
        break;
    }

    case WM_NOTIFY:
        switch (((NMHDR*)lParam)->code) {
        case PSN_APPLY:
            cv_on_apply(hDlg);
            return TRUE;
        case PSN_HELP:
            My2chHelp(GetParent(hDlg));
            return TRUE;
        }
        break;
    }

    return FALSE;
}
