/*-------------------------------------------
  pagemouse.c
　　「マウス操作」プロパティページ
　　KAZUBON 1997-1998
---------------------------------------------*/

#include "tclock.h"
#include <stdio.h>
#include "plugin_actions.h"

#include "..\\common\\text_codec.h"
#include "..\\common\\ini_io_utf8.h"
#include "..\\common\\input_binding.h"

typedef struct { char* data; DWORD used, capacity; } PM_BATCH;
typedef struct { IB_ACTION action; BOOL edited, protectedAliases; char source[32]; } PM_ACTION;
typedef struct { char key[IB_KEY_CHARS]; WCHAR label[600]; } PM_COMMAND;
static PM_COMMAND* pm_commands;
static int pm_command_count;
#define PM_ACTION_ROW INT_MIN
/* Serialized UTF-8 text: match the existing 1024-byte mouse execution input. */
#define PM_TEXT_BYTES 1024
static PM_ACTION pm_actions[28][4][3];
static int pm_zone_initial[28][4];
static BOOL pm_actions_ready;
static BOOL pm_text_ready;
static BYTE pm_edit_invalid[28][4][3];
typedef struct { WCHAR* text; BOOL pending; } PM_DRAFT;
static PM_DRAFT pm_drafts[28][4][3];
static BOOL pm_restoring;
static int pm_view_button = -1, pm_view_click = -1;
static void pm_load_actions(void);
static void pm_select_action(HWND hDlg, int control, int zone);

static void OnInit(HWND hDlg);
static BOOL OnApply(HWND hDlg);
static void OnDestroy(HWND hDlg);
static void OnDropFilesChange(HWND hDlg);
static void OnMouseButton(HWND hDlg);
static void OnMouseClickTime(HWND hDlg, int id);
static void OnMouseFunc(HWND hDlg, BOOL selected);
static void OnMouseFileChange(HWND hDlg);
static void OnMouseWorkDirChange(HWND hDlg);
static BOOL GetZoneSel(HWND hDlg, int *button, int *click);
static BOOL IsZoneButton(int button);
static BOOL IsZoneContext(HWND hDlg);
static void OnZoneCount(HWND hDlg);
static void OnZoneFunc(HWND hDlg, WORD id);
static void OnZoneFileChange(HWND hDlg, WORD id);
static void OnZoneWorkDirChange(HWND hDlg, WORD id);
static void LoadZoneCurrent(HWND hDlg);
static void SaveZoneCurrent(HWND hDlg);
static BOOL pm_add_slot(PM_BATCH* batch, int button, int click);
static BOOL HasZoneStringEntry(const char* entry);
static BOOL HasRawZoneKeys(int button, int click);
static BOOL HasCompatZoneKeys(int button, int click);
static BOOL HasLegacyLeftClickZoneKeys(void);

static void OnSansho(HWND hDlg, WORD id);
static void InitMouseFuncList(HWND hDlg);
static void InitMouseFuncCombo(HWND hDlg, int ctrlId);
static void ShowZoneFuncRow(HWND hDlg, int labelId, int comboId, BOOL show);
static void SetMouseFuncComboValue(HWND hDlg, int ctrlId, int func);
static LONG GetZoneCountValue(int button, int click);
static LONG GetZoneFuncValue(int button, int click, int zone_number);
static void GetZoneFileValue(int button, int click, int zone_number, char *dst, int dst_count);
static void GetZoneWorkDirValue(int button, int click, int zone_number, char *dst, int dst_count);
static BOOL UseRightClickMenuConfig(void);
static BOOL IsRightClickRelatedButton(int button);
static int GetMouseButtonSel(HWND hDlg);
static void SetMouseButtonSel(HWND hDlg, int button);
static void MoveDlgItemY(HWND hDlg, int ctrlId, int y);
static int GetDlgItemHeight(HWND hDlg, int ctrlId);
static int DlgYFromDlu(HWND hDlg, int dlu_y);
static int AlignLabelY(HWND hDlg, int label_id, int row_id, int row_y);
static void LayoutMouseRows(HWND hDlg);
static void LayoutZoneRows(HWND hDlg, BOOL show_zone, BOOL show_file, BOOL show_workdir, BOOL show_zone2, BOOL show_zone3);
static void RefreshZoneControls(HWND hDlg);

static char reg_section[] = "Mouse";

typedef struct {
	BOOL disable;
	int func[4];
	WORD hotkey[4];
	char format[4][256];
	char fname[4][PM_TEXT_BYTES];
	int volume[4];
} CLICKDATA;
static CLICKDATA *pData = NULL;

static int zone_count_map[28][4];
static int zone_func_map[28][4][3];
static char zone_file_map[28][4][3][PM_TEXT_BYTES];
static char zone_workdir_map[28][4][3][PM_TEXT_BYTES];
static int zone_count = 1;
static int zone_func[3] = { MOUSEFUNC_NONE, MOUSEFUNC_NONE, MOUSEFUNC_NONE };
static char zone_file[3][PM_TEXT_BYTES];
static char zone_workdir[3][PM_TEXT_BYTES];
static BOOL zone_dirty_map[28][4];
static int dropfiles_initial = 0;
static char dropfiles_app_initial[PM_TEXT_BYTES];
static BOOL dropfiles_dirty = FALSE;
static BOOL dropfiles_app_dirty = FALSE;
static BOOL rightclick_menu_initial = TRUE;
static BOOL rightclick_menu_dirty = FALSE;

//#define SendPSChanged(hDlg) SendMessage(GetParent(hDlg),PSM_CHANGED,(WPARAM)(hDlg),0)

__inline void SendPSChanged(HWND hDlg)
{
	g_bApplyClock = TRUE;
	SendMessage(GetParent(hDlg), PSM_CHANGED, (WPARAM)(hDlg), 0);
}

extern BOOL b_EnglishMenu;
extern int Language_Offset;



static void ShowZoneFuncRow(HWND hDlg, int labelId, int comboId, BOOL show)
{
	ShowDlgItem(hDlg, labelId, show);
	ShowDlgItem(hDlg, comboId, show);
}

static void ShowZoneEditRow(HWND hDlg, int labelId, int editId, int browseId, BOOL show)
{
	ShowDlgItem(hDlg, labelId, show);
	ShowDlgItem(hDlg, editId, show);
	if (browseId)
		ShowDlgItem(hDlg, browseId, show);
}

static BOOL IsMousePathFunc(int func)
{
	return (func == MOUSEFUNC_OPENFILE || func == MOUSEFUNC_FILELIST || func == MOUSEFUNC_CUSTOMPROGRAM) ? TRUE : FALSE;
}

static BOOL IsMouseWorkDirFunc(int func)
{
	return (func == MOUSEFUNC_CUSTOMPROGRAM) ? TRUE : FALSE;
}

static void UpdateMainFuncLabel(HWND hDlg, BOOL show_zone)
{
	if (show_zone)
	{
		SetDlgItemTextUTF8Strict(hDlg, IDC_LABZONE1FUNC, b_EnglishMenu ? "Area 1" : "エリア1");
		return;
	}
	if (b_EnglishMenu)
		SetDlgItemTextUTF8Strict(hDlg, IDC_LABZONE1FUNC, "Function");
	else
		SetDlgItemTextUTF8Strict(hDlg, IDC_LABZONE1FUNC, "機能");
}

static void UpdatePathLabel(HWND hDlg, int labelId, int func)
{
	if (func == MOUSEFUNC_CUSTOMPROGRAM)
	{
		if (b_EnglishMenu)
			SetDlgItemTextUTF8Strict(hDlg, labelId, "Program");
		else
			SetDlgItemTextUTF8Strict(hDlg, labelId, "プログラム");
		return;
	}
	SetDlgItemTextUTF8Strict(hDlg, labelId, MyStringUTF8(IDS_FILE));
}

static BOOL pm_get_text(HWND hDlg, int control, char* output, int capacity)
{
    WCHAR wide[PM_TEXT_BYTES] = {0};
    HWND target = GetDlgItem(hDlg, control);
    int length;
    if (!target) return FALSE;
    length = GetWindowTextLengthW(target);
    int bytes;
    if (length >= (int)_countof(wide) || (int)GetDlgItemTextW(hDlg, control, wide, _countof(wide)) != length) return FALSE;
    bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0 || bytes > capacity) return FALSE;
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, output, capacity, NULL, NULL) == bytes;
}

static void pm_clear_draft(int button, int click, int zone)
{
    PM_DRAFT* draft = &pm_drafts[button][click][zone];
    if (draft->text) free(draft->text);
    draft->text = NULL; draft->pending = FALSE;
}

static void pm_clear_drafts(void)
{
    int button, click, zone;
    for (button = 0; button < 28; ++button) for (click = 0; click < 4; ++click)
        for (zone = 0; zone < 3; ++zone) pm_clear_draft(button, click, zone);
    pm_view_button = pm_view_click = -1; pm_restoring = FALSE;
}

static BOOL pm_store_draft(HWND hDlg, int control, int button, int click, int zone)
{
    PM_DRAFT* draft = &pm_drafts[button][click][zone];
    HWND edit = GetDlgItem(hDlg, control);
    WCHAR* text;
    int length;
    draft->pending = TRUE;
    if (!edit || !IsWindow(edit)) return FALSE;
    length = GetWindowTextLengthW(edit);
    if (length < 0 || length == INT_MAX || (SIZE_T)length + 1 > (SIZE_T)-1 / sizeof(*text)) return FALSE;
    text = malloc(((SIZE_T)length + 1) * sizeof(*text));
    if (!text) return FALSE;
    if (GetWindowTextW(edit, text, length + 1) != length || GetWindowTextLengthW(edit) != length) {
        free(text); return FALSE;
    }
    if (draft->text) free(draft->text);
    draft->text = text; draft->pending = FALSE;
    return TRUE;
}

static BOOL pm_read_edit(HWND hDlg, int control, int zone, char* output, int capacity)
{
    int button, click;
    if (pm_restoring || !GetZoneSel(hDlg, &button, &click)) return FALSE;
    pm_edit_invalid[button][click][zone] = !pm_get_text(hDlg, control, output, capacity);
    if (pm_edit_invalid[button][click][zone]) {
        pm_store_draft(hDlg, control, button, click, zone); return FALSE;
    }
    pm_clear_draft(button, click, zone);
    return TRUE;
}

