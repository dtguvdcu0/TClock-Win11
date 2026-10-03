/*-------------------------------------------
  page_rclick_menu.c
  "Right Click Menu" page for [MenuCustom]
---------------------------------------------*/

#include "tclock.h"
#include "../common/ini_io_utf8.h"
#include "../common/text_codec.h"
#include <stdio.h>
#include <string.h>

#define RM_ITEM_MIN 1
#define RM_ITEM_MAX 64
#define RM_SAVE_BYTES (256 * 1024)

extern BOOL b_EnglishMenu;
extern int Language_Offset;

static int g_rm_selectedN = 1;
static int g_rm_item_count = 0;
static int g_rm_ready = 0;
static int g_rm_loading = 0;
static int g_rm_dirty = 0;

typedef struct RM_MENU_ITEM_DATA {
    int sourceRow; /* Original identity until the pending edits are successfully committed. */
    char mode[32];
    int enabled;
    char label[256];
    char action[128];
    char param[512];
    char args[512];
    char workdir[MAX_PATH];
    int show;
    char labelFormat[256];
    int labelUpdateSec;
    char alarmMessage[256];
    int alarmInitialSec;
    int alarmUpdateSec;
    char alarmSoundFile[MAX_PATH];
    int alarmNotifyFlags;
    char alarmLabelRun[256];
    int alarmSoundVolume;
    int alarmKeepMenuOpen;
    int alarmSoundLoop;
    char alarmLabelIdle[256];
    char alarmLabelPause[256];
    char alarmLabelDone[256];
} RM_MENU_ITEM_DATA;

static RM_MENU_ITEM_DATA g_rm_items[RM_ITEM_MAX];

static const char* rm_type_key_from_index(int idx);

static void rm_send_ps_changed(HWND hDlg)
{
    if (g_rm_loading) return;
    g_rm_dirty = 1;
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
            int actionIndex = (int)SendDlgItemMessageW(hDlg, id, CB_GETITEMDATA, sel, 0);
            lstrcpyn(out, rm_action_key_from_index(actionIndex), outBytes);
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
    case 0: return englishUi ? "builtin" : "ビルトイン";
    case 1: return englishUi ? "shell" : "シェル";
    case 2: return englishUi ? "commandline" : "コマンド";
    case 3: return englishUi ? "passive" : "表示のみ";
    case 4: return englishUi ? "separator" : "区切り線";
    case 5: return englishUi ? "alarm" : "アラーム";
    default: return englishUi ? "builtin" : "ビルトイン";
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
    if (lstrcmpi(action, "time_sync") == 0) return MyStringUTF8(IDS_TIMESYNC);
    if (lstrcmpi(action, "settings_datausage") == 0) return MyStringUTF8(IDS_DATAUSAGE);
    if (lstrcmpi(action, "tcard_open") == 0) return MyStringUTF8(IDS_TCARD_OPEN);
    if (lstrcmpi(action, "tcalendar_open") == 0) return MyStringUTF8(IDS_TCAL_OPEN);
    if (lstrcmpi(action, "tcapture_settings") == 0) return MyStringUTF8(IDS_TCAP_SETTING);
    if (lstrcmpi(action, "tcycle_open") == 0) return MyStringUTF8(IDS_TCYC_OPEN);
    if (lstrcmpi(action, "control_datetime") == 0) return MyStringUTF8(IDS_CONTROLDATE);
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
    case 7: return "control_datetime";
    case 8: return "settings_home";
    case 9: return "settings_network";
    case 10: return "settings_datausage";
    case 11: return "time_sync";
    case 12: return "settings_datetime";
    case 13: return "tcard_open";
    case 14: return "tcalendar_open";
    case 15: return "tcapture_settings";
    case 16: return "tcycle_open";
    case 17: return "remove_drive_dynamic";
    default: return "taskmgr";
    }
}

static int rm_action_index_from_key(const char* key)
{
    int i;
    if (!key || !key[0]) return -1;
    for (i = 0; i < 18; ++i) {
        if (lstrcmpi(key, rm_action_key_from_index(i)) == 0) return i;
    }
    return -1;
}

