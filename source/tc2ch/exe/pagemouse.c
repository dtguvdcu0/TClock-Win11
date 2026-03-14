/*-------------------------------------------
  pagemouse.c
　　「マウス操作」プロパティページ
　　KAZUBON 1997-1998
---------------------------------------------*/

#include "tclock.h"

#include "..\\common\\text_codec.h"

static void OnInit(HWND hDlg);
static void OnApply(HWND hDlg);
static void OnDestroy(HWND hDlg);
static void OnDropFilesChange(HWND hDlg);
static void OnMouseButton(HWND hDlg);
static void OnMouseClickTime(HWND hDlg, int id);
static void OnMouseFunc(HWND hDlg);
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
static void WriteMouseEntry(int button, int click);
static BOOL HasZoneStringEntry(const char* entry);
static BOOL HasRawZoneKeys(int button, int click);
static BOOL HasCompatZoneKeys(int button, int click);
static BOOL HasLegacyLeftClickZoneKeys(void);

static void OnSansho(HWND hDlg, WORD id);
static void InitMouseFuncList(HWND hDlg);
static void InitMouseFuncCombo(HWND hDlg, int ctrlId);
static LONG GetTCaptureEnableForMousePage(void);
static LONG GetTCalendarEnableForMousePage(void);
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
static void NormalizeUtf8InPlaceNoWriteback(char *text, int count);

static char reg_section[] = "Mouse";

typedef struct {
	BOOL disable;
	int func[4];
	WORD hotkey[4];
	char format[4][256];
	char fname[4][256];
	int volume[4];
} CLICKDATA;
static CLICKDATA *pData = NULL;

static int zone_count_map[28][4];
static int zone_func_map[28][4][3];
static char zone_file_map[28][4][3][256];
static char zone_workdir_map[28][4][3][256];
static int zone_count = 1;
static int zone_func[3] = { MOUSEFUNC_NONE, MOUSEFUNC_NONE, MOUSEFUNC_NONE };
static char zone_file[3][256];
static char zone_workdir[3][256];
static BOOL zone_dirty_map[28][4];
static int dropfiles_initial = 0;
static char dropfiles_app_initial[256];
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

static LONG GetTCaptureEnableForMousePage(void)
{
	LONG v = GetMyRegLong("TCapture", "Enable", -1);
	if (v != -1) return (v != 0) ? 1 : 0;
	v = GetMyRegLong("ETC", "TCaptureEnable", 0);
	SetMyRegLong("TCapture", "Enable", (v != 0) ? 1 : 0);
	DelMyReg("ETC", "TCaptureEnable");
	return (v != 0) ? 1 : 0;
}