static void pm_set_edit(HWND hDlg, int control, int zone, const char* value)
{
    int button, click;
    BOOL restoring = pm_restoring;
    PM_DRAFT* draft;
    if (!GetZoneSel(hDlg, &button, &click)) return;
    draft = &pm_drafts[button][click][zone];
    if (pm_edit_invalid[button][click][zone] && draft->pending) return;
    pm_restoring = TRUE;
    if (pm_edit_invalid[button][click][zone] && draft->text)
        SetDlgItemTextW(hDlg, control, draft->text);
    else SetDlgItemTextUTF8Strict(hDlg, control, value);
    pm_restoring = restoring;
}

static BOOL pm_leave_edits(HWND hDlg)
{
    const int controls[] = {IDC_MOUSEFILE, IDC_ZONE2FILE, IDC_ZONE3FILE};
    int zone;
    if (pm_view_button < 0 || pm_view_click < 0) return TRUE;
    for (zone = 0; zone < 3; ++zone) {
        if (!pm_edit_invalid[pm_view_button][pm_view_click][zone] ||
            !pm_drafts[pm_view_button][pm_view_click][zone].pending ||
            pm_store_draft(hDlg, controls[zone], pm_view_button, pm_view_click, zone)) continue;
        SetMouseButtonSel(hDlg, pm_view_button);
        CheckRadioButton(hDlg, IDC_RADSINGLE, IDC_RADQUADRUPLE, IDC_RADSINGLE + pm_view_click);
        MessageBoxW(hDlg, b_EnglishMenu
            ? L"Could not retain the current edit. Your input is kept here; correct it or retry before changing the input slot."
            : L"\u7de8\u96c6\u5185\u5bb9\u3092\u4fdd\u6301\u3067\u304d\u307e\u305b\u3093\u3002\n\u5165\u529b\u306f\u3053\u306e\u753b\u9762\u306b\u6b8b\u3063\u3066\u3044\u307e\u3059\u3002\u5207\u308a\u66ff\u3048\u308b\u524d\u306b\u4fee\u6b63\u3059\u308b\u304b\u3001\u518d\u8a66\u884c\u3057\u3066\u304f\u3060\u3055\u3044\u3002",
            L"TClock", MB_OK | MB_ICONERROR);
        SetFocus(GetDlgItem(hDlg, controls[zone]));
        return FALSE;
    }
    return TRUE;
}

static void pm_read_text(const char* key, char* output, int capacity, const char* fallback)
{
    char raw[4096];
    size_t length;
    tc_ini_utf8_read_string(g_inifile, reg_section, key, fallback, raw, sizeof(raw));
    length = strlen(raw);
    /* Match the profile string contract once, without an ACP output roundtrip. */
    if (length >= 2 && ((raw[0] == '"' && raw[length-1] == '"') || (raw[0] == '\'' && raw[length-1] == '\''))) {
        memmove(raw, raw + 1, length - 2); raw[length - 2] = 0; length -= 2;
    }
    if (length >= (size_t)capacity || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, -1, NULL, 0)) {
        output[0] = 0; pm_text_ready = FALSE; return;
    }
    memcpy(output, raw, length + 1);
}

static BOOL pm_read_action(const char* key, IB_ACTION* action)
{
    char value[IB_RECORD_CHARS + 2];
    tc_ini_utf8_read_string(g_inifile, reg_section, key, "\x1d", value, sizeof(value));
    if (!strcmp(value, "\x1d")) {
        /* Two defaults distinguish a missing key from a stored sentinel value. */
        tc_ini_utf8_read_string(g_inifile, reg_section, key, "\x1e", value, sizeof(value));
        if (!strcmp(value, "\x1e")) return FALSE;
    }
    if(strlen(value)>=sizeof(value)-1){ZeroMemory(action,sizeof(*action));action->kind=IB_OPAQUE;return TRUE;}
    ib_parse_action(value, action);
    if (action->kind == IB_BUILTIN && action->number < MOUSEFUNC_NONE && action->number != -32768)
        action->kind = IB_OPAQUE;
    return TRUE;
}

static void pm_load_actions(void)
{
    WCHAR path[MAX_PATH];
    int button, click, zone, alias;
    ZeroMemory(pm_actions, sizeof(pm_actions));
    pm_actions_ready = FALSE;
    if (tc_utf8_to_utf16(g_inifile, path, _countof(path)) <= 0 || !tc_begin_legacyW(path)) return;
    for (button = 0; button < 28; ++button) for (click = 0; click < 4; ++click) {
        pm_zone_initial[button][click] = zone_count_map[button][click];
        for (zone = 0; zone < 3; ++zone) {
            PM_ACTION* saved = &pm_actions[button][click][zone];
            BOOL found = FALSE;
            for (alias = 0; alias < 5; ++alias) {
                IB_ACTION action;
                char key[32];
                if (alias == 0) {
                    if (!zone) sprintf_s(key, sizeof(key), "%d%d", button, click + 1);
                    else sprintf_s(key, sizeof(key), "%d%dFunc%d", button, click + 1, zone + 1);
                } else if (alias == 1) sprintf_s(key, sizeof(key), "%d%dZone%dFunc", button, click + 1, zone + 1);
                else if (alias == 2) sprintf_s(key, sizeof(key), "%d%dZone%d", button, click + 1, zone + 1);
                else {
                    if (button || click) continue;
                    sprintf_s(key, sizeof(key), alias == 3 ? "LeftClickZone%dFunc" : "LeftClickZone%d", zone + 1);
                }
                if (!pm_read_action(key, &action)) continue;
                if (action.kind != IB_BUILTIN) saved->protectedAliases = TRUE;
                if (found || (action.kind == IB_BUILTIN && action.number == -32768)) continue;
                saved->action = action;
                strcpy_s(saved->source, sizeof(saved->source), key);
                found = TRUE;
            }
            if (saved->action.kind != IB_BUILTIN) zone_func_map[button][click][zone] = MOUSEFUNC_NONE;
        }
        pData[button].func[click] = zone_func_map[button][click][0];
    }
    tc_ini_end();
    pm_actions_ready = TRUE;
}

static BOOL pm_load_commands(void)
{
    int i, used = 0, total = pact_count();
    PM_COMMAND* commands = NULL;
    if (total < 0 || total > INT_MAX - 1 || (SIZE_T)total > (SIZE_T)-1 / sizeof(*commands)) return FALSE;
    if (total) {
        commands = calloc((size_t)total, sizeof(*commands));
        if (!commands) return FALSE;
    }
    for (i = 0; i < total; ++i) {
        const char* key = pact_get_key(i);
        const char* label = key ? pact_get_label(key, b_EnglishMenu) : NULL;
        WCHAR wide[512], stable[IB_KEY_CHARS];
        char record[IB_RECORD_CHARS];
        IB_ACTION action;
        int duplicate;
        if (!label || !key || strlen(key) >= IB_KEY_CHARS) continue;
        sprintf_s(record, sizeof(record), "-1|%s", key);
        ib_parse_action(record, &action);
        if (action.kind != IB_DLL || tc_utf8_to_utf16(label, wide, _countof(wide)) <= 0 ||
            tc_utf8_to_utf16(key, stable, _countof(stable)) <= 0) continue;
        for (duplicate = 0; duplicate < used; ++duplicate)
            if (!strcmp(commands[duplicate].key, key)) break;
        if (duplicate < used) continue;
        /* Own the catalog snapshot; combo positions and runtime slots are not persistent identities. */
        strcpy_s(commands[used].key, IB_KEY_CHARS, key);
        swprintf_s(commands[used].label, _countof(commands[0].label), L"DLL: %ls [%ls]", wide, stable);
        ++used;
    }
    if (used == pm_command_count && (!used || !memcmp(commands, pm_commands, (SIZE_T)used * sizeof(*commands)))) {
        free(commands); return FALSE;
    }
    free(pm_commands); pm_commands = commands; pm_command_count = used;
    return TRUE;
}

static void pm_refresh_commands(HWND hDlg, BOOL force)
{
    const int controls[] = {IDC_MOUSEFUNC, IDC_ZONE2FUNC, IDC_ZONE3FUNC};
    int i;
    if (!pData) return;
    if (!pm_load_commands() && !force) return;
    for (i = 0; i < 3; ++i) InitMouseFuncCombo(hDlg, controls[i]);
    for (i = 0; i < 3; ++i) {
        SetMouseFuncComboValue(hDlg, controls[i], zone_func[i]);
        pm_select_action(hDlg, controls[i], i);
    }
    /* Catalog refresh must not rewrite edit controls or publish a settings change. */
}

static BOOL pm_choose_action(HWND hDlg, int control, int zone, BOOL selected, int* function)
{
    int button, click, row, value;
    IB_ACTION action = {0};
    PM_ACTION* saved;
    if (!GetZoneSel(hDlg, &button, &click)) return FALSE;
    row = CBGetCurSel(hDlg, control);
    if (row == CB_ERR) return FALSE;
    value = (int)(INT_PTR)CBGetItemData(hDlg, control, row);
    if (value == PM_ACTION_ROW) { *function = zone_func[zone]; return TRUE; }
    if (value <= -2) {
        int index = -(value + 2);
        if (index >= pm_command_count) return FALSE;
        action.kind = IB_DLL;
        strcpy_s(action.key, sizeof(action.key), pm_commands[index].key);
        *function = MOUSEFUNC_NONE;
    } else { action.number = value; *function = value; }
    if (selected) {
        saved = &pm_actions[button][click][zone];
        ZeroMemory(saved, sizeof(*saved));
        saved->action = action;
        saved->edited = TRUE;
        if (action.kind != IB_BUILTIN || !IsMousePathFunc(value)) {
            pm_edit_invalid[button][click][zone] = FALSE;
            pm_clear_draft(button, click, zone);
        }
        zone_dirty_map[button][click] = TRUE;
    }
    return TRUE;
}

