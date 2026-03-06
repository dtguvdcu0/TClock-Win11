/*-------------------------------------------
  page_rclick_menu.c
  "Right Click Menu" page for [MenuCustom]
---------------------------------------------*/

#include "tclock.h"

#define RM_ITEM_MIN 1
#define RM_ITEM_MAX 64

extern BOOL b_EnglishMenu;
extern int Language_Offset;

static int g_rm_selectedN = 1;

static const char* rm_type_key_from_index(int idx);

static void rm_send_ps_changed(HWND hDlg)
{
    g_bApplyClock = TRUE;
    SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)hDlg, 0);
}

static int rm_clamp_int(int v, int minv, int maxv)
{
    if (v < minv) return minv;
    if (v > maxv) return maxv;
    return v;
}

static void rm_build_key(int n, const char* suffix, char* out, int outBytes)
{
    if (!out || outBytes <= 0) return;
    wsprintf(out, "Item%d%s", n, suffix);
}

static void rm_get_reg_str(const char* key, char* out, int outBytes, const char* defv)
{
    if (!out || outBytes <= 0) return;
    if (GetMyRegStr("MenuCustom", key, out, outBytes, defv ? defv : "") <= 0) {
        lstrcpyn(out, defv ? defv : "", outBytes);
    }
}

static int rm_combo_find_text(HWND hDlg, int id, const char* text)
{
    int i;
    int count;
    char buf[128];

    count = CBGetCount(hDlg, id);
    for (i = 0; i < count; ++i) {
        CBGetLBText(hDlg, id, i, buf);
        if (lstrcmpi(buf, text) == 0) return i;
    }
    return 0;
}

static const char* rm_action_key_from_index(int idx);
static int rm_action_index_from_key(const char* key);

static void rm_get_combo_text(HWND hDlg, int id, char* out, int outBytes, const char* defv)
{
    int sel;
    if (!out || outBytes <= 0) return;

    sel = CBGetCurSel(hDlg, id);
    if (sel >= 0) {
        if (id == IDC_RM_ITEM_TYPE) {
            lstrcpyn(out, rm_type_key_from_index(sel), outBytes);
            return;
        }
        if (id == IDC_RM_ITEM_ACTION) {
            lstrcpyn(out, rm_action_key_from_index(sel), outBytes);
            return;
        }
        CBGetLBText(hDlg, id, sel, out);
        if (out[0]) return;
    }

    GetDlgItemTextUTF8(hDlg, id, out, outBytes);
    if (out[0]) return;

    lstrcpyn(out, defv ? defv : "", outBytes);
}

static int rm_get_int(HWND hDlg, int id, int defv, int minv, int maxv)
{
    BOOL ok = FALSE;
    UINT u = GetDlgItemInt(hDlg, id, &ok, FALSE);
    int v = ok ? (int)u : defv;
    return rm_clamp_int(v, minv, maxv);
}


static int rm_is_english_ui(void)
{
    if (Language_Offset == LANGUAGE_OFFSET_ENGLISH) return 1;
    if (Language_Offset == LANGUAGE_OFFSET_JAPANESE) return 0;
    return b_EnglishMenu ? 1 : 0;
}

static int rm_is_alarm_type(const char* type)
{
    return (type && lstrcmpi(type, "alarm") == 0) ? 1 : 0;
}

static const char* rm_type_key_from_index(int idx)
{
    switch (idx) {
    case 0: return "builtin";
    case 1: return "shell";
    case 2: return "commandline";
    case 3: return "passive";
    case 4: return "separator";
    case 5: return "alarm";
    default: return "builtin";
    }
}

static int rm_type_index_from_key(const char* key)
{
    int i;
    if (!key || !key[0]) return 0;
    for (i = 0; i < 6; ++i) {
        if (lstrcmpi(key, rm_type_key_from_index(i)) == 0) return i;
    }
    return 0;
}