static LONG GetTCalendarEnableForMousePage(void)
{
	LONG v = GetMyRegLong("TCalendar", "Enable", -1);
	if (v == -1) {
		SetMyRegLong("TCalendar", "Enable", 0);
		return 0;
	}
	return (v != 0) ? 1 : 0;
}

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
	CBSetCurSel(hDlg, ctrlId, 0);
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
	GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	if (dst[0] == missing[0] && dst[1] == 0)
	{
		wsprintf(entry, "%d%dZone%dFile", button, click + 1, zone_number);
		GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0 && button == 0 && click == 0)
	{
		wsprintf(entry, "LeftClickZone%dFile", zone_number);
		GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0) dst[0] = 0;
	NormalizeUtf8InPlaceNoWriteback(dst, dst_count);
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
	GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	if (dst[0] == missing[0] && dst[1] == 0)
	{
		wsprintf(entry, "%d%dZone%dWorkDir", button, click + 1, zone_number);
		GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0 && button == 0 && click == 0)
	{
		wsprintf(entry, "LeftClickZone%dWorkDir", zone_number);
		GetMyRegStr(reg_section, entry, dst, dst_count, missing);
	}
	if (dst[0] == missing[0] && dst[1] == 0) dst[0] = 0;
	NormalizeUtf8InPlaceNoWriteback(dst, dst_count);
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
		SetDlgItemTextUTF8Strict(hDlg, IDC_MOUSEFILE, zone_file[0]);
	if (show_zone2_file)
		SetDlgItemTextUTF8Strict(hDlg, IDC_ZONE2FILE, zone_file[1]);
	if (show_zone3_file)
		SetDlgItemTextUTF8Strict(hDlg, IDC_ZONE3FILE, zone_file[2]);
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
		case WM_COMMAND:
		{
			WORD id, code;
			id = LOWORD(wParam); code = HIWORD(wParam);
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
					char s[256];
					GetDlgItemTextUTF8(hDlg, IDC_DROPFILESAPP, s, 256);
					dropfiles_app_dirty = (lstrcmp(s, dropfiles_app_initial) != 0);
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
					OnMouseFunc(hDlg);
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
				OnMouseFunc(hDlg);
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
		case WM_NOTIFY:
			switch(((NMHDR *)lParam)->code)
			{
				case PSN_APPLY: OnApply(hDlg); break;
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
static void NormalizeUtf8InPlaceNoWriteback(char* value, int valueBytes)
{
	WCHAR wbuf[MAX_PATH];
	char utf8[MAX_PATH];
	if (!value || valueBytes <= 0 || value[0] == '\0') return;
	if (tc_utf8_to_utf16(value, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return;
	if (tc_utf16_to_utf8(wbuf, utf8, (int)sizeof(utf8)) <= 0) return;
	lstrcpyn(value, utf8, valueBytes);
}

void OnInit(HWND hDlg)
{
	char s[256];
	char entry[20];
	int i, j, k, n;
	HFONT hfont;

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
	GetMyRegStr(reg_section, "DropFilesApp", s, 256, "");
	NormalizeUtf8InPlaceNoWriteback(s, (int)sizeof(s));
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
				GetMyRegStr(reg_section, entry, pData[i].fname[j], 256, "");
				NormalizeUtf8InPlaceNoWriteback(pData[i].fname[j], (int)sizeof(pData[i].fname[j]));
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
}

static void WriteMouseEntry(int button, int click)
{
	char entry[32];
	int k;
	BOOL has_zone_extras;
	int effective_zone_count;

	if (button < 0 || button >= 28 || click < 0 || click >= 4) return;

	effective_zone_count = zone_count_map[button][click];
	if (effective_zone_count < 1) effective_zone_count = 1;
	if (effective_zone_count > 3) effective_zone_count = 3;
	has_zone_extras = (effective_zone_count > 1) ? TRUE : FALSE;

	wsprintf(entry, "%d%dZoneCount", button, click + 1);
	if (has_zone_extras)
		SetMyRegLong(reg_section, entry, effective_zone_count);
	else
		DelMyReg(reg_section, entry);
	for (k = 0; k < 3; k++)
	{
		wsprintf(entry, "%d%dZone%dFunc", button, click + 1, k + 1);
		DelMyReg(reg_section, entry);
		wsprintf(entry, "%d%dZone%d", button, click + 1, k + 1);
		DelMyReg(reg_section, entry);
		wsprintf(entry, "%d%dZone%dFile", button, click + 1, k + 1);
		DelMyReg(reg_section, entry);
		wsprintf(entry, "%d%dZone%dWorkDir", button, click + 1, k + 1);
		DelMyReg(reg_section, entry);
	}

	wsprintf(entry, "%d%d", button, click + 1);
	if (zone_func_map[button][click][0] >= 0)
		SetMyRegLong(reg_section, entry, zone_func_map[button][click][0]);
	else
		DelMyReg(reg_section, entry);
	if (button == IDS_HOTKEY - IDS_LEFTBUTTON)
	{
		wsprintf(entry, "%d%dHotkey", button, click + 1);
		if (pData[button].hotkey[click])
			SetMyRegLong(reg_section, entry, pData[button].hotkey[click]);
		else
			DelMyReg(reg_section, entry);
	}
	if (IsMousePathFunc(zone_func_map[button][click][0]))
	{
		wsprintf(entry, "%d%dFile", button, click + 1);
		SetMyRegStr(reg_section, entry, zone_file_map[button][click][0]);
	}
	else
	{
		wsprintf(entry, "%d%dFile", button, click + 1);
		DelMyReg(reg_section, entry);
	}
	if (IsMouseWorkDirFunc(zone_func_map[button][click][0]))
	{
		wsprintf(entry, "%d%dWorkDir", button, click + 1);
		SetMyRegStr(reg_section, entry, zone_workdir_map[button][click][0]);
	}
	else
	{
		wsprintf(entry, "%d%dWorkDir", button, click + 1);
		DelMyReg(reg_section, entry);
	}

	for (k = 2; k <= 3; k++)
	{
		wsprintf(entry, "%d%dFunc%d", button, click + 1, k);
		if (has_zone_extras && effective_zone_count >= k && zone_func_map[button][click][k - 1] >= 0)
			SetMyRegLong(reg_section, entry, zone_func_map[button][click][k - 1]);
		else
			DelMyReg(reg_section, entry);
		wsprintf(entry, "%d%dFile%d", button, click + 1, k);
		if (has_zone_extras && effective_zone_count >= k && IsMousePathFunc(zone_func_map[button][click][k - 1]))
			SetMyRegStr(reg_section, entry, zone_file_map[button][click][k - 1]);
		else
			DelMyReg(reg_section, entry);
		wsprintf(entry, "%d%dWorkDir%d", button, click + 1, k);
		if (has_zone_extras && effective_zone_count >= k && IsMouseWorkDirFunc(zone_func_map[button][click][k - 1]))
			SetMyRegStr(reg_section, entry, zone_workdir_map[button][click][k - 1]);
		else
			DelMyReg(reg_section, entry);
	}

	if (button == 0 && click == 0)
	{
		DelMyReg(reg_section, "LeftClickZoneCount");
		for (k = 0; k < 3; k++)
		{
			wsprintf(entry, "LeftClickZone%dFunc", k + 1);
			DelMyReg(reg_section, entry);
			wsprintf(entry, "LeftClickZone%d", k + 1);
			DelMyReg(reg_section, entry);
			wsprintf(entry, "LeftClickZone%dFile", k + 1);
			DelMyReg(reg_section, entry);
			wsprintf(entry, "LeftClickZone%dWorkDir", k + 1);
			DelMyReg(reg_section, entry);
		}
	}

	zone_dirty_map[button][click] = FALSE;
}

/*------------------------------------------------
　更新
--------------------------------------------------*/
void OnApply(HWND hDlg)
{
	char s[256];
	int n;
	int i;
	int j;

	n = CBGetCurSel(hDlg, IDC_DROPFILES);
	if (dropfiles_dirty)
	{
		SetMyRegLong(reg_section, "DropFiles", n);
		dropfiles_initial = n;
		dropfiles_dirty = FALSE;
	}
	GetDlgItemTextUTF8(hDlg, IDC_DROPFILESAPP, s, 256);
	if (dropfiles_app_dirty)
	{
		SetMyRegStr(reg_section, "DropFilesApp", s);
		lstrcpyn(dropfiles_app_initial, s, (int)sizeof(dropfiles_app_initial));
		dropfiles_app_dirty = FALSE;
	}
	if (rightclick_menu_dirty)
	{
		rightclick_menu_initial =
			(IsDlgButtonChecked(hDlg, IDC_RCLICKMENU) == BST_CHECKED) ? TRUE : FALSE;
		SetMyRegLong(reg_section, "RightClickMenu", rightclick_menu_initial ? 1 : 0);
		rightclick_menu_dirty = FALSE;
	}

	SaveZoneCurrent(hDlg);

	for (i = 0; i < 28; i++)
	{
		for (j = 0; j < 4; j++)
		{
			if (!zone_dirty_map[i][j]) continue;
			WriteMouseEntry(i, j);
		}
	}
	ResetHotkey(g_hwndMain);
}

/*------------------------------------------------

--------------------------------------------------*/
void OnDestroy(HWND hDlg)
{
	UNREFERENCED_PARAMETER(hDlg);
	if(pData) free(pData);
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
		if(pData[button].func[j] >= 0) break;
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
	OnMouseFunc(hDlg);
}

/*------------------------------------------------
  Mouse Functions combo box
--------------------------------------------------*/
void OnMouseFunc(HWND hDlg)
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
		NormalizeUtf8InPlaceNoWriteback(zone_file[0], (int)sizeof(zone_file[0]));
		SetDlgItemTextUTF8Strict(hDlg, IDC_MOUSEFILE, zone_file[0]);
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
		GetDlgItemTextUTF8(hDlg, IDC_MOUSEFILE, zone_file[0], (int)sizeof(zone_file[0]));
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
	int index;
	int func;

	if (id == IDC_ZONE2FUNC)
		zone_index = 1;
	else if (id == IDC_ZONE3FUNC)
		zone_index = 2;
	else
		return;
	index = CBGetCurSel(hDlg, id);
	if (index == CB_ERR) return;
	func = (int)(INT_PTR)CBGetItemData(hDlg, id, index);
	if (func == CB_ERR) return;
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
	GetDlgItemTextUTF8(hDlg, id, zone_file[zone_index], (int)sizeof(zone_file[zone_index]));
	SaveZoneCurrent(hDlg);
}

/*------------------------------------------------
　「...」　ファイルの参照
--------------------------------------------------*/

void OnSansho(HWND hDlg, WORD id)
{
	int n;
	char filter[80], deffile[MAX_PATH], fname[MAX_PATH];
	HRESULT hrModern;

	if(id == IDC_DROPFILESAPPSANSHO)
	{
		n = CBGetCurSel(hDlg, IDC_DROPFILES);
		if(n >= 3)
		{
			char initdirUtf8[MAX_PATH];
			wchar_t initdirW[MAX_PATH];
			const wchar_t* initdirArg = NULL;
			GetDlgItemTextUTF8(hDlg, id - 1, initdirUtf8, MAX_PATH);
			if (initdirUtf8[0] && tc_utf8_to_utf16(initdirUtf8, initdirW, (int)(sizeof(initdirW) / sizeof(initdirW[0]))) > 0) {
				initdirArg = initdirW;
			}
			hrModern = SelectPathUTF8Modern(hDlg, TRUE, initdirArg, NULL, 0, fname, (int)sizeof(fname));
			if (FAILED(hrModern)) return;
			NormalizeUtf8InPlaceNoWriteback(fname, (int)sizeof(fname));
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

	GetDlgItemTextUTF8(hDlg, id - 1, deffile, MAX_PATH);

	if(!SelectMyFileUTF8(hDlg, filter, 0, deffile, fname, (int)sizeof(fname))) // propsheet.c
		return;

	NormalizeUtf8InPlaceNoWriteback(fname, (int)sizeof(fname));
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
	LONG tcapEnabled = GetTCaptureEnableForMousePage();
	LONG tcalEnabled = GetTCalendarEnableForMousePage();

	CBResetContent(hDlg, ctrlId);
	cnt = GetMouseFuncCount();
	pmfl = GetMouseFuncList();
	for (i = 0; i < cnt; i++)
	{
		if (pmfl[i].mousefunc == MOUSEFUNC_TCALENDAR_OPEN && !tcalEnabled) continue;
		if (pmfl[i].mousefunc == MOUSEFUNC_TCAPTURE_SETTINGS && !tcapEnabled) continue;
		//リストの各項目を追加
		index = CBAddStringUTF8Compat(hDlg, ctrlId, MyStringUTF8(pmfl[i].idstring));
		CBSetItemData(hDlg, ctrlId, index, pmfl[i].mousefunc);
	}
	index = CBAddStringUTF8Compat(hDlg, ctrlId, b_EnglishMenu ? "Open Program" : "プログラムを開く");
	CBSetItemData(hDlg, ctrlId, index, MOUSEFUNC_CUSTOMPROGRAM);
	//リスト項目の表示数を指定
	AdjustDlgConboBoxDropDown(hDlg, ctrlId, 29);
}
