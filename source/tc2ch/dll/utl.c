/*-------------------------------------------
  utl.c
  misc functions
  KAZUBON 1997-1999
---------------------------------------------*/

#include "tcdll.h"
#include "..\common\ini_io_utf8.h"
#include "..\common\text_codec.h"

extern HANDLE hmod;

BOOL g_bIniSetting = TRUE;
char g_inifile[MAX_PATH];
static const char* k_Utf8HexSuffix = "Utf8Hex";

static int tc_hex_digit(int v)
{
	if (v >= 0 && v <= 9) return '0' + v;
	if (v >= 10 && v <= 15) return 'A' + (v - 10);
	return '0';
}

static int tc_hex_value(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return -1;
}

static BOOL tc_is_utf8_hex_target_key(const char* section, const char* entry)
{
	if (!section || !entry || !*section || !*entry) return FALSE;
	if (lstrcmpi(section, "Color_Font") == 0 && lstrcmpi(entry, "Font") == 0) return TRUE;
	if (lstrcmpi(section, "Format") == 0 &&
		(lstrcmpi(entry, "Format") == 0 || lstrcmpi(entry, "CustomFormat") == 0)) return TRUE;
	if (lstrcmpi(section, "Tooltip") == 0 &&
		(lstrcmpi(entry, "TipFont") == 0 ||
		 lstrcmpi(entry, "TipTitle") == 0 ||
		 lstrcmpi(entry, "Tooltip") == 0 ||
		 lstrcmpi(entry, "Tooltip2") == 0 ||
		 lstrcmpi(entry, "Tooltip3") == 0)) return TRUE;
	return FALSE;
}

static BOOL tc_build_utf8_hex_entry_name(const char* entry, char* out, int outBytes)
{
	int entryLen;
	int suffixLen;
	if (!entry || !*entry || !out || outBytes <= 0) return FALSE;
	entryLen = lstrlen(entry);
	suffixLen = lstrlen(k_Utf8HexSuffix);
	if (entryLen + suffixLen + 1 > outBytes) return FALSE;
	lstrcpyn(out, entry, outBytes);
	lstrcat(out, k_Utf8HexSuffix);
	return TRUE;
}