static const char* rm_type_label_from_index(int idx, int englishUi)
{
    switch (idx) {
    case 0: return englishUi ? "builtin" : "内蔵機能";
    case 1: return englishUi ? "shell" : "シェル";
    case 2: return englishUi ? "commandline" : "コマンド";
    case 3: return englishUi ? "passive" : "表示のみ";
    case 4: return englishUi ? "separator" : "区切り線";
    case 5: return englishUi ? "alarm" : "アラーム";
    default: return englishUi ? "builtin" : "内蔵機能";
    }
}

static int rm_hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int rm_decode_utf8_hex(const char* hex, char* out, int outBytes)
{
    int i;
    int n;
    if (!hex || !out || outBytes <= 0) return 0;
    out[0] = '\0';
    n = lstrlen(hex);
    if ((n & 1) != 0) return 0;
    if ((n / 2) + 1 > outBytes) return 0;
    for (i = 0; i < n / 2; ++i) {
        int hi = rm_hex_value((unsigned char)hex[i * 2]);
        int lo = rm_hex_value((unsigned char)hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (char)((hi << 4) | lo);
    }
    out[n / 2] = '\0';
    return 1;
}

static const char* rm_default_label_for_action(const char* action)
{
    if (!action || !action[0]) return "";
    if (lstrcmpi(action, "taskmgr") == 0) return MyStringUTF8(IDS_TASKMGR);
    if (lstrcmpi(action, "cmd") == 0) return MyStringUTF8(IDS_CMD);
    if (lstrcmpi(action, "alarm_clock") == 0) return MyStringUTF8(IDS_ALARM_CLOCK);
    if (lstrcmpi(action, "pullback") == 0) return MyStringUTF8(IDS_PULLBACK);
    if (lstrcmpi(action, "control_panel") == 0) return MyStringUTF8(IDS_CONTROLPNL);
    if (lstrcmpi(action, "power_options") == 0) return MyStringUTF8(IDS_POWERPNL);
    if (lstrcmpi(action, "network_connections") == 0) return MyStringUTF8(IDS_NETWORKPNL);
    if (lstrcmpi(action, "settings_home") == 0) return MyStringUTF8(IDS_SETTING);
    if (lstrcmpi(action, "settings_network") == 0) return MyStringUTF8(IDS_NETWORKSTG);
    if (lstrcmpi(action, "settings_datetime") == 0) return MyStringUTF8(IDS_PROPDATE);
    if (lstrcmpi(action, "remove_drive_dynamic") == 0) return MyStringUTF8(IDS_ABOUTRMVDRV);
    return action;
}

static const char* rm_action_key_from_index(int idx)
{
    switch (idx) {
    case 0: return "taskmgr";
    case 1: return "cmd";
    case 2: return "alarm_clock";
    case 3: return "pullback";
    case 4: return "control_panel";
    case 5: return "power_options";
    case 6: return "network_connections";
    case 7: return "settings_home";
    case 8: return "settings_network";
    case 9: return "settings_datetime";
    case 10: return "remove_drive_dynamic";
    default: return "taskmgr";
    }
}

static int rm_action_index_from_key(const char* key)
{
    int i;
    if (!key || !key[0]) return -1;
    for (i = 0; i < 11; ++i) {
        if (lstrcmpi(key, rm_action_key_from_index(i)) == 0) return i;
    }
    return -1;
}

static void rm_fill_builtin_action_combo(HWND hDlg)
{
    int i;
    CBResetContent(hDlg, IDC_RM_ITEM_ACTION);
    for (i = 0; i < 11; ++i) {
        CBAddStringUTF8Compat(hDlg, IDC_RM_ITEM_ACTION, rm_default_label_for_action(rm_action_key_from_index(i)));
    }
}
static void rm_fill_show_combo(HWND hDlg, int alarmMode)
{
    CBResetContent(hDlg, IDC_RM_ITEM_SHOW);
    if (alarmMode) {
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"0");
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"1");
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"2");
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"3");
    } else {
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"1");
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"3");
        CBAddString(hDlg, IDC_RM_ITEM_SHOW, (LPARAM)"7");
    }
}