static void rm_fill_builtin_action_combo(HWND hDlg)
{
    int i;
    CBResetContent(hDlg, IDC_RM_ITEM_ACTION);
    for (i = 0; i < 18; ++i) {
        int function = i == 13 ? MOUSEFUNC_TCARD_OPEN : i == 14 ? MOUSEFUNC_TCALENDAR_OPEN :
            i == 15 ? MOUSEFUNC_TCAPTURE_SETTINGS : i == 16 ? MOUSEFUNC_TCYCLE_OPEN : MOUSEFUNC_NONE;
        int row;
        if (!act_is_available(hDlg, function)) continue;
        row = CBAddStringUTF8Compat(hDlg, IDC_RM_ITEM_ACTION, rm_default_label_for_action(rm_action_key_from_index(i)));
        CBSetItemData(hDlg, IDC_RM_ITEM_ACTION, row, i);
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
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ALARM_SAMPLE), cmd);
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

static void rm_update_description(HWND hDlg, const char* mode)
{
    const char* text = "";
    int en = rm_is_english_ui();
    if (lstrcmpi(mode, "builtin") == 0) text = en ? "Calls a feature preset in TClock." : "TClockにプリセットされた機能を呼び出します。";
    else if (lstrcmpi(mode, "shell") == 0) text = en ? "Launches the specified application or file, with optional arguments and working directory." : "指定したアプリやファイルを起動します。必要に応じて引数や作業フォルダーも指定できます。";
    else if (lstrcmpi(mode, "commandline") == 0) text = en ? "Runs the specified command in Command Prompt." : "指定したコマンドをコマンドプロンプトで実行します。";
    else if (rm_is_alarm_type(mode)) text = en ? "Click to start or pause the timer. A message or sound signals completion; right-click to reset." : "クリックでタイマーを開始・一時停止します。設定時間になると、メッセージや音でお知らせします。右クリックでリセットできます。";
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_DESCRIPTION, text);
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
    rm_update_description(hDlg, mode);
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
    int configured = 0;
    RM_MENU_ITEM_DATA* item;

    if (g_rm_selectedN < 1 || g_rm_selectedN > g_rm_item_count) {
        SetDlgItemTextUTF8Strict(hDlg, IDC_RM_HINT, "Status: empty");
        return;
    }
    item = &g_rm_items[g_rm_selectedN - 1];
    if (item->action[0] || item->label[0] || item->labelFormat[0] ||
        lstrcmpi(item->mode, "builtin") != 0) {
        configured = 1;
    }
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_HINT, configured ? "Status: configured" : "Status: empty");
}

static void rm_item_init(RM_MENU_ITEM_DATA* item)
{
    const char* action = rm_action_key_from_index(0);
    ZeroMemory(item, sizeof(*item));
    lstrcpyn(item->mode, "builtin", (int)sizeof(item->mode));
    item->enabled = 1;
    lstrcpyn(item->action, action, (int)sizeof(item->action));
    lstrcpyn(item->labelFormat, rm_default_label_for_action(action), (int)sizeof(item->labelFormat));
    item->show = SW_SHOWNORMAL;
    item->labelUpdateSec = 1;
    item->alarmInitialSec = 60;
    item->alarmUpdateSec = 1;
    item->alarmNotifyFlags = 3;
    item->alarmSoundVolume = 70;
    item->alarmKeepMenuOpen = 1;
    lstrcpyn(item->alarmMessage, "Timer finished", (int)sizeof(item->alarmMessage));
    lstrcpyn(item->alarmLabelRun, "Running %REMAIN_MMSS%", (int)sizeof(item->alarmLabelRun));
    lstrcpyn(item->alarmLabelIdle, "Timer %REMAIN_SEC%s", (int)sizeof(item->alarmLabelIdle));
    lstrcpyn(item->alarmLabelPause, "Paused %REMAIN_MMSS%", (int)sizeof(item->alarmLabelPause));
    lstrcpyn(item->alarmLabelDone, "Done", (int)sizeof(item->alarmLabelDone));
}