static void pm_select_action(HWND hDlg, int control, int zone)
{
    int button, click, i, row;
    PM_ACTION* saved;
    WCHAR label[640], key[IB_KEY_CHARS];
    if (!GetZoneSel(hDlg, &button, &click)) return;
    for (i = CBGetCount(hDlg, control) - 1; i >= 0; --i) {
        if ((int)(INT_PTR)CBGetItemData(hDlg, control, i) == PM_ACTION_ROW)
            SendDlgItemMessageW(hDlg, control, CB_DELETESTRING, i, 0);
    }
    saved = &pm_actions[button][click][zone];
    if (saved->action.kind == IB_BUILTIN) return;
    if (saved->action.kind == IB_DLL) {
        for (i = 0; i < CBGetCount(hDlg, control); ++i) {
            int value = (int)(INT_PTR)CBGetItemData(hDlg, control, i);
            if (value <= -2 && value != PM_ACTION_ROW) {
                int index = -(value + 2);
                if (index < pm_command_count && !strcmp(pm_commands[index].key, saved->action.key)) {
                    CBSetCurSel(hDlg, control, i); return;
                }
            }
        }
        if (tc_utf8_to_utf16(saved->action.key, key, _countof(key)) <= 0) return;
        swprintf_s(label, _countof(label), b_EnglishMenu ? L"Unavailable DLL: %ls"
            : L"\u5229\u7528\u3067\u304d\u306a\u3044 DLL: %ls", key);
    } else {
        wcscpy_s(label, _countof(label), b_EnglishMenu ? L"Unrecognized operation (preserved)"
            : L"\u672a\u5bfe\u5fdc\u306e\u64cd\u4f5c\uff08\u4fdd\u6301\u3057\u307e\u3059\uff09");
    }
    row = (int)SendDlgItemMessageW(hDlg, control, CB_ADDSTRING, 0, (LPARAM)label);
    if (row != CB_ERR && row != CB_ERRSPACE) {
        CBSetItemData(hDlg, control, row, PM_ACTION_ROW);
        CBSetCurSel(hDlg, control, row);
    }
}

static void SetMouseFuncComboValue(HWND hDlg, int ctrlId, int func)
{
	int i;
	int count = CBGetCount(hDlg, ctrlId);
	for (i = 0; i < count; i++)
	{
		if (func == (int)(INT_PTR)CBGetItemData(hDlg, ctrlId, i))
		{
			CBSetCurSel(hDlg, ctrlId, i);
			return;
		}
	}
	/* Keep omitted plugin assignments in the backing data; no replacement selection. */
	CBSetCurSel(hDlg, ctrlId, -1);
}

static LONG GetZoneCountValue(int button, int click)
{
	char entry[32];
	LONG count;
	const LONG missing = -32768;

	wsprintf(entry, "%d%dZoneCount", button, click + 1);
	count = GetMyRegLong(reg_section, entry, missing);
	if (count == missing && button == 0 && click == 0) {
		count = GetMyRegLong(reg_section, "LeftClickZoneCount", 1);
	}
	if (count == missing) count = 1;
	if (count < 1) count = 1;
	if (count > 3) count = 3;
	return count;
}