static void rm_fill_combo_defaults(HWND hDlg)
{
    int i;
    int en = rm_is_english_ui();

    CBResetContent(hDlg, IDC_RM_ITEM_TYPE);
    for (i = 0; i < 6; ++i) {
        CBAddStringUTF8Compat(hDlg, IDC_RM_ITEM_TYPE, rm_type_label_from_index(i, en));
    }

    CBResetContent(hDlg, IDC_RM_ITEM_EXEC_TYPE);
    CBAddString(hDlg, IDC_RM_ITEM_EXEC_TYPE, (LPARAM)"(merged)");

    rm_fill_show_combo(hDlg, 0);
    rm_fill_builtin_action_combo(hDlg);
}


static void rm_set_alarm_extra_visible(HWND hDlg, int visible)
{
    int cmd = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_KEEP_OPEN), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_SOUND_LOOP), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LBL_IDLE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LABEL_IDLE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LBL_PAUSE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LABEL_PAUSE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LBL_DONE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_LABEL_DONE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_ALARM_SEC), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_ALARM_SEC), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_SPIN_ALARM_SEC), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_ALARM_SOUND_FILE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_ALARM_VOLUME), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_ALARM_VOLUME), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_SPIN_ALARM_VOLUME), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_ALARM_MESSAGE), cmd);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_ALARM_MESSAGE), cmd);

    EnableDlgItem(hDlg, IDC_RM_ALARM_KEEP_OPEN, visible);
    EnableDlgItem(hDlg, IDC_RM_ALARM_SOUND_LOOP, visible);
    EnableDlgItem(hDlg, IDC_RM_ALARM_LABEL_IDLE, visible);
    EnableDlgItem(hDlg, IDC_RM_ALARM_LABEL_PAUSE, visible);
    EnableDlgItem(hDlg, IDC_RM_ALARM_LABEL_DONE, visible);
    EnableDlgItem(hDlg, IDC_RM_ITEM_ALARM_SEC, visible);
    EnableDlgItem(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, visible);
    EnableDlgItem(hDlg, IDC_RM_ITEM_ALARM_VOLUME, visible);
    EnableDlgItem(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, visible);
}

static void rm_set_type_labels(HWND hDlg, const char* type)
{
    int en = rm_is_english_ui();

    if (rm_is_alarm_type(type)) {
        if (en) {
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ACTION, "AlarmMessage");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_EXEC, "(unused)");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_SHOW, "Notify");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_PARAM, "AlarmSec");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ARGS, "Volume");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_WORKDIR, "SoundFile");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_FORMAT, "Running");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_UPDATE, "UpdSec");
        } else {
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ACTION, "メッセージ");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_EXEC, "(未使用)");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_SHOW, "通知");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_PARAM, "初期秒");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ARGS, "音量");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_WORKDIR, "音声ファイル");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_FORMAT, "実行中");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_UPDATE, "更新秒");
        }
    } else {
        if (en) {
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ACTION, "Action");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_EXEC, "Exec");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_SHOW, "Show");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_PARAM, "Param");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ARGS, "Args");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_WORKDIR, "WorkDir");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_FORMAT, "DisplayLabel");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_UPDATE, "UpdSec");
        } else {
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ACTION, "アクション");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_EXEC, "実行種別");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_SHOW, "表示");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_PARAM, "パラメータ");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_ARGS, "引数");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_WORKDIR, "作業フォルダ");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_FORMAT, "表示ラベル");
            SetDlgItemTextUTF8Strict(hDlg, IDC_RM_LBL_LABEL_UPDATE, "更新秒");
        }
    }
}

static void rm_set_visible_enabled(HWND hDlg, int ctrlId, int visible)
{
    HWND h = GetDlgItem(hDlg, ctrlId);
    if (!h) return;
    ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
    EnableWindow(h, visible ? TRUE : FALSE);
}

static void rm_set_pair_visible_enabled(HWND hDlg, int labelId, int ctrlId, int visible)
{
    rm_set_visible_enabled(hDlg, labelId, visible);
    rm_set_visible_enabled(hDlg, ctrlId, visible);
}