static void rm_item_get_str(int n, const char* suffix, char* out, int outBytes, const char* defv)
{
    char key[64];
    rm_build_key(n, suffix, key, (int)sizeof(key));
    rm_get_reg_str(key, out, outBytes, defv);
}

static int rm_item_get_int(int n, const char* suffix, int defv)
{
    char key[64];
    rm_build_key(n, suffix, key, (int)sizeof(key));
    return (int)GetMyRegLong("MenuCustom", key, (DWORD)defv);
}

static BOOL rm_append_key(char* out, DWORD* used, const char* key, const char* value)
{
    size_t keyLength = strlen(key);
    size_t valueLength = value ? strlen(value) : 0;
    size_t need = keyLength + (value ? valueLength + 1 : 0) + 1;
    if (need >= RM_SAVE_BYTES - *used || (value && (strchr(value, '\r') || strchr(value, '\n')))) return FALSE;
    memcpy(out + *used, key, keyLength); *used += (DWORD)keyLength;
    if (value) {
        out[(*used)++] = '=';
        memcpy(out + *used, value, valueLength); *used += (DWORD)valueLength;
    }
    out[(*used)++] = '\0'; out[*used] = '\0';
    return TRUE;
}

static BOOL rm_append_str(char* out, DWORD* used, int n, const char* suffix, const char* value)
{
    char key[96];
    rm_build_key(n, suffix, key, (int)sizeof(key));
    return rm_append_key(out, used, key, value);
}

static BOOL rm_append_int(char* out, DWORD* used, int n, const char* suffix, int value)
{
    char number[32];
    sprintf_s(number, sizeof(number), "%d", value);
    return rm_append_str(out, used, n, suffix, number);
}

static int rm_item_read_ini(int n, RM_MENU_ITEM_DATA* item)
{
    char hex[2048];
    rm_item_init(item);
    rm_item_get_str(n, "Mode", item->mode, (int)sizeof(item->mode), "");
    if (!item->mode[0]) return 0;
    item->sourceRow = n;
    item->enabled = rm_item_get_int(n, "Enabled", rm_is_alarm_type(item->mode) ? 0 : 1);
    rm_item_get_str(n, "Label", item->label, (int)sizeof(item->label), "");
    rm_item_get_str(n, "Action", item->action, (int)sizeof(item->action), "");
    rm_item_get_str(n, "Param", item->param, (int)sizeof(item->param), "");
    rm_item_get_str(n, "Args", item->args, (int)sizeof(item->args), "");
    rm_item_get_str(n, "WorkDir", item->workdir, (int)sizeof(item->workdir), "");
    item->show = rm_item_get_int(n, rm_is_alarm_type(item->mode) ? "AlarmNotifyFlags" : "Show", rm_is_alarm_type(item->mode) ? 3 : SW_SHOWNORMAL);
    rm_item_get_str(n, "LabelFormat", item->labelFormat, (int)sizeof(item->labelFormat), "");
    item->labelUpdateSec = rm_item_get_int(n, rm_is_alarm_type(item->mode) ? "AlarmUpdateSec" : "LabelUpdateSec", 1);
    rm_item_get_str(n, "AlarmMessage", item->alarmMessage, (int)sizeof(item->alarmMessage), "Timer finished");
    item->alarmInitialSec = rm_item_get_int(n, "AlarmInitialSec", 60);
    item->alarmUpdateSec = rm_item_get_int(n, "AlarmUpdateSec", 1);
    rm_item_get_str(n, "AlarmSoundFile", item->alarmSoundFile, (int)sizeof(item->alarmSoundFile), "");
    item->alarmNotifyFlags = rm_item_get_int(n, "AlarmNotifyFlags", 3);
    rm_item_get_str(n, "AlarmLabelRun", item->alarmLabelRun, (int)sizeof(item->alarmLabelRun), "Running %REMAIN_MMSS%");
    item->alarmSoundVolume = rm_item_get_int(n, "AlarmSoundVolume", 70);
    item->alarmKeepMenuOpen = rm_item_get_int(n, "AlarmKeepMenuOpen", 1);
    item->alarmSoundLoop = rm_item_get_int(n, "AlarmSoundLoop", 0);
    rm_item_get_str(n, "AlarmLabelIdle", item->alarmLabelIdle, (int)sizeof(item->alarmLabelIdle), "Timer %REMAIN_SEC%s");
    rm_item_get_str(n, "AlarmLabelPause", item->alarmLabelPause, (int)sizeof(item->alarmLabelPause), "Paused %REMAIN_MMSS%");
    rm_item_get_str(n, "AlarmLabelDone", item->alarmLabelDone, (int)sizeof(item->alarmLabelDone), "Done");
    if (!rm_is_alarm_type(item->mode) && !item->labelFormat[0]) {
        if (item->label[0]) {
            lstrcpyn(item->labelFormat, item->label, (int)sizeof(item->labelFormat));
        } else {
            rm_item_get_str(n, "LabelUtf8Hex", hex, (int)sizeof(hex), "");
            if (hex[0] && rm_decode_utf8_hex(hex, item->labelFormat, (int)sizeof(item->labelFormat)) <= 0) {
                item->labelFormat[0] = '\0';
            }
            if (!item->labelFormat[0]) {
                lstrcpyn(item->labelFormat, rm_default_label_for_action(item->action), (int)sizeof(item->labelFormat));
            }
        }
    }
    return 1;
}