static LONG GetZoneFuncValue(int button, int click, int zone_number)
{
	char entry[32];
	LONG value;
	const LONG missing = -32768;

	if (zone_number < 1 || zone_number > 3) return MOUSEFUNC_NONE;
	if (zone_number == 1) {
		wsprintf(entry, "%d%d", button, click + 1);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	else {
		wsprintf(entry, "%d%dFunc%d", button, click + 1, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing) {
		wsprintf(entry, "%d%dZone%dFunc", button, click + 1, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing) {
		wsprintf(entry, "%d%dZone%d", button, click + 1, zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
	}
	if (value == missing && button == 0 && click == 0) {
		wsprintf(entry, "LeftClickZone%dFunc", zone_number);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value == missing) {
			wsprintf(entry, "LeftClickZone%d", zone_number);
			value = GetMyRegLong(reg_section, entry, missing);
		}
	}
	if (value == missing) value = MOUSEFUNC_NONE;
	return value;
}

static void GetZoneFileValue(int button, int click, int zone_number, char *dst, int dst_count)
{
	char entry[32];
	char missing[2] = { 1, 0 };

	if (!dst || dst_count <= 0) return;
	dst[0] = 0;
	if (zone_number < 1 || zone_number > 3) return;
	if (zone_number == 1)
		wsprintf(entry, "%d%dFile", button, click + 1);
	else
		wsprintf(entry, "%d%dFile%d", button, click + 1, zone_number);
	pm_read_text(entry, dst, dst_count, missing);
	if (dst[0] == missing[0] && dst[1] == 0)
	{
		wsprintf(entry, "%d%dZone%dFile", button, click + 1, zone_number);
		pm_read_text(entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0 && button == 0 && click == 0)
	{
		wsprintf(entry, "LeftClickZone%dFile", zone_number);
		pm_read_text(entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0) dst[0] = 0;
}

static void GetZoneWorkDirValue(int button, int click, int zone_number, char *dst, int dst_count)
{
	char entry[32];
	char missing[2] = { 1, 0 };

	if (!dst || dst_count <= 0) return;
	dst[0] = 0;
	if (zone_number < 1 || zone_number > 3) return;
	if (zone_number == 1)
		wsprintf(entry, "%d%dWorkDir", button, click + 1);
	else
		wsprintf(entry, "%d%dWorkDir%d", button, click + 1, zone_number);
	pm_read_text(entry, dst, dst_count, missing);
	if (dst[0] == missing[0] && dst[1] == 0)
	{
		wsprintf(entry, "%d%dZone%dWorkDir", button, click + 1, zone_number);
		pm_read_text(entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0 && button == 0 && click == 0)
	{
		wsprintf(entry, "LeftClickZone%dWorkDir", zone_number);
		pm_read_text(entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0) dst[0] = 0;
}

static BOOL GetZoneSel(HWND hDlg, int *button, int *click)
{
	int current_button;
	int current_click;

	current_button = GetMouseButtonSel(hDlg);
	if (!pData || current_button < 0 || current_button >= 28) return FALSE;
	for (current_click = 0; current_click < 4; current_click++)
	{
		if (IsDlgButtonChecked(hDlg, IDC_RADSINGLE + current_click)) break;
	}
	if (current_click >= 4) return FALSE;
	if (button) *button = current_button;
	if (click) *click = current_click;
	return TRUE;
}

static BOOL UseRightClickMenuConfig(void)
{
	return GetMyRegLong("Mouse", "RightClickMenu", TRUE) ? TRUE : FALSE;
}

static BOOL IsRightClickRelatedButton(int button)
{
	if (button == (IDS_RIGHTBUTTON - IDS_LEFTBUTTON)) return TRUE;
	if (button == (IDS_CRIGHTBUTTON - IDS_LEFTBUTTON)) return TRUE;
	if (button == (IDS_SRIGHTBUTTON - IDS_LEFTBUTTON)) return TRUE;
	return FALSE;
}

static int GetMouseButtonSel(HWND hDlg)
{
	int index;
	int button;

	index = CBGetCurSel(hDlg, IDC_MOUSEBUTTON);
	if (index == CB_ERR) return CB_ERR;
	button = (int)(INT_PTR)CBGetItemData(hDlg, IDC_MOUSEBUTTON, index);
	if (button == CB_ERR) return CB_ERR;
	return button;
}

static void SetMouseButtonSel(HWND hDlg, int button)
{
	int count;
	int index;

	count = CBGetCount(hDlg, IDC_MOUSEBUTTON);
	for (index = 0; index < count; index++)
	{
		if ((int)(INT_PTR)CBGetItemData(hDlg, IDC_MOUSEBUTTON, index) == button)
		{
			CBSetCurSel(hDlg, IDC_MOUSEBUTTON, index);
			return;
		}
	}
	CBSetCurSel(hDlg, IDC_MOUSEBUTTON, 0);
}

static BOOL IsZoneButton(int button)
{
	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON)) return FALSE;
	if (button == (IDS_WHEEL1 - IDS_LEFTBUTTON) ||
		button == (IDS_WHEEL2 - IDS_LEFTBUTTON) ||
		button == (IDS_CWHEEL1 - IDS_LEFTBUTTON) ||
		button == (IDS_CWHEEL2 - IDS_LEFTBUTTON) ||
		button == (IDS_SWHEEL1 - IDS_LEFTBUTTON) ||
		button == (IDS_SWHEEL2 - IDS_LEFTBUTTON)) {
		return FALSE;
	}
	return TRUE;
}

static void LoadZoneCurrent(HWND hDlg)
{
	int button;
	int click;
	int i;

	if (!GetZoneSel(hDlg, &button, &click)) return;
    pm_view_button = button; pm_view_click = click;
	zone_count = zone_count_map[button][click];
	for (i = 0; i < 3; i++)
	{
		zone_func[i] = zone_func_map[button][click][i];
		lstrcpyn(zone_file[i], zone_file_map[button][click][i], (int)sizeof(zone_file[i]));
		lstrcpyn(zone_workdir[i], zone_workdir_map[button][click][i], (int)sizeof(zone_workdir[i]));
	}
	pData[button].func[click] = zone_func[0];
	lstrcpyn(pData[button].fname[click], zone_file[0], (int)sizeof(pData[button].fname[click]));
}

static void SaveZoneCurrent(HWND hDlg)
{
	int button;
	int click;
	int i;
	BOOL changed = FALSE;

	if (!GetZoneSel(hDlg, &button, &click)) return;
	if (zone_count_map[button][click] != zone_count) changed = TRUE;
	for (i = 0; i < 3; i++)
	{
		if (zone_func_map[button][click][i] != zone_func[i]) changed = TRUE;
		if (lstrcmp(zone_file_map[button][click][i], zone_file[i]) != 0) changed = TRUE;
		if (lstrcmp(zone_workdir_map[button][click][i], zone_workdir[i]) != 0) changed = TRUE;
		zone_func_map[button][click][i] = zone_func[i];
		lstrcpyn(zone_file_map[button][click][i], zone_file[i], (int)sizeof(zone_file_map[button][click][i]));
		lstrcpyn(zone_workdir_map[button][click][i], zone_workdir[i], (int)sizeof(zone_workdir_map[button][click][i]));
	}
	if (pData[button].func[click] != zone_func[0]) changed = TRUE;
	if (lstrcmp(pData[button].fname[click], zone_file[0]) != 0) changed = TRUE;
	zone_count_map[button][click] = zone_count;
	pData[button].func[click] = zone_func[0];
	lstrcpyn(pData[button].fname[click], zone_file[0], (int)sizeof(pData[button].fname[click]));
	if (changed) zone_dirty_map[button][click] = TRUE;
}

static BOOL HasZoneStringEntry(const char* entry)
{
	char value[4];
	char missing[2] = { 1, 0 };

	GetMyRegStr(reg_section, entry, value, (int)sizeof(value), missing);
	return !(value[0] == missing[0] && value[1] == 0);
}

static BOOL HasRawZoneKeys(int button, int click)
{
	char entry[32];
	LONG value;
	const LONG missing = -32768;
	int k;

	wsprintf(entry, "%d%dZoneCount", button, click + 1);
	value = GetMyRegLong(reg_section, entry, missing);
	if (value != missing) return TRUE;
	for (k = 2; k <= 3; k++)
	{
		wsprintf(entry, "%d%dFunc%d", button, click + 1, k);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value != missing) return TRUE;
		wsprintf(entry, "%d%dFile%d", button, click + 1, k);
		if (HasZoneStringEntry(entry)) return TRUE;
		wsprintf(entry, "%d%dWorkDir%d", button, click + 1, k);
		if (HasZoneStringEntry(entry)) return TRUE;
	}
	if (HasCompatZoneKeys(button, click)) return TRUE;
	return FALSE;
}

static BOOL HasCompatZoneKeys(int button, int click)
{
	char entry[32];
	LONG value;
	const LONG missing = -32768;
	int k;

	for (k = 1; k <= 3; k++)
	{
		wsprintf(entry, "%d%dZone%dFunc", button, click + 1, k);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value != missing) return TRUE;
		wsprintf(entry, "%d%dZone%d", button, click + 1, k);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value != missing) return TRUE;
		wsprintf(entry, "%d%dZone%dFile", button, click + 1, k);
		if (HasZoneStringEntry(entry)) return TRUE;
		wsprintf(entry, "%d%dZone%dWorkDir", button, click + 1, k);
		if (HasZoneStringEntry(entry)) return TRUE;
	}
	return FALSE;
}

static BOOL HasLegacyLeftClickZoneKeys(void)
{
	char entry[32];
	LONG value;
	const LONG missing = -32768;
	int k;

	value = GetMyRegLong(reg_section, "LeftClickZoneCount", missing);
	if (value != missing) return TRUE;
	for (k = 1; k <= 3; k++)
	{
		wsprintf(entry, "LeftClickZone%dFunc", k);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value != missing) return TRUE;
		wsprintf(entry, "LeftClickZone%d", k);
		value = GetMyRegLong(reg_section, entry, missing);
		if (value != missing) return TRUE;
		wsprintf(entry, "LeftClickZone%dFile", k);
		if (HasZoneStringEntry(entry)) return TRUE;
		wsprintf(entry, "LeftClickZone%dWorkDir", k);
		if (HasZoneStringEntry(entry)) return TRUE;
	}
	return FALSE;
}

static void MoveDlgItemY(HWND hDlg, int ctrlId, int y)
{
	HWND hCtrl;
	RECT rc;
	POINT pt[2];

	hCtrl = GetDlgItem(hDlg, ctrlId);
	if (!hCtrl) return;
	GetWindowRect(hCtrl, &rc);
	pt[0].x = rc.left;
	pt[0].y = rc.top;
	pt[1].x = rc.right;
	pt[1].y = rc.bottom;
	MapWindowPoints(HWND_DESKTOP, hDlg, pt, 2);
	SetWindowPos(hCtrl, NULL, pt[0].x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

static int GetDlgItemY(HWND hDlg, int ctrlId)
{
	HWND hCtrl;
	RECT rc;
	POINT pt;

	hCtrl = GetDlgItem(hDlg, ctrlId);
	if (!hCtrl) return 0;
	GetWindowRect(hCtrl, &rc);
	pt.x = rc.left;
	pt.y = rc.top;
	ScreenToClient(hDlg, &pt);
	return pt.y;
}

static int GetDlgItemHeight(HWND hDlg, int ctrlId)
{
	HWND hCtrl;
	RECT rc;

	hCtrl = GetDlgItem(hDlg, ctrlId);
	if (!hCtrl) return 0;
	GetWindowRect(hCtrl, &rc);
	return rc.bottom - rc.top;
}

static int DlgYFromDlu(HWND hDlg, int dlu_y)
{
	RECT rc;

	rc.left = 0;
	rc.top = 0;
	rc.right = 0;
	rc.bottom = dlu_y;
	MapDialogRect(hDlg, &rc);
	return rc.bottom;
}

static int AlignLabelY(HWND hDlg, int label_id, int row_id, int row_y)
{
	int label_h;
	int row_h;

	label_h = GetDlgItemHeight(hDlg, label_id);
	row_h = GetDlgItemHeight(hDlg, row_id);
	if (label_h <= 0 || row_h <= 0) return row_y;
	return row_y + ((row_h - label_h) / 2);
}

static void LayoutMouseRows(HWND hDlg)
{
	int group_y_dlu;
	int button_y_dlu;
	int radio_y_dlu;
	int zone_y_dlu;
	int group_y;
	int button_y;
	int radio_y;
	int zone_y;

	group_y_dlu = 43;
	button_y_dlu = 51;
	radio_y_dlu = 67;
	zone_y_dlu = 83;
	group_y = DlgYFromDlu(hDlg, group_y_dlu);
	button_y = DlgYFromDlu(hDlg, button_y_dlu);
	radio_y = DlgYFromDlu(hDlg, radio_y_dlu);
	zone_y = DlgYFromDlu(hDlg, zone_y_dlu);

	MoveDlgItemY(hDlg, IDC_LABMOUSEBUTTON, group_y);
	MoveDlgItemY(hDlg, IDC_MOUSEBUTTON, button_y);
	MoveDlgItemY(hDlg, IDC_HOTKEY, button_y);
	MoveDlgItemY(hDlg, IDC_RADSINGLE, radio_y);
	MoveDlgItemY(hDlg, IDC_RADDOUBLE, radio_y);
	MoveDlgItemY(hDlg, IDC_RADTRIPLE, radio_y);
	MoveDlgItemY(hDlg, IDC_RADQUADRUPLE, radio_y);
	MoveDlgItemY(hDlg, IDC_LABZONEBLOCK, AlignLabelY(hDlg, IDC_LABZONEBLOCK, IDC_ZONECOUNT, zone_y));
	MoveDlgItemY(hDlg, IDC_LABZONECOUNT, AlignLabelY(hDlg, IDC_LABZONECOUNT, IDC_ZONECOUNT, zone_y));
	MoveDlgItemY(hDlg, IDC_ZONECOUNT, zone_y);
}

static void LayoutZoneRows(HWND hDlg, BOOL show_zone, BOOL show_file, BOOL show_workdir, BOOL show_zone2, BOOL show_zone3)
{
	int next_y;
	int zone_row_y;
	const int row_pitch = DlgYFromDlu(hDlg, 16);
	const int file_gap = 0;
	BOOL show_zone2_file = show_zone2 && IsMousePathFunc(zone_func[1]);
	BOOL show_zone3_file = show_zone3 && IsMousePathFunc(zone_func[2]);
	(void)show_workdir;

	zone_row_y = GetDlgItemY(hDlg, IDC_ZONECOUNT);
	next_y = zone_row_y + row_pitch;

	if (!show_zone)
	{
		MoveDlgItemY(hDlg, IDC_LABZONE1FUNC, AlignLabelY(hDlg, IDC_LABZONE1FUNC, IDC_MOUSEFUNC, next_y));
		MoveDlgItemY(hDlg, IDC_MOUSEFUNC, next_y);
		next_y += row_pitch;
		if (show_file)
		{
			MoveDlgItemY(hDlg, IDC_LABMOUSEFILE, AlignLabelY(hDlg, IDC_LABMOUSEFILE, IDC_MOUSEFILE, next_y));
			MoveDlgItemY(hDlg, IDC_MOUSEFILE, next_y);
			MoveDlgItemY(hDlg, IDC_MOUSEFILESANSHO, next_y);
			next_y += row_pitch + file_gap;
		}
		return;
	}

	MoveDlgItemY(hDlg, IDC_LABZONE1FUNC, AlignLabelY(hDlg, IDC_LABZONE1FUNC, IDC_MOUSEFUNC, next_y));
	MoveDlgItemY(hDlg, IDC_MOUSEFUNC, next_y);
	next_y += row_pitch;
	if (show_file)
	{
		MoveDlgItemY(hDlg, IDC_LABMOUSEFILE, AlignLabelY(hDlg, IDC_LABMOUSEFILE, IDC_MOUSEFILE, next_y));
		MoveDlgItemY(hDlg, IDC_MOUSEFILE, next_y);
		MoveDlgItemY(hDlg, IDC_MOUSEFILESANSHO, next_y);
		next_y += row_pitch + file_gap;
	}
	if (show_zone2)
	{
		MoveDlgItemY(hDlg, IDC_LABZONE2FUNC, AlignLabelY(hDlg, IDC_LABZONE2FUNC, IDC_ZONE2FUNC, next_y));
		MoveDlgItemY(hDlg, IDC_ZONE2FUNC, next_y);
		next_y += row_pitch;
		if (show_zone2_file)
		{
			MoveDlgItemY(hDlg, IDC_LABZONE2FILE, AlignLabelY(hDlg, IDC_LABZONE2FILE, IDC_ZONE2FILE, next_y));
			MoveDlgItemY(hDlg, IDC_ZONE2FILE, next_y);
			MoveDlgItemY(hDlg, IDC_ZONE2FILESANSHO, next_y);
			next_y += row_pitch + file_gap;
		}
	}
	if (show_zone3)
	{
		MoveDlgItemY(hDlg, IDC_LABZONE3FUNC, AlignLabelY(hDlg, IDC_LABZONE3FUNC, IDC_ZONE3FUNC, next_y));
		MoveDlgItemY(hDlg, IDC_ZONE3FUNC, next_y);
		next_y += row_pitch;
		if (show_zone3_file)
		{
			MoveDlgItemY(hDlg, IDC_LABZONE3FILE, AlignLabelY(hDlg, IDC_LABZONE3FILE, IDC_ZONE3FILE, next_y));
			MoveDlgItemY(hDlg, IDC_ZONE3FILE, next_y);
			MoveDlgItemY(hDlg, IDC_ZONE3FILESANSHO, next_y);
			next_y += row_pitch + file_gap;
		}
	}
}

static BOOL IsZoneContext(HWND hDlg)
{
	int button;

	if (!GetZoneSel(hDlg, &button, NULL)) return FALSE;
	return IsZoneButton(button);
}

static void RefreshZoneControls(HWND hDlg)
{
	BOOL show_zone = IsZoneContext(hDlg);
	int func = zone_func[0];
	BOOL show_file = IsMousePathFunc(func);
	BOOL show_workdir = FALSE;
	BOOL show_zone2 = (zone_count >= 2) ? TRUE : FALSE;
	BOOL show_zone3 = (zone_count >= 3) ? TRUE : FALSE;
	BOOL show_zone2_file = show_zone2 && IsMousePathFunc(zone_func[1]);
	BOOL show_zone3_file = show_zone3 && IsMousePathFunc(zone_func[2]);

	CBSetCurSel(hDlg, IDC_ZONECOUNT, zone_count - 1);
	if (show_zone)
		SetMouseFuncComboValue(hDlg, IDC_MOUSEFUNC, zone_func[0]);
	SetMouseFuncComboValue(hDlg, IDC_ZONE2FUNC, zone_func[1]);
	SetMouseFuncComboValue(hDlg, IDC_ZONE3FUNC, zone_func[2]);
	ShowDlgItem(hDlg, IDC_LABZONEBLOCK, show_zone);
	ShowDlgItem(hDlg, IDC_LABZONECOUNT, FALSE);
	ShowDlgItem(hDlg, IDC_ZONECOUNT, show_zone);
	ShowDlgItem(hDlg, IDC_LABZONE1FUNC, TRUE);
	ShowZoneFuncRow(hDlg, IDC_LABZONE2FUNC, IDC_ZONE2FUNC, show_zone && show_zone2);
	ShowZoneFuncRow(hDlg, IDC_LABZONE3FUNC, IDC_ZONE3FUNC, show_zone && show_zone3);
	UpdateMainFuncLabel(hDlg, show_zone);
	UpdatePathLabel(hDlg, IDC_LABMOUSEFILE, func);
	UpdatePathLabel(hDlg, IDC_LABZONE2FILE, zone_func[1]);
	UpdatePathLabel(hDlg, IDC_LABZONE3FILE, zone_func[2]);
	ShowDlgItem(hDlg, IDC_LABMOUSEFILE, show_file);
	ShowDlgItem(hDlg, IDC_MOUSEFILE, show_file);
	ShowDlgItem(hDlg, IDC_MOUSEFILESANSHO, show_file);
	ShowZoneEditRow(hDlg, IDC_LABZONE2FILE, IDC_ZONE2FILE, IDC_ZONE2FILESANSHO, show_zone && show_zone2_file);
	ShowZoneEditRow(hDlg, IDC_LABZONE3FILE, IDC_ZONE3FILE, IDC_ZONE3FILESANSHO, show_zone && show_zone3_file);
	if (show_file)
		pm_set_edit(hDlg, IDC_MOUSEFILE, 0, zone_file[0]);
	if (show_zone2_file)
		pm_set_edit(hDlg, IDC_ZONE2FILE, 1, zone_file[1]);
	if (show_zone3_file)
		pm_set_edit(hDlg, IDC_ZONE3FILE, 2, zone_file[2]);
	pm_select_action(hDlg, IDC_MOUSEFUNC, 0);
	pm_select_action(hDlg, IDC_ZONE2FUNC, 1);
	pm_select_action(hDlg, IDC_ZONE3FUNC, 2);
	LayoutMouseRows(hDlg);
	LayoutZoneRows(hDlg, show_zone, show_file, show_workdir, show_zone2, show_zone3);
}

/*------------------------------------------------
　「マウス操作」ページ用ダイアログプロシージャ
--------------------------------------------------*/
BOOL CALLBACK PageMouseProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_INITDIALOG:
			OnInit(hDlg);
			return TRUE;
        case WM_SHOWWINDOW:
            if (wParam && pData) pm_refresh_commands(hDlg, TRUE);
            return TRUE;
		case WM_COMMAND:
		{
			WORD id, code;
			id = LOWORD(wParam); code = HIWORD(wParam);
            if (pm_restoring && code == EN_CHANGE && (id == IDC_MOUSEFILE || id == IDC_ZONE2FILE || id == IDC_ZONE3FILE)) return TRUE;
            if (((id == IDC_MOUSEBUTTON && code == CBN_SELCHANGE) ||
                (id >= IDC_RADSINGLE && id <= IDC_RADQUADRUPLE && code == BN_CLICKED)) && !pm_leave_edits(hDlg)) return TRUE;
            if (code == CBN_DROPDOWN && (id == IDC_MOUSEFUNC || id == IDC_ZONE2FUNC || id == IDC_ZONE3FUNC)) {
                pm_refresh_commands(hDlg, FALSE);
                return TRUE;
            }
			switch(id)
			{
			// "Drop files"
			case IDC_DROPFILES:
				if(code == CBN_SELCHANGE)
				{
					OnDropFilesChange(hDlg);
					g_bApplyClock = TRUE;
				}
				break;
			case IDC_DROPFILESAPP:
				if(code == EN_CHANGE)
				{
					char s[PM_TEXT_BYTES];
                    dropfiles_app_dirty = !pm_get_text(hDlg, IDC_DROPFILESAPP, s, sizeof(s)) ||
                        lstrcmp(s, dropfiles_app_initial) != 0;
					SendPSChanged(hDlg);
				}
				break;
			// "..."
			case IDC_DROPFILESAPPSANSHO:
			case IDC_MOUSEFILESANSHO:
			case IDC_ZONE2FILESANSHO:
			case IDC_ZONE3FILESANSHO:
				OnSansho(hDlg, id);
				break;
			case IDC_ZONECOUNT:
				if(code == CBN_SELCHANGE)
				{
					OnZoneCount(hDlg);
					SendPSChanged(hDlg);
				}
				break;
			case IDC_ZONE2FUNC:
			case IDC_ZONE3FUNC:
				if(code == CBN_SELCHANGE)
				{
					OnZoneFunc(hDlg, id);
					SendPSChanged(hDlg);
				}
				break;
			//  "Button"
			case IDC_MOUSEBUTTON:
				if(code == CBN_SELCHANGE)
					OnMouseButton(hDlg);
				break;
			// single .... quadruple
			case IDC_RADSINGLE:
			case IDC_RADDOUBLE:
			case IDC_RADTRIPLE:
			case IDC_RADQUADRUPLE:
				OnMouseClickTime(hDlg, id);
				break;
			//  Mouse Function
			case IDC_MOUSEFUNC:
				if(code == CBN_SELCHANGE)
				{
					OnMouseFunc(hDlg, TRUE);
					SendPSChanged(hDlg);
				}
				break;
			// Mouse Function - File
			case IDC_MOUSEFILE:
				if(code == EN_CHANGE)
				{
					OnMouseFileChange(hDlg);
					SendPSChanged(hDlg);
				}
				break;
			case IDC_ZONE2FILE:
			case IDC_ZONE3FILE:
				if(code == EN_CHANGE)
				{
					OnZoneFileChange(hDlg, id);
					SendPSChanged(hDlg);
				}
				break;
			case IDC_HOTKEY:
				OnMouseFunc(hDlg, FALSE);
				SendPSChanged(hDlg);
				break;
			case IDC_RCLICKMENU:
				rightclick_menu_dirty =
					(((IsDlgButtonChecked(hDlg, IDC_RCLICKMENU) == BST_CHECKED) ? TRUE : FALSE) != rightclick_menu_initial);
				SendPSChanged(hDlg);
				break;
			}
			return TRUE;
		}
        case MOUSE_COMMIT:
            if (OnApply(hDlg)) {
                SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            if (GetLastError() == ERROR_CANCELLED) {
                SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, FALSE);
                return TRUE;
            }
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                MessageBoxW(hDlg, b_EnglishMenu
                    ? L"The text is too long or contains invalid Unicode. Your edits are kept; correct it and retry."
                    : L"\u6587\u5b57\u5217\u304c\u9577\u3059\u304e\u308b\u304b\u3001Unicode \u6587\u5b57\u304c\u4e0d\u6b63\u3067\u3059\u3002\n\u7de8\u96c6\u5185\u5bb9\u306f\u4fdd\u6301\u3057\u3066\u3044\u307e\u3059\u3002\u4fee\u6b63\u3057\u3066\u518d\u8a66\u884c\u3057\u3066\u304f\u3060\u3055\u3044\u3002",
                    L"TClock", MB_OK | MB_ICONERROR);
            else
            MessageBoxW(hDlg,
                b_EnglishMenu
                    ? L"Could not save mouse settings. Your edits are kept; resolve the write error and retry."
                    : L"\u30de\u30a6\u30b9\u8a2d\u5b9a\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002\n\u7de8\u96c6\u5185\u5bb9\u306f\u4fdd\u6301\u3057\u3066\u3044\u307e\u3059\u3002\n\u66f8\u304d\u8fbc\u307f\u30a8\u30e9\u30fc\u3092\u89e3\u6d88\u3057\u3066\u518d\u8a66\u884c\u3057\u3066\u304f\u3060\u3055\u3044\u3002",
                L"TClock", MB_OK | MB_ICONERROR);
            SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, FALSE);
            return TRUE;
		case WM_NOTIFY:
			switch(((NMHDR *)lParam)->code)
			{
				case PSN_APPLY:
                    SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, OnApply(hDlg) ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE);
                    break;
			}
			return TRUE;
		case WM_DESTROY:
			OnDestroy(hDlg);
			return TRUE;
	}
	return FALSE;
}