static BOOL tc_encode_utf8_hex_from_ansi(const char* ansi, char* outHex, int outHexBytes)
{
	WCHAR wbuf[4096];
	char u8[8192];
	UINT cps[3];
	int cpCount = 0;
	int i;
	int n;
	int c;

	if (!ansi || !outHex || outHexBytes <= 0) return FALSE;
	cps[cpCount++] = GetACP();
	if (cps[0] != 932) cps[cpCount++] = 932;
	if (cps[0] != CP_UTF8 && (cpCount == 1 || cps[1] != CP_UTF8)) cps[cpCount++] = CP_UTF8;

	for (c = 0; c < cpCount; c++) {
		if (tc_ansi_to_utf16(cps[c], ansi, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) continue;
		if (tc_utf16_to_utf8(wbuf, u8, (int)sizeof(u8)) <= 0) continue;
		n = lstrlen(u8);
		if (n * 2 + 1 > outHexBytes) return FALSE;
		for (i = 0; i < n; i++) {
			unsigned char b = (unsigned char)u8[i];
			outHex[i * 2] = (char)tc_hex_digit((b >> 4) & 0x0F);
			outHex[i * 2 + 1] = (char)tc_hex_digit(b & 0x0F);
		}
		outHex[n * 2] = '\0';
		return TRUE;
	}
	return FALSE;
}

static BOOL tc_decode_utf8_hex_to_ansi(const char* hex, char* outAnsi, int outAnsiBytes)
{
	char u8[8192];
	WCHAR wbuf[4096];
	int n;
	int i;

	if (!hex || !outAnsi || outAnsiBytes <= 0) return FALSE;
	n = lstrlen(hex);
	if ((n & 1) != 0) return FALSE;
	if (n / 2 + 1 > (int)sizeof(u8)) return FALSE;

	for (i = 0; i < n / 2; i++) {
		int hi = tc_hex_value((unsigned char)hex[i * 2]);
		int lo = tc_hex_value((unsigned char)hex[i * 2 + 1]);
		if (hi < 0 || lo < 0) return FALSE;
		u8[i] = (char)((hi << 4) | lo);
	}
	u8[n / 2] = '\0';

	if (tc_utf8_to_utf16(u8, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return FALSE;
	if (tc_utf16_to_ansi(GetACP(), wbuf, outAnsi, outAnsiBytes) <= 0) return FALSE;
	return TRUE;
}

static void tc_strip_wrapping_quotes(char* s)
{
	int len;
	if (!s) return;
	len = lstrlen(s);
	if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
		MoveMemory(s, s + 1, (SIZE_T)(len - 2));
		s[len - 2] = '\0';
	}
}

BOOL flag_LogClear = FALSE;


BOOL b_AutoClearLogFile_back;
extern BOOL b_AutoClearLogFile;

/*-------------------------------------------
ログファイルのクリアを抑止する by TTTT
 ---------------------------------------------*/
void SuspendClearLog_Win10(void)
{
	b_AutoClearLogFile_back = b_AutoClearLogFile;
	b_AutoClearLogFile = FALSE;
}

/*-------------------------------------------
ログファイルのクリア指定を復帰する by TTTT
---------------------------------------------*/
void RecoverClearLog_Win10(void)
{
	b_AutoClearLogFile = b_AutoClearLogFile_back;
}



int _strncmp(const char* d, const char* s, size_t n)
{
	unsigned int i;
	for(i = 0; i < n; i++)
	{
		if(*s == 0 && *d == 0) break;
		if(*d != *s) return (*d - *s);
		d++; s++;
	}
	return 0;
}

/*-------------------------------------------
　パス名にファイル名をつける
---------------------------------------------*/
void add_title(char *path, char *title)
{
	char *p;

	p = path;

	if(*title && *(title + 1) == ':') ;
	else if(*title == '\\')
	{
		if(*p && *(p + 1) == ':') p += 2;
	}
	else
	{
		while(*p)
		{
			if((*p == '\\' || *p == '/') && *(p + 1) == 0)
			{
				break;
			}
			p = CharNext(p);
		}
		*p++ = '\\';
	}
	while(*title) *p++ = *title++;
	*p = 0;
}

/*-------------------------------------------
　パス名からファイル名をとりのぞく
---------------------------------------------*/
void del_title(char *path)
{
	char *p, *ep;

	p = ep = path;
	while(*p)
	{
		if(*p == '\\' || *p == '/')
		{
			if(p > path && *(p - 1) == ':') ep = p + 1;
			else ep = p;
		}
		p = CharNext(p);
	}
	*ep = 0;
}


/*-------------------------------------------
　パス名からファイル名を得る	copied from utl.c in tclock by TTTT
 ---------------------------------------------*/
void get_title(char* dst, const char *path)
{
	const char *p, *ep;

	p = ep = path;
	while (*p)
	{
		if (*p == '\\' || *p == '/')
		{
			if (p > path && *(p - 1) == ':') ep = p + 1;
			else ep = p;
		}
		p = CharNext(p);
	}

	if (*ep == '\\' || *ep == '/') ep++;

	while (*ep) *dst++ = *ep++;
	*dst = 0;
}





/*------------------------------------------------
	カンマで区切られた文字列を取り出す
--------------------------------------------------*/
void parse(char *dst, char *src, int n)
{
	char *dp;
	int i;

	for(i = 0; i < n; i++)
	{
		while(*src && *src != ',') src++;
		if(*src == ',') src++;
	}
	if(*src == 0)
	{
		*dst = 0; return;
	}

	while(*src == ' ') src++;

	dp = dst;
	while(*src && *src != ',') *dst++ = *src++;
	*dst = 0;

	while(dst != dp)
	{
		dst--;
		if(*dst == ' ') *dst = 0;
		else break;
	}
}

/*-------------------------------------------
  returns a resource string
---------------------------------------------*/
char* MyString(UINT id)
{
	static char buf[80];
	WCHAR wbuf[80];
	int n;

	buf[0] = 0;
	wbuf[0] = L'\0';
	n = LoadStringW(hmod, id, wbuf, 80);
	if(n > 0)
	{
		if(tc_utf16_to_ansi_compat(CP_ACP, wbuf, buf, 80) <= 0)
			buf[0] = 0;
	}

	return buf;
}

char mykey[] = "Software\\Kazubon\\TClock";

/*------------------------------------------------
　自分のレジストリから文字列を得る
--------------------------------------------------*/
int GetMyRegStr(char* section, char* entry, char* val, int cbData,
	char* defval)
{
	char key[80];
	int r = 0;
	BOOL isUtf8 = FALSE;
	BOOL needsHexBackfill = FALSE;
	char hexEntry[128];
	const char missingSentinel[] = "\x1D";

	if (strlen(g_inifile) == 0) return 0;
	if (!val || cbData <= 0) return 0;
	val[0] = '\0';

	key[0] = 0;


	if(section && *section)
	{
		strcat(key, section);
	}
	else
	{
		strcpy(key, "Main");
	}


	{
		if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
			if (tc_is_utf8_hex_target_key(section, entry)) {
				char mainbuf[4096];
				char hexbuf[4096];
				if (tc_ini_utf8_read_string(g_inifile, key, entry, missingSentinel, mainbuf, (int)sizeof(mainbuf)) > 0 ||
					mainbuf[0] == '\0') {
					if (lstrcmp(mainbuf, missingSentinel) != 0) {
						tc_strip_wrapping_quotes(mainbuf);
						lstrcpyn(val, mainbuf, cbData);
						r = lstrlen(val);
						if (isUtf8 && r > 0) {
							WCHAR wbuf[4096];
							char abuf[4096];
							if (tc_utf8_to_utf16(val, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) > 0 &&
								tc_utf16_to_ansi(GetACP(), wbuf, abuf, (int)sizeof(abuf)) > 0) {
								lstrcpyn(val, abuf, cbData);
								r = lstrlen(val);
							}
						}
						if (r == 0 && tc_build_utf8_hex_entry_name(entry, hexEntry, (int)sizeof(hexEntry))) {
							tc_ini_utf8_delete_key(g_inifile, key, hexEntry);
						}
						goto getmyregstr_done;
					}
				}
				if (tc_build_utf8_hex_entry_name(entry, hexEntry, (int)sizeof(hexEntry))) {
					if (tc_ini_utf8_read_string(g_inifile, key, hexEntry, "", hexbuf, (int)sizeof(hexbuf)) > 0) {
						if (tc_decode_utf8_hex_to_ansi(hexbuf, val, cbData)) {
							r = lstrlen(val);
							goto getmyregstr_done;
						}
					}
					else {
						needsHexBackfill = TRUE;
					}
				}
			}
			r = tc_ini_utf8_read_string(g_inifile, key, entry, defval ? defval : "", val, cbData);
			if (r == 0) {
				r = GetPrivateProfileString(key, entry, defval ? defval : "", val, cbData, g_inifile);
			}
		}
		else {
			r = GetPrivateProfileString(key, entry, defval ? defval : "", val, cbData, g_inifile);
		}
	}
	if (isUtf8 && r > 0) {
		WCHAR wbuf[4096];
		char abuf[4096];
		if (tc_utf8_to_utf16(val, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) > 0 &&
			tc_utf16_to_ansi(GetACP(), wbuf, abuf, (int)sizeof(abuf)) > 0) {
			lstrcpyn(val, abuf, cbData);
			r = lstrlen(val);
		}
	}
	if (isUtf8 && needsHexBackfill && r > 0) {
		char hexbuf[4096];
		if (tc_encode_utf8_hex_from_ansi(val, hexbuf, (int)sizeof(hexbuf))) {
			tc_ini_utf8_write_string(g_inifile, key, hexEntry, hexbuf);
		}
	}

getmyregstr_done:


	extern BOOL b_DebugLog_RegAccess;
	if (b_DebugLog_RegAccess)
	{
		char strLog[256];
		wsprintf(strLog, "***** GetMyRegStr, %s, %s called and returned string is %s", section, entry, val);
		writeDebugLog_Win10(strLog, 999);
	}

	return r;
}



/*------------------------------------------------
　自分のレジストリからLONG値を得る
--------------------------------------------------*/
LONG GetMyRegLong(char* section, char* entry, LONG defval)
{
	char key[80];
	LONG r = 0;


	if (strlen(g_inifile) == 0) return 0;


	key[0] = 0;


	if(section && *section)
	{
		strcat(key, section);
	}
	else
	{
		strcpy(key, "Main");
	}


	{
		char numbuf[64];
		char defbuf[64];
		BOOL isUtf8 = FALSE;
		wsprintf(defbuf, "%ld", defval);
		if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
			if (tc_ini_utf8_read_string(g_inifile, key, entry, defbuf, numbuf, (int)sizeof(numbuf)) > 0) {
				{
				char* pnum = numbuf;
				LONG sign = 1;
				LONG v = 0;
				if (*pnum == '-') { sign = -1; pnum++; }
				while (*pnum >= '0' && *pnum <= '9') {
					v = v * 10 + (LONG)(*pnum - '0');
					pnum++;
				}
				r = v * sign;
			}
			}
			else {
				r = GetPrivateProfileInt(key, entry, defval, g_inifile);
			}
		}
		else {
			r = GetPrivateProfileInt(key, entry, defval, g_inifile);
		}
	}




	extern BOOL b_DebugLog_RegAccess;
	if (b_DebugLog_RegAccess)
	{
		char strLog[256];
		wsprintf(strLog, "***** GetMyRegLong, %s, %s called and returned value is %d", section, entry, (int)r);
		writeDebugLog_Win10(strLog, 999);
	}

	return r;
}