static void rm_item_read_all(void)
{
    int i;
    g_rm_item_count = 0;
    for (i = 1; i <= RM_ITEM_MAX; ++i) {
        RM_MENU_ITEM_DATA item;
        if (rm_item_read_ini(i, &item)) {
            g_rm_items[g_rm_item_count++] = item;
        }
    }
    if (g_rm_item_count <= 0) {
        rm_item_init(&g_rm_items[0]);
        g_rm_item_count = 1;
    }
}

static void rm_item_capture_controls(HWND hDlg, RM_MENU_ITEM_DATA* item)
{
    char s[2048];
    rm_get_combo_text(hDlg, IDC_RM_ITEM_TYPE, item->mode, (int)sizeof(item->mode), "builtin");
    item->enabled = IsDlgButtonChecked(hDlg, IDC_RM_ITEM_ENABLED) == BST_CHECKED;
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_LABEL, item->label, (int)sizeof(item->label));
    /* An omitted saved plugin action keeps its stable key until the user chooses another row. */
    if (_stricmp(item->mode, "builtin") || CBGetCurSel(hDlg, IDC_RM_ITEM_ACTION) >= 0 ||
        rm_action_index_from_key(item->action) < 0)
        rm_get_combo_text(hDlg, IDC_RM_ITEM_ACTION, item->action, (int)sizeof(item->action), "");
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_PARAM, item->param, (int)sizeof(item->param));
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ARGS, item->args, (int)sizeof(item->args));
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_WORKDIR, item->workdir, (int)sizeof(item->workdir));
    rm_get_combo_text(hDlg, IDC_RM_ITEM_SHOW, s, (int)sizeof(s), rm_is_alarm_type(item->mode) ? "3" : "1");
    item->show = atoi(s);
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_LABEL_FORMAT, item->labelFormat, (int)sizeof(item->labelFormat));
    item->labelUpdateSec = rm_get_int(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, 1, 0, 9999);
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, item->alarmMessage, (int)sizeof(item->alarmMessage));
    item->alarmInitialSec = rm_get_int(hDlg, IDC_RM_ITEM_ALARM_SEC, 60, 1, 86400);
    item->alarmUpdateSec = item->labelUpdateSec;
    GetDlgItemTextUTF8(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, item->alarmSoundFile, (int)sizeof(item->alarmSoundFile));
    item->alarmNotifyFlags = rm_clamp_int(item->show, 0, 3);
    lstrcpyn(item->alarmLabelRun, item->labelFormat, (int)sizeof(item->alarmLabelRun));
    item->alarmSoundVolume = rm_get_int(hDlg, IDC_RM_ITEM_ALARM_VOLUME, 70, 0, 100);
    item->alarmKeepMenuOpen = IsDlgButtonChecked(hDlg, IDC_RM_ALARM_KEEP_OPEN) == BST_CHECKED;
    item->alarmSoundLoop = IsDlgButtonChecked(hDlg, IDC_RM_ALARM_SOUND_LOOP) == BST_CHECKED;
    GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_IDLE, item->alarmLabelIdle, (int)sizeof(item->alarmLabelIdle));
    GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_PAUSE, item->alarmLabelPause, (int)sizeof(item->alarmLabelPause));
    GetDlgItemTextUTF8(hDlg, IDC_RM_ALARM_LABEL_DONE, item->alarmLabelDone, (int)sizeof(item->alarmLabelDone));
}