static void rm_apply_type_ui_state(HWND hDlg)
{
    char mode[64];
    char curShow[64];
    int isSeparator;
    int isPassive;
    int isAlarm;
    int isBuiltin;
    int isShell;
    int isCommandline;
    int showShow;
    int showAction;
    int showCmdInputs;
    int showParam;
    int showArgs;
    int showWorkDir;
    int showDisplay;
    int showLabelUpdate;

    rm_get_combo_text(hDlg, IDC_RM_ITEM_TYPE, mode, (int)sizeof(mode), "builtin");
    rm_get_combo_text(hDlg, IDC_RM_ITEM_SHOW, curShow, (int)sizeof(curShow), "1");

    isSeparator = (lstrcmpi(mode, "separator") == 0);
    isPassive = (lstrcmpi(mode, "passive") == 0);
    isAlarm = rm_is_alarm_type(mode);
    isBuiltin = (lstrcmpi(mode, "builtin") == 0);
    isShell = (lstrcmpi(mode, "shell") == 0);
    isCommandline = (lstrcmpi(mode, "commandline") == 0);

    rm_set_type_labels(hDlg, mode);
    rm_fill_show_combo(hDlg, isAlarm);
    CBSetCurSel(hDlg, IDC_RM_ITEM_SHOW, rm_combo_find_text(hDlg, IDC_RM_ITEM_SHOW, curShow));

    showShow = 0;
    showAction = isBuiltin;
    showCmdInputs = (isAlarm || isShell || isCommandline);
    showParam = (isShell || isCommandline);
    showArgs = (isShell || isCommandline);
    showWorkDir = (isShell || isCommandline);
    showDisplay = !isSeparator;
    showLabelUpdate = showDisplay;

    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_EXEC, IDC_RM_ITEM_EXEC_TYPE, 0);
    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_SHOW, IDC_RM_ITEM_SHOW, showShow);

    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_ACTION, IDC_RM_ITEM_ACTION, showAction);

    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_PARAM, IDC_RM_ITEM_PARAM, showParam);
    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_ARGS, IDC_RM_ITEM_ARGS, showArgs);
    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_WORKDIR, IDC_RM_ITEM_WORKDIR, showWorkDir);

    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_LABEL_FORMAT, IDC_RM_ITEM_LABEL_FORMAT, showDisplay);
    rm_set_pair_visible_enabled(hDlg, IDC_RM_LBL_LABEL_UPDATE, IDC_RM_ITEM_LABEL_UPDATE_SEC, showLabelUpdate);
    rm_set_visible_enabled(hDlg, IDC_RM_SPIN_ITEM_LABEL_UPDATE_SEC, showLabelUpdate);

    rm_set_alarm_extra_visible(hDlg, isAlarm);
}

static void rm_update_select_nav_buttons(HWND hDlg)
{
    int sel = CBGetCurSel(hDlg, IDC_RM_SELECT_N);
    int count = CBGetCount(hDlg, IDC_RM_SELECT_N);
    int hasSel = (sel >= 0 && count > 0);

    EnableDlgItem(hDlg, IDC_RM_SELECT_PREV, hasSel && sel > 0);
    EnableDlgItem(hDlg, IDC_RM_SELECT_NEXT, hasSel && sel < (count - 1));
}

static void rm_update_hint(HWND hDlg)
{
    int n = g_rm_selectedN;
    char key[64];
    char v[512];
    int configured = 0;

    rm_build_key(n, "Action", key, (int)sizeof(key));
    rm_get_reg_str(key, v, (int)sizeof(v), "");
    if (v[0]) configured = 1;

    if (!configured) {
        rm_build_key(n, "Label", key, (int)sizeof(key));
        rm_get_reg_str(key, v, (int)sizeof(v), "");
        if (v[0]) configured = 1;
    }

    if (!configured) {
        rm_build_key(n, "Mode", key, (int)sizeof(key));
        rm_get_reg_str(key, v, (int)sizeof(v), "");
        if (v[0] && lstrcmpi(v, "builtin") != 0) configured = 1;
    }

    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_HINT, configured ? "Status: configured" : "Status: empty");
}