/*-------------------------------------------
　レジストリに文字列を書き込む
---------------------------------------------*/
BOOL SetMyRegStr(char* section, char* entry, char* val)
{
	BOOL r;
	char key[80];
	BOOL isUtf8 = FALSE;

	if (strlen(g_inifile) == 0) return 0;

	key[0] = 0;


	if(section && *section)
	{
		lstrcpyn(key, section, (int)sizeof(key));
	}
	else
	{
		lstrcpyn(key, "Main", (int)sizeof(key));
	}

		char *chk_val;
		BOOL b_chkflg = FALSE;
		char saveval[1024];

		r = FALSE;
		if (!val) val = "";
		chk_val = val;
		while(*chk_val)
		{
			if (*chk_val == '\"' || *chk_val == '\'' || *chk_val == ' ') {
				b_chkflg = TRUE;
			}
			chk_val++;
		}

		if (b_chkflg)
		{
			int vlen = lstrlen(val);
			if (vlen > (int)sizeof(saveval) - 3) vlen = (int)sizeof(saveval) - 3;
			saveval[0] = '\"';
			if (vlen > 0) CopyMemory(saveval + 1, val, (SIZE_T)vlen);
			saveval[1 + vlen] = '\"';
			saveval[1 + vlen + 1] = '\0';
		}
		else
			lstrcpyn(saveval, val, (int)sizeof(saveval));

		if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
			if (tc_is_utf8_hex_target_key(section, entry)) {
				char hexbuf[4096];
				char hexEntry[128];
				BOOL rHex = FALSE;
				BOOL rLegacy = FALSE;
				if (tc_build_utf8_hex_entry_name(entry, hexEntry, (int)sizeof(hexEntry))) {
					if (val[0] == '\0') {
						rHex = tc_ini_utf8_write_string(g_inifile, key, hexEntry, "") ? TRUE : FALSE;
					}
					else if (tc_encode_utf8_hex_from_ansi(val, hexbuf, (int)sizeof(hexbuf))) {
						rHex = tc_ini_utf8_write_string(g_inifile, key, hexEntry, hexbuf) ? TRUE : FALSE;
					}
					else {
						/* Keep both keys in sync even when encoding fails. */
						rHex = tc_ini_utf8_write_string(g_inifile, key, hexEntry, "") ? TRUE : FALSE;
					}
				}
				rLegacy = tc_ini_utf8_write_string(g_inifile, key, entry, val) ? TRUE : FALSE;
				r = rHex && rLegacy;
			}
			else {
				r = tc_ini_utf8_write_string(g_inifile, key, entry, saveval) ? TRUE : FALSE;
			}
		}
		else if (WritePrivateProfileString(key, entry, saveval, g_inifile)) {
			r = TRUE;
		}

	extern BOOL b_DebugLog_RegAccess;
	if (b_DebugLog_RegAccess)
	{
		char strLog[256];
		wsprintf(strLog, "***** SetMyRegStr, %s, %s called to save string %s", section, entry, val);
		writeDebugLog_Win10(strLog, 999);
	}

	return r;
}