static void rm_item_load_controls(HWND hDlg, const RM_MENU_ITEM_DATA* item)
{
    int index;
    char number[32];
    CBSetCurSel(hDlg, IDC_RM_ITEM_TYPE, rm_type_index_from_key(item->mode));
    CheckDlgButton(hDlg, IDC_RM_ITEM_ENABLED, item->enabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL, item->label);
    index = rm_action_index_from_key(item->action);
    {
        int row, found = -1;
        for (row = 0; row < CBGetCount(hDlg, IDC_RM_ITEM_ACTION); ++row)
            if ((int)CBGetItemData(hDlg, IDC_RM_ITEM_ACTION, row) == index) { found = row; break; }
        CBSetCurSel(hDlg, IDC_RM_ITEM_ACTION, found);
        if (found < 0) SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ACTION,
            index >= 0 ? rm_default_label_for_action(item->action) : item->action);
    }
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_PARAM, item->param);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ARGS, item->args);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_WORKDIR, item->workdir);
    rm_fill_show_combo(hDlg, rm_is_alarm_type(item->mode));
    wsprintf(number, "%d", rm_is_alarm_type(item->mode) ? item->alarmNotifyFlags : item->show);
    CBSetCurSel(hDlg, IDC_RM_ITEM_SHOW, rm_combo_find_text(hDlg, IDC_RM_ITEM_SHOW, number));
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL_FORMAT, rm_is_alarm_type(item->mode) ? item->alarmLabelRun : item->labelFormat);
    SetDlgItemInt(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, (UINT)(rm_is_alarm_type(item->mode) ? item->alarmUpdateSec : item->labelUpdateSec), FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, item->alarmMessage);
    SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_SEC, (UINT)item->alarmInitialSec, FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, item->alarmSoundFile);
    SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_VOLUME, (UINT)item->alarmSoundVolume, FALSE);
    CheckDlgButton(hDlg, IDC_RM_ALARM_KEEP_OPEN, item->alarmKeepMenuOpen ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_RM_ALARM_SOUND_LOOP, item->alarmSoundLoop ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_IDLE, item->alarmLabelIdle);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_PAUSE, item->alarmLabelPause);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_DONE, item->alarmLabelDone);
    rm_apply_type_ui_state(hDlg);
    rm_update_hint(hDlg);
}

static BOOL rm_write_fields(char* out, DWORD* used, int n, const RM_MENU_ITEM_DATA* item)
{
    if (!rm_append_str(out, used, n, "Mode", item->mode)) return FALSE;
    if (!rm_append_int(out, used, n, "Enabled", item->enabled)) return FALSE;
    if (!rm_append_str(out, used, n, "Label", item->label)) return FALSE;
    if (!rm_append_str(out, used, n, "Action", item->action)) return FALSE;
    if (!rm_append_str(out, used, n, "Param", item->param)) return FALSE;
    if (!rm_append_str(out, used, n, "Args", item->args)) return FALSE;
    if (!rm_append_str(out, used, n, "WorkDir", item->workdir)) return FALSE;
    if (!rm_append_int(out, used, n, "Show", item->show)) return FALSE;
    if (!rm_append_str(out, used, n, "LabelFormat", item->labelFormat)) return FALSE;
    if (!rm_append_int(out, used, n, "LabelUpdateSec", item->labelUpdateSec)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmMessage", item->alarmMessage)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmInitialSec", item->alarmInitialSec)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmUpdateSec", item->alarmUpdateSec)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmSoundFile", item->alarmSoundFile)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmNotifyFlags", item->alarmNotifyFlags)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmLabelRun", item->alarmLabelRun)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmSoundVolume", item->alarmSoundVolume)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmKeepMenuOpen", item->alarmKeepMenuOpen)) return FALSE;
    if (!rm_append_int(out, used, n, "AlarmSoundLoop", item->alarmSoundLoop)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmLabelIdle", item->alarmLabelIdle)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmLabelPause", item->alarmLabelPause)) return FALSE;
    if (!rm_append_str(out, used, n, "AlarmLabelDone", item->alarmLabelDone)) return FALSE;
    return TRUE;
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
    int index = g_rm_selectedN - 1;
    if (index < 0 || index >= g_rm_item_count) return;
    ++g_rm_loading;
    rm_item_load_controls(hDlg, &g_rm_items[index]);
    --g_rm_loading;
}