/*------------------------------------------------
　ページの初期化
--------------------------------------------------*/

void OnInit(HWND hDlg)
{
    WCHAR iniPath[MAX_PATH];
    BOOL scoped;
	char s[PM_TEXT_BYTES];
	char entry[20];
	int i, j, k, n;
	HFONT hfont;

    pm_text_ready = TRUE;
    pm_clear_drafts();
    ZeroMemory(pm_edit_invalid, sizeof(pm_edit_invalid));
    scoped = tc_utf8_to_utf16(g_inifile, iniPath, _countof(iniPath)) > 0 && tc_begin_legacyW(iniPath);
    if (!scoped) pm_text_ready = FALSE;
	hfont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	if(hfont)
	{
		SendDlgItemMessage(hDlg, IDC_DROPFILESAPP,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_MOUSEFILE,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_ZONE2FILE,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_ZONE3FILE,
			WM_SETFONT, (WPARAM)hfont, 0);
		SendDlgItemMessage(hDlg, IDC_TOOLTIP,
			WM_SETFONT, (WPARAM)hfont, 0);
	}

	for(i = IDS_NONE; i <= IDS_MOVETO; i++)
		CBAddStringUTF8Compat(hDlg, IDC_DROPFILES, MyStringUTF8(i));
	CBSetCurSel(hDlg, IDC_DROPFILES,
		GetMyRegLong(reg_section, "DropFiles", 0));
	pm_read_text("DropFilesApp", s, sizeof(s), "");
	SetDlgItemTextUTF8Strict(hDlg, IDC_DROPFILESAPP, s);
	dropfiles_initial = CBGetCurSel(hDlg, IDC_DROPFILES);
	lstrcpyn(dropfiles_app_initial, s, (int)sizeof(dropfiles_app_initial));
	dropfiles_dirty = FALSE;
	dropfiles_app_dirty = FALSE;
	rightclick_menu_initial = UseRightClickMenuConfig();
	CheckDlgButton(hDlg, IDC_RCLICKMENU, rightclick_menu_initial ? BST_CHECKED : BST_UNCHECKED);
	rightclick_menu_dirty = FALSE;

	pData = malloc(sizeof(CLICKDATA) * 28);
	ZeroMemory(zone_dirty_map, sizeof(zone_dirty_map));

	for(i = 0; i < 28; i++)
	{
		for(j = 0; j < 4; j++)
		{
			wsprintf(entry, "%d%d", i, j+1);
			pData[i].disable = FALSE;
			pData[i].func[j] =
				GetMyRegLong(reg_section, entry, MOUSEFUNC_NONE);
			pData[i].format[j][0] = 0;
			pData[i].fname[j][0] = 0;
			pData[i].hotkey[j] = 0;
			if(i == IDS_HOTKEY - IDS_LEFTBUTTON)
			{
				wsprintf(entry, "%d%dHotkey", i, j+1);
				pData[i].hotkey[j] = (WORD)GetMyRegLong(reg_section, entry, 0);
			}

			else if(IsMousePathFunc(pData[i].func[j]))
			{
				wsprintf(entry, "%d%dFile", i, j+1);
				pm_read_text(entry, pData[i].fname[j], sizeof(pData[i].fname[j]), "");
			}
			zone_count_map[i][j] = (int)GetZoneCountValue(i, j);
			for (n = 0; n < 3; n++) {
				zone_func_map[i][j][n] = (int)GetZoneFuncValue(i, j, n + 1);
				GetZoneFileValue(i, j, n + 1, zone_file_map[i][j][n], (int)sizeof(zone_file_map[i][j][n]));
				GetZoneWorkDirValue(i, j, n + 1, zone_workdir_map[i][j][n], (int)sizeof(zone_workdir_map[i][j][n]));
			}
			pData[i].func[j] = zone_func_map[i][j][0];
			lstrcpyn(pData[i].fname[j], zone_file_map[i][j][0], (int)sizeof(pData[i].fname[j]));
			if ((zone_count_map[i][j] <= 1 && HasRawZoneKeys(i, j)) ||
				(zone_count_map[i][j] > 1 && HasCompatZoneKeys(i, j)) ||
				(i == 0 && j == 0 && HasLegacyLeftClickZoneKeys()))
				zone_dirty_map[i][j] = TRUE;
		}
	}


    pm_load_actions();
    pm_load_commands();

	for(i = IDS_LEFTBUTTON; i <= IDS_SWHEEL2; i++)
	{
		int button;
		int index;

		button = i - IDS_LEFTBUTTON;
		if (UseRightClickMenuConfig() && IsRightClickRelatedButton(button)) continue;
		index = CBAddStringUTF8Compat(hDlg, IDC_MOUSEBUTTON, MyStringUTF8(i));
		CBSetItemData(hDlg, IDC_MOUSEBUTTON, index, button);
	}
	AdjustDlgConboBoxDropDown(hDlg, IDC_MOUSEBUTTON, 22);
	// set mouse functions to combo box
	InitMouseFuncList(hDlg);
	InitMouseFuncCombo(hDlg, IDC_ZONE2FUNC);
	InitMouseFuncCombo(hDlg, IDC_ZONE3FUNC);
	CBAddStringUTF8Compat(hDlg, IDC_ZONECOUNT, "1");
	CBAddStringUTF8Compat(hDlg, IDC_ZONECOUNT, "2");
	CBAddStringUTF8Compat(hDlg, IDC_ZONECOUNT, "3");
	for (k = 0; k < 3; k++)
	{
		zone_file[k][0] = 0;
		zone_workdir[k][0] = 0;
	}
	RefreshZoneControls(hDlg);

	SetMouseButtonSel(hDlg, 0);
	OnDropFilesChange(hDlg);
	OnMouseButton(hDlg);
    if (scoped) tc_ini_end();
}