static void rm_rebuild_select_combo(HWND hDlg, int itemCount)
{
    int i;
    int sel;
    char label[32];

    itemCount = rm_clamp_int(itemCount, RM_ITEM_MIN, RM_ITEM_MAX);
    sel = g_rm_selectedN - 1;
    sel = rm_clamp_int(sel, 0, itemCount - 1);

    CBResetContent(hDlg, IDC_RM_SELECT_N);
    for (i = 1; i <= itemCount; ++i) {
        wsprintf(label, "Item%d", i);
        CBAddString(hDlg, IDC_RM_SELECT_N, (LPARAM)label);
    }

    CBSetCurSel(hDlg, IDC_RM_SELECT_N, sel);
    g_rm_selectedN = sel + 1;
    rm_update_select_nav_buttons(hDlg);
}

static void rm_load_selected_item(HWND hDlg)
{
    int n = g_rm_selectedN;
    char key[64];
    char s[1024];
    char mode[64];
    int v;

    rm_build_key(n, "Mode", key, (int)sizeof(key));
    rm_get_reg_str(key, mode, (int)sizeof(mode), "builtin");
    CBSetCurSel(hDlg, IDC_RM_ITEM_TYPE, rm_type_index_from_key(mode));

    rm_build_key(n, "Enabled", key, (int)sizeof(key));
    v = (int)GetMyRegLong("MenuCustom", key, rm_is_alarm_type(mode) ? 0 : 1);
    CheckDlgButton(hDlg, IDC_RM_ITEM_ENABLED, v ? BST_CHECKED : BST_UNCHECKED);

    rm_build_key(n, "Label", key, (int)sizeof(key));
    rm_get_reg_str(key, s, (int)sizeof(s), "");
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL, s);

    if (rm_is_alarm_type(mode)) {
        rm_build_key(n, "AlarmMessage", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "Timer finished");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, s);

        rm_build_key(n, "AlarmInitialSec", key, (int)sizeof(key));
        wsprintf(s, "%d", (int)GetMyRegLong("MenuCustom", key, 60));
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_SEC, s);

        rm_build_key(n, "AlarmUpdateSec", key, (int)sizeof(key));
        v = (int)GetMyRegLong("MenuCustom", key, 1);
        SetDlgItemInt(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, (UINT)rm_clamp_int(v, 0, 9999), FALSE);


        rm_build_key(n, "AlarmSoundFile", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, s);

        rm_build_key(n, "AlarmNotifyFlags", key, (int)sizeof(key));
        wsprintf(s, "%d", (int)GetMyRegLong("MenuCustom", key, 3));
        rm_fill_show_combo(hDlg, 1);
        CBSetCurSel(hDlg, IDC_RM_ITEM_SHOW, rm_combo_find_text(hDlg, IDC_RM_ITEM_SHOW, s));

        rm_build_key(n, "AlarmLabelRun", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "Running %REMAIN_MMSS%");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL_FORMAT, s);

        rm_build_key(n, "AlarmSoundVolume", key, (int)sizeof(key));
        v = (int)GetMyRegLong("MenuCustom", key, 70);
        SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_VOLUME, (UINT)rm_clamp_int(v, 0, 100), FALSE);

        rm_build_key(n, "AlarmKeepMenuOpen", key, (int)sizeof(key));
        CheckDlgButton(hDlg, IDC_RM_ALARM_KEEP_OPEN, GetMyRegLong("MenuCustom", key, 1) ? BST_CHECKED : BST_UNCHECKED);

        rm_build_key(n, "AlarmSoundLoop", key, (int)sizeof(key));
        CheckDlgButton(hDlg, IDC_RM_ALARM_SOUND_LOOP, GetMyRegLong("MenuCustom", key, 0) ? BST_CHECKED : BST_UNCHECKED);

        rm_build_key(n, "AlarmLabelIdle", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "Timer %REMAIN_SEC%s");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_IDLE, s);

        rm_build_key(n, "AlarmLabelPause", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "Paused %REMAIN_MMSS%");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_PAUSE, s);

        rm_build_key(n, "AlarmLabelDone", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "Done");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_DONE, s);
    }
    else {
        CheckDlgButton(hDlg, IDC_RM_ALARM_KEEP_OPEN, BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_RM_ALARM_SOUND_LOOP, BST_UNCHECKED);
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_IDLE, "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_PAUSE, "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_DONE, "");
        SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_VOLUME, 70, FALSE);
        SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_SEC, 60, FALSE);
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, "");
        rm_build_key(n, "Action", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        v = rm_action_index_from_key(s);
        if (v >= 0) CBSetCurSel(hDlg, IDC_RM_ITEM_ACTION, v);
        else SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ACTION, s);

        rm_build_key(n, "Param", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_PARAM, s);

        rm_build_key(n, "Args", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ARGS, s);

        rm_build_key(n, "WorkDir", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_WORKDIR, s);

        rm_build_key(n, "Show", key, (int)sizeof(key));
        wsprintf(s, "%d", (int)GetMyRegLong("MenuCustom", key, SW_SHOWNORMAL));
        rm_fill_show_combo(hDlg, 0);
        CBSetCurSel(hDlg, IDC_RM_ITEM_SHOW, rm_combo_find_text(hDlg, IDC_RM_ITEM_SHOW, s));

        rm_build_key(n, "LabelFormat", key, (int)sizeof(key));
        rm_get_reg_str(key, s, (int)sizeof(s), "");
        if (!s[0]) {
            char actionForLabel[128];
            char hex[2048];
            rm_build_key(n, "Label", key, (int)sizeof(key));
            rm_get_reg_str(key, s, (int)sizeof(s), "");
            if (!s[0]) {
                rm_build_key(n, "LabelUtf8Hex", key, (int)sizeof(key));
                rm_get_reg_str(key, hex, (int)sizeof(hex), "");
                if (hex[0]) {
                    rm_decode_utf8_hex(hex, s, (int)sizeof(s));
                }
            }
            if (!s[0]) {
                rm_build_key(n, "Action", key, (int)sizeof(key));
                rm_get_reg_str(key, actionForLabel, (int)sizeof(actionForLabel), "");
                lstrcpyn(s, rm_default_label_for_action(actionForLabel), (int)sizeof(s));
            }
        }
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL_FORMAT, s);

        rm_build_key(n, "LabelUpdateSec", key, (int)sizeof(key));
        v = (int)GetMyRegLong("MenuCustom", key, 1);
        SetDlgItemInt(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, (UINT)rm_clamp_int(v, 0, 9999), FALSE);
    }

    rm_apply_type_ui_state(hDlg);
    rm_update_hint(hDlg);
}