static void rm_select_item(HWND hDlg, int newSel)
{
    int oldIndex = g_rm_selectedN - 1;
    if (g_rm_ready && oldIndex >= 0 && oldIndex < g_rm_item_count) {
        rm_item_capture_controls(hDlg, &g_rm_items[oldIndex]);
    }
    if (g_rm_item_count <= 0) return;
    newSel = rm_clamp_int(newSel, 0, g_rm_item_count - 1);
    g_rm_selectedN = newSel + 1;
    CBSetCurSel(hDlg, IDC_RM_SELECT_N, newSel);
    rm_load_selected_item(hDlg);
    rm_update_select_nav_buttons(hDlg);
}

static void rm_step_selection(HWND hDlg, int delta)
{
    int target = g_rm_selectedN - 1 + delta;
    if (target < 0 || target >= g_rm_item_count) return;
    rm_select_item(hDlg, target);
}

static void rm_add_item(HWND hDlg)
{
    int sel = g_rm_selectedN - 1;
    int i;
    if (g_rm_item_count >= RM_ITEM_MAX) {
        MessageBoxW(hDlg, L"項目数の上限に達しています。", L"TClock", MB_OK | MB_ICONINFORMATION);
        return;
    }
    rm_item_capture_controls(hDlg, &g_rm_items[sel]);
    for (i = g_rm_item_count; i > sel + 1; --i) g_rm_items[i] = g_rm_items[i - 1];
    rm_item_init(&g_rm_items[sel + 1]);
    ++g_rm_item_count;
    g_rm_selectedN = sel + 2;
    rm_rebuild_select_combo(hDlg, g_rm_item_count);
    rm_load_selected_item(hDlg);
    rm_send_ps_changed(hDlg);
}

static void rm_delete_item(HWND hDlg)
{
    int sel = g_rm_selectedN - 1;
    int i;
    wchar_t message[128];
    if (g_rm_item_count <= RM_ITEM_MIN) {
        MessageBoxW(hDlg, L"少なくとも1項目は残してください。", L"TClock", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wsprintfW(message, L"Item%d を削除しますか？", sel + 1);
    if (MessageBoxW(hDlg, message, L"TClock", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    rm_item_capture_controls(hDlg, &g_rm_items[sel]);
    for (i = sel; i < g_rm_item_count - 1; ++i) g_rm_items[i] = g_rm_items[i + 1];
    --g_rm_item_count;
    ZeroMemory(&g_rm_items[g_rm_item_count], sizeof(g_rm_items[g_rm_item_count]));
    if (g_rm_selectedN > g_rm_item_count) g_rm_selectedN = g_rm_item_count;
    rm_rebuild_select_combo(hDlg, g_rm_item_count);
    rm_load_selected_item(hDlg);
    rm_send_ps_changed(hDlg);
}

static void rm_load_alarm_sample(HWND hDlg)
{
    CBSetCurSel(hDlg, IDC_RM_ITEM_TYPE, rm_type_index_from_key("alarm"));
    CheckDlgButton(hDlg, IDC_RM_ITEM_ENABLED, BST_CHECKED);
    SetDlgItemInt(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, 1, FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_IDLE, "Timer %REMAIN_SEC%s");
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_LABEL_FORMAT, "%REMAIN_MMSS% タイマー");
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_PAUSE, "Paused %REMAIN_MMSS%");
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ALARM_LABEL_DONE, "Done");
    SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_SEC, 10, FALSE);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE, "C:\\Windows\\Media\\notify.wav");
    SetDlgItemInt(hDlg, IDC_RM_ITEM_ALARM_VOLUME, 70, FALSE);
    CheckDlgButton(hDlg, IDC_RM_ALARM_SOUND_LOOP, BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_RM_ALARM_KEEP_OPEN, BST_CHECKED);
    SetDlgItemTextUTF8Strict(hDlg, IDC_RM_ITEM_ALARM_MESSAGE, "タイマー終了");
    rm_apply_type_ui_state(hDlg);
    CBSetCurSel(hDlg, IDC_RM_ITEM_SHOW, 3);
    rm_item_capture_controls(hDlg, &g_rm_items[g_rm_selectedN - 1]);
    rm_update_hint(hDlg);
    rm_send_ps_changed(hDlg);
}