static BOOL pm_add_value(PM_BATCH* batch, const char* key, const char* value)
{
    size_t keyBytes = strlen(key), valueBytes = value ? strlen(value) : 0;
    DWORD need, capacity;
    char* next;
    if (!keyBytes || keyBytes >= 128 || valueBytes > 4095) return FALSE;
    need = batch->used + (DWORD)keyBytes + (DWORD)valueBytes + (value ? 1 : 0) + 2;
    if (need > TC_PATCH_MAX_BYTES) return FALSE;
    if (need > batch->capacity) {
        capacity = batch->capacity ? batch->capacity : 4096;
        while (capacity < need) capacity *= 2;
        next = batch->data ? (char*)HeapReAlloc(GetProcessHeap(), 0, batch->data, capacity)
                           : (char*)HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!next) return FALSE;
        batch->data = next; batch->capacity = capacity;
    }
    memcpy(batch->data + batch->used, key, keyBytes); batch->used += (DWORD)keyBytes;
    if (value) {
        batch->data[batch->used++] = '=';
        memcpy(batch->data + batch->used, value, valueBytes); batch->used += (DWORD)valueBytes;
    }
    batch->data[batch->used++] = 0;
    batch->data[batch->used] = 0;
    return TRUE;
}

static BOOL pm_add_number(PM_BATCH* batch, const char* key, int value, BOOL sparse)
{
    char number[32];
    int i;
    sprintf_s(number, sizeof(number), "%d", value);
    if (sparse) for (i = 0; i < seed_count(); ++i) {
        const char *section, *entry, *defaultValue;
        BOOL emit, eligible;
        if (seed_get(i, &section, &entry, &defaultValue, &emit, &eligible) && eligible &&
            !strcmp(section, reg_section) && !strcmp(entry, key)) {
            if (!strcmp(defaultValue, number)) return pm_add_value(batch, key, NULL);
            break;
        }
    }
    return pm_add_value(batch, key, number);
}

static BOOL pm_add_text(PM_BATCH* batch, const char* key, const char* value)
{
    char quoted[PM_TEXT_BYTES + 2];
    /* Existing page strings are serialized UTF-8 settings, not OS path arguments. */
    if (strpbrk(value, "\"' ")) {
        if (strlen(value) > sizeof(quoted) - 3) return FALSE;
        sprintf_s(quoted, sizeof(quoted), "\"%s\"", value);
        return pm_add_value(batch, key, quoted);
    }
    return pm_add_value(batch, key, value);
}