static void rm_write_item(HWND hDlg, int n)
{
    char key[64];
    char s[2048];
    char mode[64];
    int show;

    rm_build_key(n, "Mode", key, (int)sizeof(key));
    rm_get_combo_text(hDlg, IDC_RM_ITEM_TYPE, mode, (int)sizeof(mode), "builtin");
    SetMyRegStr("MenuCustom", key, mode);

    rm_build_key(n, "Enabled", key, (int)sizeof(key));
    SetMyRegLong("MenuCustom", key, IsDlgButtonChecked(hDlg, IDC_RM_ITEM_ENABLED) == BST_CHECKED ? 1 : 0);

    if (GetDlgItem(hDlg, IDC_RM_ITEM_LABEL)) {
        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_LABEL, s, (int)sizeof(s));
        rm_build_key(n, "Label", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);
    }

    if (rm_is_alarm_type(mode)) {
        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, s, (int)sizeof(s));
        rm_build_key(n, "AlarmMessage", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        rm_build_key(n, "Action", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, "");

        rm_build_key(n, "Param", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, "");
        rm_build_key(n, "Args", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, "");
        rm_build_key(n, "WorkDir", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, "");

        rm_build_key(n, "Show", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, 1);

        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ALARM_SEC, s, (int)sizeof(s));
        rm_build_key(n, "AlarmInitialSec", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)rm_clamp_int(atoi(s), 1, 86400));

        rm_build_key(n, "AlarmUpdateSec", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)rm_get_int(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, 1, 0, 9999));

        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, s, (int)sizeof(s));
        rm_build_key(n, "AlarmSoundFile", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        rm_get_combo_text(hDlg, IDC_RM_ITEM_SHOW, s, (int)sizeof(s), "3");
        show = atoi(s);
        rm_build_key(n, "AlarmNotifyFlags", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)rm_clamp_int(show, 0, 3));

        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_LABEL_FORMAT, s, (int)sizeof(s));
        rm_build_key(n, "AlarmLabelRun", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        rm_build_key(n, "AlarmSoundVolume", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)rm_get_int(hDlg, IDC_RM_ITEM_ALARM_VOLUME, 70, 0, 100));

        rm_build_key(n, "AlarmKeepMenuOpen", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, IsDlgButtonChecked(hDlg, IDC_RM_ALARM_KEEP_OPEN) == BST_CHECKED ? 1 : 0);

        rm_build_key(n, "AlarmSoundLoop", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, IsDlgButtonChecked(hDlg, IDC_RM_ALARM_SOUND_LOOP) == BST_CHECKED ? 1 : 0);

        GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_IDLE, s, (int)sizeof(s));
        rm_build_key(n, "AlarmLabelIdle", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_PAUSE, s, (int)sizeof(s));
        rm_build_key(n, "AlarmLabelPause", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_DONE, s, (int)sizeof(s));
        rm_build_key(n, "AlarmLabelDone", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);
    }
    else {
        rm_get_combo_text(hDlg, IDC_RM_ITEM_ACTION, s, (int)sizeof(s), "");
        rm_build_key(n, "Action", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        if (lstrcmpi(mode, "builtin") == 0) {
            rm_build_key(n, "Param", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, "");
            rm_build_key(n, "Args", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, "");
            rm_build_key(n, "WorkDir", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, "");
        } else if (lstrcmpi(mode, "shell") == 0 || lstrcmpi(mode, "commandline") == 0) {
            rm_build_key(n, "Action", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, "");

            GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_PARAM, s, (int)sizeof(s));
            rm_build_key(n, "Param", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, s);

            GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ARGS, s, (int)sizeof(s));
            rm_build_key(n, "Args", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, s);

            GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_WORKDIR, s, (int)sizeof(s));
            rm_build_key(n, "WorkDir", key, (int)sizeof(key));
            SetMyRegStr("MenuCustom", key, s);
        }

        rm_get_combo_text(hDlg, IDC_RM_ITEM_SHOW, s, (int)sizeof(s), "1");
        show = atoi(s);
        rm_build_key(n, "Show", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)show);

        GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_LABEL_FORMAT, s, (int)sizeof(s));
        rm_build_key(n, "LabelFormat", key, (int)sizeof(key));
        SetMyRegStr("MenuCustom", key, s);

        rm_build_key(n, "LabelUpdateSec", key, (int)sizeof(key));
        SetMyRegLong("MenuCustom", key, (DWORD)rm_get_int(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, 1, 0, 9999));
    }
}