static void rm_on_init(HWND hDlg)
{
    g_rm_loading = 1;
    g_rm_ready = 0;
    g_rm_dirty = 0;
    rm_fill_combo_defaults(hDlg);

    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ITEM_LABEL_FORMAT);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ITEM_ALARM_MESSAGE);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ITEM_ALARM_SOUND_FILE);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ALARM_LABEL_IDLE);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ALARM_LABEL_PAUSE);
    EnsureUnicodeEditControlShared(hDlg, IDC_RM_ALARM_LABEL_DONE);

    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_LABEL), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_LABEL), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_EXEC), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_EXEC_TYPE), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_LBL_SHOW), SW_HIDE);
    ShowWindow(GetDlgItem(hDlg, IDC_RM_ITEM_SHOW), SW_HIDE);

    SendDlgItemMessage(hDlg, IDC_RM_ITEM_LABEL_UPDATE_SEC, EM_LIMITTEXT, 4, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ITEM_LABEL_UPDATE_SEC, UDM_SETRANGE, 0, MAKELONG(9999, 0));
    SendDlgItemMessage(hDlg, IDC_RM_ITEM_ALARM_SEC, EM_LIMITTEXT, 5, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ALARM_SEC, UDM_SETRANGE, 0, MAKELONG(86400, 1));
    SendDlgItemMessage(hDlg, IDC_RM_ITEM_ALARM_VOLUME, EM_LIMITTEXT, 3, 0);
    SendDlgItemMessage(hDlg, IDC_RM_SPIN_ALARM_VOLUME, UDM_SETRANGE, 0, MAKELONG(100, 0));

    CheckDlgButton(hDlg, IDC_RM_ENABLE, GetMyRegLong("MenuCustom", "MenuCustomEnabled", 1) ? BST_CHECKED : BST_UNCHECKED);
    rm_item_read_all();
    g_rm_selectedN = 1;
    rm_rebuild_select_combo(hDlg, g_rm_item_count);
    g_rm_ready = 1;
    rm_load_selected_item(hDlg);
    rm_update_select_nav_buttons(hDlg);
    g_rm_loading = 0;
}