static BOOL pm_add_slot(PM_BATCH* batch, int button, int click)
{
    char key[32];
    int zone, count = zone_count_map[button][click];
    BOOL protected = FALSE;
    if (count < 1) count = 1;
    if (count > 3) count = 3;
    for (zone = 0; zone < 3; ++zone) {
        PM_ACTION* saved = &pm_actions[button][click][zone];
        if (saved->protectedAliases && !saved->edited) protected = TRUE;
    }
    if (!protected || count != pm_zone_initial[button][click]) {
        sprintf_s(key, sizeof(key), "%d%dZoneCount", button, click + 1);
        if (count > 1) { if (!pm_add_number(batch, key, count, FALSE)) return FALSE; }
        else if (!pm_add_value(batch, key, NULL)) return FALSE;
        if (!button && !click && !pm_add_value(batch, "LeftClickZoneCount", NULL)) return FALSE;
    }
    for (zone = 0; zone < 3; ++zone) {
        PM_ACTION* saved = &pm_actions[button][click][zone];
        int func = zone_func_map[button][click][zone];
        const char* formats[] = { "%d%dZone%dFunc", "%d%dZone%d", "%d%dZone%dFile", "%d%dZone%dWorkDir" };
        int alias;
        /* Unedited external records stay at their original keys, without truncation or migration. */
        if (saved->protectedAliases && !saved->edited) continue;
        for (alias = 0; alias < 4; ++alias) {
            sprintf_s(key, sizeof(key), formats[alias], button, click + 1, zone + 1);
            if (!pm_add_value(batch, key, NULL)) return FALSE;
        }
        if (!button && !click) {
            const char* old[] = { "LeftClickZone%dFunc", "LeftClickZone%d", "LeftClickZone%dFile", "LeftClickZone%dWorkDir" };
            for (alias = 0; alias < 4; ++alias) {
                sprintf_s(key, sizeof(key), old[alias], zone + 1);
                if (!pm_add_value(batch, key, NULL)) return FALSE;
            }
        }
        if (!zone) sprintf_s(key, sizeof(key), "%d%d", button, click + 1);
        else sprintf_s(key, sizeof(key), "%d%dFunc%d", button, click + 1, zone + 1);
        if (zone < count && saved->action.kind == IB_DLL) {
            char record[IB_RECORD_CHARS];
            sprintf_s(record, sizeof(record), "-1|%s", saved->action.key);
            if (!pm_add_value(batch, key, record)) return FALSE;
        } else if (zone < count && func >= 0) { if (!pm_add_number(batch, key, func, FALSE)) return FALSE; }
        else if (!pm_add_value(batch, key, NULL)) return FALSE;
        if (!zone) sprintf_s(key, sizeof(key), "%d%dFile", button, click + 1);
        else sprintf_s(key, sizeof(key), "%d%dFile%d", button, click + 1, zone + 1);
        if (zone < count && IsMousePathFunc(func)) {
            if (!pm_add_text(batch, key, zone_file_map[button][click][zone])) return FALSE;
        } else if (!pm_add_value(batch, key, NULL)) return FALSE;
        if (!zone) sprintf_s(key, sizeof(key), "%d%dWorkDir", button, click + 1);
        else sprintf_s(key, sizeof(key), "%d%dWorkDir%d", button, click + 1, zone + 1);
        if (zone < count && IsMouseWorkDirFunc(func)) {
            if (!pm_add_text(batch, key, zone_workdir_map[button][click][zone])) return FALSE;
        } else if (!pm_add_value(batch, key, NULL)) return FALSE;
    }
    if (button == IDS_HOTKEY - IDS_LEFTBUTTON) {
        sprintf_s(key, sizeof(key), "%d%dHotkey", button, click + 1);
        if (pData[button].hotkey[click]) return pm_add_number(batch, key, pData[button].hotkey[click], FALSE);
        return pm_add_value(batch, key, NULL);
    }
    return TRUE;
}

/*------------------------------------------------
　更新
--------------------------------------------------*/
static BOOL pm_commit_batch(HWND hDlg, LPCWSTR path, const PM_BATCH* batch)
{
    WCHAR question[640], backup[MAX_PATH + 80], notice[MAX_PATH + 180];
    DWORD error;
    if (tc_patch_batchW(path, reg_section, batch->data, batch->used + 1)) return TRUE;
    error = GetLastError();
    if (error != ERROR_NO_UNICODE_TRANSLATION) { SetLastError(error); return FALSE; }
    swprintf_s(question, _countof(question), b_EnglishMenu
        ? L"This INI uses legacy Windows code page %u. Saving this text requires converting the entire INI to UTF-8. A verified copy of the original will be kept beside it. Convert and save\x3f"
        : L"\u3053\u306e INI \u306f Windows \u30b3\u30fc\u30c9\u30da\u30fc\u30b8 %u \u3067\u4fdd\u5b58\u3055\u308c\u3066\u3044\u307e\u3059\u3002\n\u3053\u306e\u6587\u5b57\u5217\u3092\u4fdd\u5b58\u3059\u308b\u306b\u306f INI \u5168\u4f53\u3092 UTF-8 \u306b\u5909\u63db\u3059\u308b\u5fc5\u8981\u304c\u3042\u308a\u307e\u3059\u3002\n\u5143\u30d5\u30a1\u30a4\u30eb\u306f\u5185\u5bb9\u3092\u7167\u5408\u3057\u3066\u540c\u3058\u30d5\u30a9\u30eb\u30c0\u306b\u4fdd\u7ba1\u3057\u307e\u3059\u3002\u5909\u63db\u3057\u3066\u4fdd\u5b58\u3057\u307e\u3059\u304b\uff1f",
        GetACP());
    if (MessageBoxW(hDlg, question, L"TClock", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        SetLastError(ERROR_CANCELLED);
        return FALSE;
    }
    if (!tc_patch_utf8W(path, reg_section, batch->data, batch->used + 1, backup, _countof(backup))) return FALSE;
    if (backup[0]) {
        swprintf_s(notice, _countof(notice), b_EnglishMenu
            ? L"The original INI was preserved at:\n%ls"
            : L"\u5143\u306e INI \u3092\u4fdd\u7ba1\u3057\u307e\u3057\u305f\uff1a\n%ls", backup);
        MessageBoxW(hDlg, notice, L"TClock", MB_OK | MB_ICONINFORMATION);
    }
    return TRUE;
}

static BOOL OnApply(HWND hDlg)
{
    PM_BATCH batch = {0};
    WCHAR path[MAX_PATH];
    char app[PM_TEXT_BYTES];
    int drop = CBGetCurSel(hDlg, IDC_DROPFILES);
    BOOL menu = IsDlgButtonChecked(hDlg, IDC_RCLICKMENU) == BST_CHECKED;
    BOOL ok = FALSE;
    DWORD error = ERROR_INVALID_DATA;
    int i, j;
    if (!pm_actions_ready || !pm_text_ready) { SetLastError(error); return FALSE; }
    if (!pm_get_text(hDlg, IDC_DROPFILESAPP, app, sizeof(app))) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE; }
    for (i = 0; i < 28; ++i) for (j = 0; j < 4; ++j) {
        int zone;
        for (zone = 0; zone < 3; ++zone) if (pm_edit_invalid[i][j][zone]) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER); return FALSE;
        }
    }
    SaveZoneCurrent(hDlg);
    if (dropfiles_dirty && !pm_add_number(&batch, "DropFiles", drop, TRUE)) goto cleanup;
    if (dropfiles_app_dirty && !pm_add_text(&batch, "DropFilesApp", app)) goto cleanup;
    if (rightclick_menu_dirty && !pm_add_number(&batch, "RightClickMenu", menu, TRUE)) goto cleanup;
    for (i = 0; i < 28; ++i) {
        for (j = 0; j < 4; ++j) {
            if (zone_dirty_map[i][j] && !pm_add_slot(&batch, i, j)) goto cleanup;
        }
    }
    /* Cross the existing serialized configuration-path boundary once; all I/O is wide. */
    if (batch.used) {
        if (tc_utf8_to_utf16(g_inifile, path, _countof(path)) <= 0) goto cleanup;
        if (!pm_commit_batch(hDlg, path, &batch)) { error = GetLastError(); goto cleanup; }
    }
    if (dropfiles_dirty) dropfiles_initial = drop;
    if (dropfiles_app_dirty) lstrcpyn(dropfiles_app_initial, app, (int)sizeof(dropfiles_app_initial));
    if (rightclick_menu_dirty) rightclick_menu_initial = menu;
    dropfiles_dirty = dropfiles_app_dirty = rightclick_menu_dirty = FALSE;
    ZeroMemory(zone_dirty_map, sizeof(zone_dirty_map));
    for (i = 0; i < 28; ++i) for (j = 0; j < 4; ++j) {
        int zone;
        pm_zone_initial[i][j] = zone_count_map[i][j];
        for (zone = 0; zone < 3; ++zone) pm_actions[i][j][zone].edited = FALSE;
    }
    mouse_note_setting("Mouse",NULL);
    ResetHotkey(g_hwndMain);
    ok = TRUE;
cleanup:
    if (batch.data) HeapFree(GetProcessHeap(), 0, batch.data);
    if (!ok) SetLastError(error);
    return ok;
}

/*------------------------------------------------

--------------------------------------------------*/
void OnDestroy(HWND hDlg)
{
	UNREFERENCED_PARAMETER(hDlg);
    pm_clear_drafts();
	if(pData) free(pData);
    pData = NULL;
    if (pm_commands) free(pm_commands);
    pm_commands = NULL; pm_command_count = 0;
}

/*------------------------------------------------
　「ファイルのドロップ」
--------------------------------------------------*/
void OnDropFilesChange(HWND hDlg)
{
	int i, n;
	n = CBGetCurSel(hDlg, IDC_DROPFILES);
	dropfiles_dirty = (n != dropfiles_initial);
	SetDlgItemTextUTF8Strict(hDlg, IDC_LABDROPFILESAPP,
		MyStringUTF8(n >= 3?IDS_LABFOLDER:IDS_LABPROGRAM));
	for(i = IDC_LABDROPFILESAPP; i <= IDC_DROPFILESAPPSANSHO; i++)
		ShowDlgItem(hDlg, i, (2 <= n && n <= 4));

	RefreshZoneControls(hDlg);
	SendPSChanged(hDlg);
}

/*------------------------------------------------
  "Button"
--------------------------------------------------*/
void OnMouseButton(HWND hDlg)
{
	int button, j;

	button = GetMouseButtonSel(hDlg);
	if (!pData || button < 0 || button >= 28) return;
	//if(n > 0) button = n + 1;

	for(j = 0; j < 4; j++)
	{
		if(pData[button].func[j] >= 0 || pm_actions[button][j][0].action.kind != IB_BUILTIN) break;
	}
	if(j == 4) j = 0;
	CheckRadioButton(hDlg, IDC_RADSINGLE, IDC_RADQUADRUPLE,
		IDC_RADSINGLE + j);

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
	{
		ShowDlgItem(hDlg, IDC_HOTKEY, TRUE);
	}
	else
	{
		ShowDlgItem(hDlg, IDC_HOTKEY, FALSE);
	}

	if (button == (IDS_WHEEL1  - IDS_LEFTBUTTON) || button == (IDS_WHEEL2  - IDS_LEFTBUTTON)||
		button == (IDS_CWHEEL1 - IDS_LEFTBUTTON) || button == (IDS_CWHEEL2 - IDS_LEFTBUTTON)||
		button == (IDS_SWHEEL1 - IDS_LEFTBUTTON) || button == (IDS_SWHEEL2 - IDS_LEFTBUTTON))
	{
		EnableDlgItem(hDlg, IDC_RADDOUBLE, FALSE);
		EnableDlgItem(hDlg, IDC_RADTRIPLE, FALSE);
		EnableDlgItem(hDlg, IDC_RADQUADRUPLE, FALSE);
	}
	else
	{
		EnableDlgItem(hDlg, IDC_RADDOUBLE, TRUE);
		EnableDlgItem(hDlg, IDC_RADTRIPLE, TRUE);
		EnableDlgItem(hDlg, IDC_RADQUADRUPLE, TRUE);
	}
	OnMouseClickTime(hDlg, IDC_RADSINGLE + j);
}