static void rm_select_item(HWND hDlg, int newSel)
{
    int count = CBGetCount(hDlg, IDC_RM_SELECT_N);

    if (count <= 0) return;
    newSel = rm_clamp_int(newSel, 0, count - 1);

    CBSetCurSel(hDlg, IDC_RM_SELECT_N, newSel);
    g_rm_selectedN = newSel + 1;
    rm_load_selected_item(hDlg);
    rm_update_select_nav_buttons(hDlg);
}

static void rm_on_init(HWND hDlg)
{
    int itemCount;

    rm_fill_combo_defaults(hDlg);

    /* Format-capable display label input must stay Unicode to preserve non-ACP symbols. */
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ITEM_LABEL_FORMAT);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ITEM_ALARM_MESSAGE);

    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_LABEL), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_LABEL), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_EXEC), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_EXEC_TYPE), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_SHOW), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_SHOW), SW_HIDE);

    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ITEMCOUNT, UDM_SETRANGE, 0, MAKELONG(RM_ITEM_MAX, 0));
    SendDlgItemMessage(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, EM_LIMITTEXT, 4, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ITEM_LABEL_UPDATE_SEC, UDM_SETRANGE, 0, MAKELONG(9999, 0));
    SendDlgItemMessage(hDlg, IDC_RM_ITEM_ALARM_SEC, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ALARM_SEC, UDM_SETRANGE, 0, MAKELONG(86400, 1));
    SendDlgItemMessage(hDlg, IDC_RM_ITEM_ALARM_VOLUME, EM_LIMITTEXT, 3, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ALARM_VOLUME, UDM_SETRANGE, 0, MAKELONG(100, 0));

    CheckDlgButton(hDlg, IDC_RM_ENABLE, GetMyRegLong("MenuCustom", "MenuCustomEnabled", 1) ? BST_CHECKED : BST_UNCHECKED);

    itemCount = (int)GetMyRegLong("MenuCustom", "ItemCount", 16);
    itemCount = rm_clamp_int(itemCount, 0, RM_ITEM_MAX);
    SetDlgItemInt(hDlg, IDC_RM_ITEMCOUNT, (UINT)itemCount, FALSE);

    rm_rebuild_select_combo(hDlg, itemCount > 0 ? itemCount : 1);
    rm_set_alarm_extra_visible(hDlg, 0);
    rm_select_item(hDlg, 0);
}