/*-------------------------------------------
　レジストリにDWORD値を書き込む
---------------------------------------------*/
BOOL SetMyRegLong(char* section, char* entry, DWORD val)
{
	BOOL r;
	char key[80];
	BOOL isUtf8 = FALSE;

	if (strlen(g_inifile) == 0) return 0;


	key[0] = 0;


	if(section && *section)
	{
		strcat(key, section);
	}
	else
	{
		strcpy(key, "Main");
	}

		char s[20];
		wsprintf(s, "%d", val);
		r = FALSE;
		if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
			if (tc_ini_utf8_write_string(g_inifile, key, entry, s)) {
				r = TRUE;
			}
		}
		else if (WritePrivateProfileString(key, entry, s, g_inifile)) {
			r = TRUE;
		}

	extern BOOL b_DebugLog_RegAccess;
	if (b_DebugLog_RegAccess)
	{
		char strLog[256];
		wsprintf(strLog, "***** SetMyRegLong, %s, %s called to save value %d", section, entry, (int)val);
		writeDebugLog_Win10(strLog, 999);
	}

	return r;
}


/*------------------------------------------------
　デバッグ用 with New API	Adde by TTTT
 --------------------------------------------------*/
void WriteDebugDLL_New(LPSTR s)
{

	HANDLE hFile;
	DWORD dwWriteSize;
	char fname[MAX_PATH];
	extern BOOL b_AutoClearLogFile;
	extern int LogLineCount;
	extern int AutoClearLogLines;
	extern char g_mydir_dll[];
	char strTemp[1024];

	strcpy(fname, g_mydir_dll);
	add_title(fname, "TCLOCK-WIN11-DEBUG.LOG");

	LogLineCount++;

	if (b_AutoClearLogFile && (LogLineCount > AutoClearLogLines))
	{
		hFile = CreateFile(
			fname, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			LogLineCount = 0;
		}
	}
	else
	{
		hFile = CreateFile(
			fname, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
			OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}

	if (hFile != INVALID_HANDLE_VALUE)
	{
		wsprintf(strTemp, "[tcdll:%4d] ", LogLineCount);
		WriteFile(hFile, strTemp, lstrlen(strTemp), &dwWriteSize, NULL);
		WriteFile(hFile, s, lstrlen(s), &dwWriteSize, NULL);
		WriteFile(hFile, "\x0d\x0a", lstrlen("\x0d\x0a"), &dwWriteSize, NULL);
		CloseHandle(hFile);
	}
}





// 与えられたファイル名が相対パスならば
// TClockのフォルダからの絶対パスに変換
PSTR CreateFullPathName(HINSTANCE hinst, PSTR fname)
{
	size_t len;
	size_t tlen;
	char szTClockPath[MAX_PATH];
	PSTR pstr;

	if (hinst == NULL) {
		return NULL;
	}
	if (fname == NULL) {
		return NULL;
	}
	if (*fname == '\0') {
		return NULL;
	}

	// \\NAME\C\path\path\filename.txt
	// C:\path\path\filename.txt
	// 以上の絶対パス以外を相対パスと判断して
	// その前にTClockのパスを基準ディレクトリとして付加
	len = strlen(fname);
	if (len >= 2) {
		if ((*fname == '\\') && (*(fname + 1) == '\\')) {
			//UNC name
			return NULL;
		} else if (*(fname + 1) == ':') {
			return NULL;
		}
	}

	// TClockの位置を基準パスとして指定文字列を相対パスとして追加
	if (GetModuleFileName(hinst, szTClockPath, MAX_PATH) == 0) {
		return NULL;
	}
	del_title(szTClockPath);
	tlen = strlen(szTClockPath);

	pstr = malloc(tlen + len + 2);
	if (pstr == NULL) {
		return NULL;
	}
	strcpy(pstr, szTClockPath);
	add_title(pstr, fname);

	return pstr;
}


/*-------------------------------------------
Normal Operation Log by TTTT
---------------------------------------------*/
void WriteNormalLog_DLL(const char* s)
{
	HANDLE hFile;
	DWORD dwWriteSize;
	char fname[MAX_PATH];
	extern char g_mydir_dll[];


	SYSTEMTIME systemtime;
	char strTemp[1024];
	GetLocalTime(&systemtime);

	{
		wsprintf(strTemp, "%d/%02d/%02d %02d:%02d:%02d.%03d %s",
			systemtime.wYear, systemtime.wMonth, systemtime.wDay,
			systemtime.wHour, systemtime.wMinute, systemtime.wSecond,
			systemtime.wMilliseconds, s);
	}


	//tickCount_LastLog = tickCount_LastLog_temp;

	strcpy(fname, g_mydir_dll);
	add_title(fname, "TClock-Win11.log");

	hFile = CreateFile(
		fname, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE) {

		WriteFile(hFile, strTemp, lstrlen(strTemp), &dwWriteSize, NULL);
		WriteFile(hFile, "\x0d\x0a", lstrlen("\x0d\x0a"), &dwWriteSize, NULL);

		CloseHandle(hFile);
	}
}


/*-------------------------------------------
　レジストリの値を削除
 ---------------------------------------------*/
int GetWindowTextUTF8_DLL(HWND hwnd, char* text, int textBytes)
{
	wchar_t wText[1024];
	if (!text || textBytes <= 0) return 0;
	text[0] = '\0';
	if (!hwnd) return 0;
	if (GetWindowTextW(hwnd, wText, (int)(sizeof(wText) / sizeof(wText[0]))) <= 0) return 0;
	if (tc_utf16_to_utf8(wText, text, textBytes) > 0) return lstrlen(text);
	if (tc_utf16_to_ansi_compat(CP_ACP, wText, text, textBytes) > 0) return lstrlen(text);
	text[0] = '\0';
	return 0;
}

int GetClassNameUTF8_DLL(HWND hwnd, char* text, int textBytes)
{
	wchar_t wText[1024];
	if (!text || textBytes <= 0) return 0;
	text[0] = '\0';
	if (!hwnd) return 0;
	if (GetClassNameW(hwnd, wText, (int)(sizeof(wText) / sizeof(wText[0]))) <= 0) return 0;
	if (tc_utf16_to_utf8(wText, text, textBytes) > 0) return lstrlen(text);
	if (tc_utf16_to_ansi_compat(CP_ACP, wText, text, textBytes) > 0) return lstrlen(text);
	text[0] = '\0';
	return 0;
}

HINSTANCE ShellExecuteUtf8Compat_DLL(HWND hwnd, const char* op, const char* file, const char* params, const char* dir, int showCmd)
{
	wchar_t wOp[64];
	wchar_t wFile[2048];
	wchar_t wParams[2048];
	wchar_t wDir[MAX_PATH];
	const wchar_t* pOp = NULL;
	const wchar_t* pFile = NULL;
	const wchar_t* pParams = NULL;
	const wchar_t* pDir = NULL;

	if (op && tc_ansi_to_utf16_compat(CP_UTF8, op, wOp, (int)(sizeof(wOp) / sizeof(wOp[0]))) > 0) pOp = wOp;
	else if (op && tc_ansi_to_utf16_compat(CP_ACP, op, wOp, (int)(sizeof(wOp) / sizeof(wOp[0]))) > 0) pOp = wOp;

	if (file && tc_ansi_to_utf16_compat(CP_UTF8, file, wFile, (int)(sizeof(wFile) / sizeof(wFile[0]))) > 0) pFile = wFile;
	else if (file && tc_ansi_to_utf16_compat(CP_ACP, file, wFile, (int)(sizeof(wFile) / sizeof(wFile[0]))) > 0) pFile = wFile;

	if (params && tc_ansi_to_utf16_compat(CP_UTF8, params, wParams, (int)(sizeof(wParams) / sizeof(wParams[0]))) > 0) pParams = wParams;
	else if (params && tc_ansi_to_utf16_compat(CP_ACP, params, wParams, (int)(sizeof(wParams) / sizeof(wParams[0]))) > 0) pParams = wParams;

	if (dir && tc_ansi_to_utf16_compat(CP_UTF8, dir, wDir, (int)(sizeof(wDir) / sizeof(wDir[0]))) > 0) pDir = wDir;
	else if (dir && tc_ansi_to_utf16_compat(CP_ACP, dir, wDir, (int)(sizeof(wDir) / sizeof(wDir[0]))) > 0) pDir = wDir;

	return ShellExecuteW(hwnd, pOp, pFile, pParams, pDir, showCmd);
}

BOOL DelMyReg_DLL(char* section, char* entry)
{
	BOOL r = FALSE;
	char key[80];
	BOOL isUtf8 = FALSE;

	if (strlen(g_inifile) == 0) return 0;

	key[0] = 0;

	if (section && *section)
	{
		strcat(key, section);
	}
	else
	{
		strcpy(key, "Main");
	}

	if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
		r = tc_ini_utf8_delete_key(g_inifile, key, entry) ? TRUE : FALSE;
	}
	else if (tc_ini_utf8_delete_key(g_inifile, key, entry)) {
		r = TRUE;
	}
	return r;
}