static BOOL rm_commit(HWND hDlg)
{
    static const char* fields[] = {"Mode", "Enabled", "Label", "Action", "Param", "Args", "WorkDir", "Show", "LabelFormat", "LabelUpdateSec", "AlarmMessage", "AlarmInitialSec", "AlarmUpdateSec", "AlarmSoundFile", "AlarmNotifyFlags", "AlarmLabelRun", "AlarmSoundVolume", "AlarmKeepMenuOpen", "AlarmSoundLoop", "AlarmLabelIdle", "AlarmLabelPause", "AlarmLabelDone", "Type", "ExecType"};
    char* data;
    wchar_t path[MAX_PATH];
    wchar_t message[256];
    DWORD used = 0, error = ERROR_INVALID_DATA;
    int i, j, origins[RM_ITEM_MAX];
    BOOL ok = FALSE;
    if (!g_rm_dirty) return TRUE;
    rm_item_capture_controls(hDlg, &g_rm_items[g_rm_selectedN - 1]);
    data = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RM_SAVE_BYTES);
    if (!data) { error = ERROR_NOT_ENOUGH_MEMORY; goto failure; }
    if (!rm_append_key(data, &used, "MenuCustomEnabled", IsDlgButtonChecked(hDlg, IDC_RM_ENABLE) == BST_CHECKED ? "1" : "0")) goto cleanup;
    for (i = 0; i < g_rm_item_count; ++i) {
        if (!rm_write_fields(data, &used, i + 1, &g_rm_items[i])) goto cleanup;
        origins[i] = g_rm_items[i].sourceRow;
    }
    for (i = 1; i <= RM_ITEM_MAX; ++i) {
        for (j = 0; j < (int)_countof(fields); ++j) {
            char companion[64];
            sprintf_s(companion, sizeof(companion), "%sUtf8Hex", fields[j]);
            if (!rm_append_str(data, &used, i, companion, NULL)) goto cleanup;
            if ((i > g_rm_item_count || j >= 22) && !rm_append_str(data, &used, i, fields[j], NULL)) goto cleanup;
        }
    }
    /* Decode once at the existing INI serialization boundary; all transaction paths stay UTF-16. */
    if (tc_utf8_to_utf16(g_inifile, path, _countof(path)) <= 0) { error = ERROR_NO_UNICODE_TRANSLATION; goto cleanup; }
    ok = tc_write_batchW(path, "MenuCustom", data, used + 1);
    if (!ok) { error = GetLastError(); goto cleanup; }
    MenuCustomRemapAlarms(origins, g_rm_item_count);
    for (i = 0; i < g_rm_item_count; ++i) g_rm_items[i].sourceRow = i + 1;
    g_rm_dirty = 0;
cleanup:
    HeapFree(GetProcessHeap(), 0, data);
    if (ok) return TRUE;
failure:
    swprintf_s(message, _countof(message), rm_is_english_ui() ?
        L"Could not save the right-click menu settings. Check INI access and UTF-8 encoding, then retry. (Error %lu)" :
        L"右クリックメニューの設定を保存できません。INIへのアクセス権とUTF-8形式を確認し、再試行してください。 (%lu)", error);
    MessageBoxW(GetParent(hDlg), message, L"TClock", MB_OK | MB_ICONERROR);
    return FALSE;
}

BOOL CALLBACK PageRClickMenuProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case RM_COMMIT:
        SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, rm_commit(hDlg));
        return TRUE;
    case WM_INITDIALOG:
        rm_on_init(hDlg);
        return TRUE;
    case WM_SHOWWINDOW:
        if (wParam && g_rm_ready && g_rm_item_count > 0) {
            g_rm_loading = 1;
            rm_item_capture_controls(hDlg, &g_rm_items[g_rm_selectedN - 1]);
            rm_fill_builtin_action_combo(hDlg);
            rm_item_load_controls(hDlg, &g_rm_items[g_rm_selectedN - 1]);
            g_rm_loading = 0;
        }
        return TRUE;
    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD code = HIWORD(wParam);
        if (id == IDC_RM_SELECT_N && code == CBN_SELCHANGE) {
            int sel = CBGetCurSel(hDlg, IDC_RM_SELECT_N);
            if (sel >= 0) rm_select_item(hDlg, sel);
            return TRUE;
        }
        if (id == IDC_RM_SELECT_PREV && code == BN_CLICKED) { rm_step_selection(hDlg, -1); return TRUE; }
        if (id == IDC_RM_SELECT_NEXT && code == BN_CLICKED) { rm_step_selection(hDlg, 1); return TRUE; }
        if (id == IDC_RM_ADD && code == BN_CLICKED) { rm_add_item(hDlg); return TRUE; }
        if (id == IDC_RM_DELETE && code == BN_CLICKED) { rm_delete_item(hDlg); return TRUE; }
        if (id == IDC_RM_ALARM_SAMPLE && code == BN_CLICKED) { rm_load_alarm_sample(hDlg); return TRUE; }
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
        if (((NMHDR*)lParam)->code == PSN_APPLY) {
            /* Parent commits atomically through RM_COMMIT before dispatching other pages. */
            return TRUE;
        }
        break;
    }
    UNREFERENCED_PARAMETER(lParam);
    UNREFERENCED_PARAMETER(wParam);
    return FALSE;
}