static void rm_on_apply(HWND hDlg)
{
    int itemCount;

    SetMyRegLong("MenuCustom", "MenuCustomEnabled", IsDlgButtonChecked(hDlg, IDC_RM_ENABLE) == BST_CHECKED ? 1 : 0);

    itemCount = rm_get_int(hDlg, IDC_RM_ITEMCOUNT, 16, 0, RM_ITEM_MAX);
    SetMyRegLong("MenuCustom", "ItemCount", (DWORD)itemCount);

    if (itemCount > 0) {
        int n = rm_clamp_int(g_rm_selectedN, 1, itemCount);
        rm_write_item(hDlg, n);
    }
}

BOOL CALLBACK PageRClickMenuProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        rm_on_init(hDlg);
        return TRUE;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);

        if (id == IDC_RM_SELECT_N && code == CBN_SELCHANGE) {
            int sel = CBGetCurSel(hDlg, IDC_RM_SELECT_N);
            if (sel >= 0) {
                rm_select_item(hDlg, sel);
            }
            return TRUE;
        }

        if ((id == IDC_RM_SELECT_PREV || id == IDC_RM_SELECT_NEXT) && code == BN_CLICKED) {
            int sel = CBGetCurSel(hDlg, IDC_RM_SELECT_N);
            int delta = (id == IDC_RM_SELECT_PREV) ? -1 : 1;
            if (sel >= 0) {
                rm_select_item(hDlg, sel + delta);
            }
            return TRUE;
        }

        if (id == IDC_RM_ITEMCOUNT && code == EN_CHANGE) {
            BOOL ok = FALSE;
            UINT u = GetDlgItemInt(hDlg, IDC_RM_ITEMCOUNT, &ok, FALSE);
            if (ok) {
                int itemCount = rm_clamp_int((int)u, 0, RM_ITEM_MAX);
                rm_rebuild_select_combo(hDlg, itemCount > 0 ? itemCount : 1);
                rm_select_item(hDlg, g_rm_selectedN - 1);
                rm_send_ps_changed(hDlg);
            }
            return TRUE;
        }

        if (id == IDC_RM_ITEM_TYPE && code == CBN_SELCHANGE) {
            rm_apply_type_ui_state(hDlg);
            rm_send_ps_changed(hDlg);
            return TRUE;
        }

        if (code == EN_CHANGE || code == CBN_SELCHANGE || code == BN_CLICKED) {
            rm_send_ps_changed(hDlg);
            return TRUE;
        }

        break;
    }

    case WM_NOTIFY:
        switch (((NMHDR*)lParam)->code) {
        case PSN_APPLY:
            rm_on_apply(hDlg);
            return TRUE;
        case PSN_HELP:
            My2chHelp(GetParent(hDlg));
            return TRUE;
        }
        break;
    }

    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(wParam);
    return FALSE;
}