/*------------------------------------------------
  "Single" ... "Quadruple"
--------------------------------------------------*/
void OnMouseClickTime(HWND hDlg, int id)
{
	int button;
	int click, i, count, func;

	button = GetMouseButtonSel(hDlg);
	if (!pData || button < 0 || button >= 28) return;

	click = id - IDC_RADSINGLE;
	if (click < 0 || click >= 4) return;
	LoadZoneCurrent(hDlg);
	func = zone_func[0];
	CBSetCurSel(hDlg, IDC_MOUSEFUNC, -1);

	count = CBGetCount(hDlg, IDC_MOUSEFUNC);
	for(i = 0; i < count; i++)
	{
		if(func == CBGetItemData(hDlg, IDC_MOUSEFUNC, i))
		{
			CBSetCurSel(hDlg, IDC_MOUSEFUNC, i);
			break;
		}
	}

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
		SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_SETHOTKEY, pData[button].hotkey[click], 0);
	else
		SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_SETHOTKEY, 0, 0);
	RefreshZoneControls(hDlg);
	OnMouseFunc(hDlg, FALSE);
}

/*------------------------------------------------
  Mouse Functions combo box
--------------------------------------------------*/
void OnMouseFunc(HWND hDlg, BOOL selected)
{
	int button, j;
	int click, func;

	button = GetMouseButtonSel(hDlg);
	if (!pData || button < 0 || button >= 28) return;

	for(j = 0; j < 4; j++)
	{
		if(IsDlgButtonChecked(hDlg, IDC_RADSINGLE + j))
			break;
	}
	if(j == 4) return;
	click = j;

    if (!pm_choose_action(hDlg, IDC_MOUSEFUNC, 0, selected, &func)) return;
    pData[button].func[click] = func;
    zone_func[0] = func;
    SaveZoneCurrent(hDlg);

	if (button == (IDS_HOTKEY - IDS_LEFTBUTTON))
	{
		WORD hotkey = (WORD)SendDlgItemMessage(hDlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);
		if (pData[button].hotkey[click] != hotkey) zone_dirty_map[button][click] = TRUE;
		pData[button].hotkey[click] = hotkey;
	}

	ShowDlgItem(hDlg, IDC_LABMOUSEFILE, IsMousePathFunc(func));
	ShowDlgItem(hDlg, IDC_MOUSEFILE, IsMousePathFunc(func));
	ShowDlgItem(hDlg, IDC_MOUSEFILESANSHO, IsMousePathFunc(func));

	if(IsMousePathFunc(func))
	{
		pm_set_edit(hDlg, IDC_MOUSEFILE, 0, zone_file[0]);
	}
	RefreshZoneControls(hDlg);
}

/*------------------------------------------------
  Format/File - Mouse Function
--------------------------------------------------*/
void OnMouseFileChange(HWND hDlg)
{
	int button, j;
	int click, index, func;

	button = GetMouseButtonSel(hDlg);
	if (!pData || button < 0 || button >= 28) return;

	for(j = 0; j < 4; j++)
	{
		if(IsDlgButtonChecked(hDlg, IDC_RADSINGLE + j))
			break;
	}
	if(j == 4) return;
	click = j;

	index = CBGetCurSel(hDlg, IDC_MOUSEFUNC);
	if (index == CB_ERR) return;
	func = (int)(INT_PTR)CBGetItemData(hDlg, IDC_MOUSEFUNC, index);
	if (func == CB_ERR) return;

	if(IsMousePathFunc(func))
	{
        if (!pm_read_edit(hDlg, IDC_MOUSEFILE, 0, zone_file[0], sizeof(zone_file[0]))) return;
		lstrcpyn(pData[button].fname[click], zone_file[0], (int)sizeof(pData[button].fname[click]));
		SaveZoneCurrent(hDlg);
	}
}

void OnZoneCount(HWND hDlg)
{
	int n = CBGetCurSel(hDlg, IDC_ZONECOUNT);
	if (n == CB_ERR) return;
	zone_count = n + 1;
	if (zone_count < 1) zone_count = 1;
	if (zone_count > 3) zone_count = 3;
	SaveZoneCurrent(hDlg);
	RefreshZoneControls(hDlg);
}

void OnZoneFunc(HWND hDlg, WORD id)
{
	int zone_index;
	int func;

	if (id == IDC_ZONE2FUNC)
		zone_index = 1;
	else if (id == IDC_ZONE3FUNC)
		zone_index = 2;
	else
		return;
    if (!pm_choose_action(hDlg, id, zone_index, TRUE, &func)) return;
    zone_func[zone_index] = func;
	SaveZoneCurrent(hDlg);
	RefreshZoneControls(hDlg);
}

void OnZoneFileChange(HWND hDlg, WORD id)
{
	int zone_index;

	if (id == IDC_ZONE2FILE)
		zone_index = 1;
	else if (id == IDC_ZONE3FILE)
		zone_index = 2;
	else
		return;
    {
        int button, click;
        if (!GetZoneSel(hDlg, &button, &click)) return;
        if (!pm_read_edit(hDlg, id, zone_index, zone_file[zone_index], sizeof(zone_file[zone_index]))) return;
    }
	SaveZoneCurrent(hDlg);
}

/*------------------------------------------------
　「...」　ファイルの参照
--------------------------------------------------*/

void OnSansho(HWND hDlg, WORD id)
{
	int n;
	char filter[80], deffile[PM_TEXT_BYTES], fname[PM_TEXT_BYTES];
	HRESULT hrModern;

	if(id == IDC_DROPFILESAPPSANSHO)
	{
		n = CBGetCurSel(hDlg, IDC_DROPFILES);
		if(n >= 3)
		{
			char initdirUtf8[PM_TEXT_BYTES];
			wchar_t initdirW[MAX_PATH];
			const wchar_t* initdirArg = NULL;
			if (!pm_get_text(hDlg, id - 1, initdirUtf8, sizeof(initdirUtf8))) return;
			if (initdirUtf8[0] && tc_utf8_to_utf16(initdirUtf8, initdirW, (int)(sizeof(initdirW) / sizeof(initdirW[0]))) > 0) {
				initdirArg = initdirW;
			}
			hrModern = SelectPathUTF8Modern(hDlg, TRUE, initdirArg, NULL, 0, fname, (int)sizeof(fname));
			if (FAILED(hrModern)) return;
			SetDlgItemTextUTF8Strict(hDlg, id - 1, fname);
			PostMessage(hDlg, WM_NEXTDLGCTL, 1, FALSE);
			SendPSChanged(hDlg);
			return;
		}
	}

	filter[0] = 0;
	if(id == IDC_DROPFILESAPPSANSHO ||
		(id == IDC_MOUSEFILESANSHO && zone_func[0] == MOUSEFUNC_CUSTOMPROGRAM) ||
		(id == IDC_ZONE2FILESANSHO && zone_func[1] == MOUSEFUNC_CUSTOMPROGRAM) ||
		(id == IDC_ZONE3FILESANSHO && zone_func[2] == MOUSEFUNC_CUSTOMPROGRAM))
	{
		str0cat(filter, MyStringUTF8(IDS_PROGRAMFILE));
		str0cat(filter, "*.exe");
	}
	str0cat(filter, MyStringUTF8(IDS_ALLFILE));
	str0cat(filter, "*.*");

	if (!pm_get_text(hDlg, id - 1, deffile, sizeof(deffile))) return;

	if(!SelectMyFileUTF8(hDlg, filter, 0, deffile, fname, (int)sizeof(fname))) // propsheet.c
		return;
	SetDlgItemTextUTF8Strict(hDlg, id - 1, fname);
	PostMessage(hDlg, WM_NEXTDLGCTL, 1, FALSE);
	if (id == IDC_MOUSEFILESANSHO)
		OnMouseFileChange(hDlg);
	else if (id == IDC_ZONE2FILESANSHO || id == IDC_ZONE3FILESANSHO)
		OnZoneFileChange(hDlg, id - 1);
	SendPSChanged(hDlg);
}


/*------------------------------------------------
  set mouse functions to combo box
--------------------------------------------------*/
void InitMouseFuncList(HWND hDlg)
{
	InitMouseFuncCombo(hDlg, IDC_MOUSEFUNC);
}

void InitMouseFuncCombo(HWND hDlg, int ctrlId)
{
	int i, index, cnt;
	MOUSE_FUNC_INFO *pmfl;

	CBResetContent(hDlg, ctrlId);
	cnt = GetMouseFuncCount();
	pmfl = GetMouseFuncList();
	for (i = 0; i < cnt; i++)
	{
		if (!act_is_available(hDlg, pmfl[i].mousefunc)) continue;
		//リストの各項目を追加
		index = CBAddStringUTF8Compat(hDlg, ctrlId, MyStringUTF8(pmfl[i].idstring));
		CBSetItemData(hDlg, ctrlId, index, pmfl[i].mousefunc);
	}
	index = CBAddStringUTF8Compat(hDlg, ctrlId, b_EnglishMenu ? "Open Program" : "プログラムを開く");
	CBSetItemData(hDlg, ctrlId, index, MOUSEFUNC_CUSTOMPROGRAM);
    for (i = 0; i < pm_command_count; ++i) {
        index = (int)SendDlgItemMessageW(hDlg, ctrlId, CB_ADDSTRING, 0, (LPARAM)pm_commands[i].label);
        if (index >= 0) CBSetItemData(hDlg, ctrlId, index, -2 - i);
    }
	//リスト項目の表示数を指定
	AdjustDlgConboBoxDropDown(hDlg, ctrlId, 29);
}