/*-------------------------------------------
　レジストリのキーを削除
 ---------------------------------------------*/
BOOL DelMyRegKey_DLL(char* section)
{
	BOOL r = FALSE;
	char key[80];
	BOOL isUtf8 = FALSE;

	if (strlen(g_inifile) == 0) return 0;

	key[0] = 0;

	if (section && *section)
	{
		strcat(key, section);
	}
	else
	{
		strcpy(key, "Main");
	}

	if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, NULL) && isUtf8) {
		r = tc_ini_utf8_delete_section(g_inifile, key) ? TRUE : FALSE;
	}
	else if (tc_ini_utf8_delete_section(g_inifile, key)) {
		r = TRUE;
	}
	return r;
}


void UpdateSettingFile(void)
{

}


void CleanSettingFile(void)
{
	//廃止したセクション
	DelMyRegKey_DLL("AppControl");
	DelMyRegKey_DLL("DataPlan");


	//廃止したエントリ
	DelMyReg_DLL("Status_DoNotEdit", "WindowsType");

	DelMyReg_DLL("ETC", "DisplayString_Single");
	DelMyReg_DLL("ETC", "DisplayString_Clone");
	DelMyReg_DLL("ETC", "DisplayString_Extend");
	DelMyReg_DLL("ETC", "ExtTXT_Length");

	DelMyReg_DLL("Graph", "CpuHigh");
	
	DelMyReg_DLL("Main", "WarnDelayedUsageRetrieval");

	DelMyReg_DLL("Tooltip", "TipDispTime");
	DelMyReg_DLL("Tooltip", "TipDisableCustomDraw");
	DelMyReg_DLL("Tooltip", "TipEnableDoubleBuffering");
	DelMyReg_DLL("Tooltip", "TipDispInterval");
		
	DelMyReg_DLL("Win11", "UseTClockNotify");
	DelMyReg_DLL("Win11", "ShowWin11NotifyNumber");

	DelMyReg_DLL(NULL, "EnhanceSubClkOnDarkTray");
}

