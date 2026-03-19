//---[s]--- For InternetTime 99/03/16@211 M.Takemura -----

/*-----------------------------------------------------
    format.c
    to make a string to display in the clock
    KAZUBON 1997-1998
-------------------------------------------------------*/

#include "tcdll.h"
#include "string.h"
#include <stdio.h>
#include <stdlib.h>
#include <winhttp.h>
#include "../common/text_codec.h"
#pragma comment(lib, "winhttp.lib")
#define MAX_PROCESSOR               64
#ifndef TC_FORMAT_ENABLE_ANSI_DATE_TIME_TOKENS
#define TC_FORMAT_ENABLE_ANSI_DATE_TIME_TOKENS 1
#endif

int codepage = 0;
int actdvl[36] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static char DayOfWeekShort[11], DayOfWeekLong[31];
static char DayOfWeekShortPrev[11], DayOfWeekLongPrev[31];
static char DayOfWeekShortNext[11], DayOfWeekLongNext[31];
static char MonthShort[11], MonthLong[31];
static char MonthShortPrev[11], MonthLongPrev[31];
static char MonthShortNext[11], MonthLongNext[31];
static char *DayOfWeekEng[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char *MonthEng[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static char AM[11], PM[11], SDate[5], STime[5];
static char EraStr[11];
static int AltYear;

static int ilang;
static BOOL tc_gip_align(const char* src, char* out, int outCch);
static void tc_gip_emit(char** dp, char** infop, const char* src);
static void tc_emit_net_mix_w(WCHAR** dp, int* remain);
static void tc_wappend_utf8_fixed_w(WCHAR** dp, int* remain, const char* src, int fixed);

extern BOOL bHour12, bHourZero;
extern BOOL b_DebugLog;

#define TC_CUSTOM_VAR_MAX 32
#define TC_CUSTOM_PATH_MAX 1024
#define TC_CUSTOM_VALUE_MAX 4096
#define TC_CUSTOM_FAIL_MAX 256
#define TC_CUSTOM_JSON_PATH_MAX 256
#define TC_CUSTOM_FILE_MAX_BYTES (64 * 1024)
#define TC_CUSTOM_MAX_CHARS_DEFAULT 20
#define TC_CUSTOM_REFRESH_DEFAULT 60
#define TC_CUSTOM_PRELOAD_DEFAULT 1
/* Enable only after CUSTOMn UTF-8 parity checks pass in migration validation. */
#define TC_CUSTOM_UTF8_CUTOVER_APPROVED 1
#ifndef TC_CUSTOM_USE_UTF8_VALUE
#define TC_CUSTOM_USE_UTF8_VALUE 1
#endif

typedef enum {
	TC_CUSTOM_WS_TRIM_EDGES = 0,
	TC_CUSTOM_WS_KEEP = 1
} TC_CUSTOM_WS_MODE;

typedef enum {
	TC_CUSTOM_MODE_LINE = 0,
	TC_CUSTOM_MODE_JSON = 1
} TC_CUSTOM_MODE;

typedef enum {
	TC_CUSTOM_JSON_TYPE_AUTO = 0,
	TC_CUSTOM_JSON_TYPE_STRING = 1,
	TC_CUSTOM_JSON_TYPE_NUMBER = 2,
	TC_CUSTOM_JSON_TYPE_BOOL = 3
} TC_CUSTOM_JSON_TYPE;

typedef enum {
	TC_CUSTOM_EXEC_TYPE_COMMAND = 0,
	TC_CUSTOM_EXEC_TYPE_SHELL = 1
} TC_CUSTOM_EXEC_TYPE;

typedef enum {
	TC_CUSTOM_EXEC_START_STARTUP = 0,
	TC_CUSTOM_EXEC_START_INTERVAL = 1,
	TC_CUSTOM_EXEC_START_BOTH = 2,
	TC_CUSTOM_EXEC_START_TIME = 3
} TC_CUSTOM_EXEC_START;

#define TC_GIP_VALUE_MAX 64
#define TC_GIP_URL_MAX 256
#define TC_GIP_FIELD_MAX 64
#define TC_GIP_PROVIDER_MAX 32

typedef struct {
	const char* key;
	const WCHAR* url;
	const char* field;
} TC_GIP_PROVIDER;

typedef struct {
	WCHAR url[TC_GIP_URL_MAX];
	char field[TC_GIP_FIELD_MAX];
} TC_GIP_REQUEST;

static const TC_GIP_PROVIDER g_gipProviders[] = {
	{ "ipify", L"https://api.ipify.org?format=json", "ip" },
	{ "seeip", L"https://api.seeip.org/jsonip?", "ip" },
	{ "ipinfo", L"https://ipinfo.io/json", "ip" }
};

typedef struct {
	char path[TC_CUSTOM_PATH_MAX];
	int refreshSec;
	int maxChars;
	int whitespaceMode;
	char failValue[TC_CUSTOM_FAIL_MAX];
	char value[TC_CUSTOM_VALUE_MAX];
	char valueUtf8[TC_CUSTOM_VALUE_MAX];
	int mode;
	char jsonDefault[TC_CUSTOM_FAIL_MAX];
	int jsonValueType;
	int jsonStringify;
	int jsonNullAsEmpty;
	char jsonValueExpr[TC_CUSTOM_VALUE_MAX];
	int execEnable;
	int execType;
	int execStart;
	int execIntervalSec;
	int execTimeHour;
	int execTimeMinute;
	char execCommand[TC_CUSTOM_PATH_MAX];
	char execCwd[TC_CUSTOM_PATH_MAX];
	DWORD nextExecTick;
	int lastExecDate;
	BOOL execStartupDone;
	DWORD nextRefreshTick;
	DWORD configHash;
	BOOL hasPath;
} TC_CUSTOM_VAR_ENTRY;

static TC_CUSTOM_VAR_ENTRY g_customVars[TC_CUSTOM_VAR_MAX];
static int g_customDefaultRefreshSec = TC_CUSTOM_REFRESH_DEFAULT;
static int g_customDefaultMaxChars = TC_CUSTOM_MAX_CHARS_DEFAULT;
static int g_customDefaultWhitespaceMode = TC_CUSTOM_WS_TRIM_EDGES;
static char g_customDefaultFailValue[TC_CUSTOM_FAIL_MAX] = "N/A";
static int g_customPreloadOnStartup = TC_CUSTOM_PRELOAD_DEFAULT;
static LONG g_gipEnabled = 0;
static int g_gipRefreshHours = 6;
static char g_gipProvider[TC_GIP_PROVIDER_MAX] = "ipify";
static WCHAR g_gipUrl[TC_GIP_URL_MAX] = L"";
static char g_gipJsonField[TC_GIP_FIELD_MAX] = "ip";
static WCHAR g_gipValue[TC_GIP_VALUE_MAX] = L"N/A";
static char g_gipValueUtf8[TC_GIP_VALUE_MAX] = "N/A";
static DWORD g_gipNextRefreshTick = 0;
static LONG g_gipFetchRunning = 0;
static LONG g_gipStartupPending = 1;
static LONG g_gipPersistPending = 0;
static CRITICAL_SECTION g_gipLock;
static LONG g_gipLockState = 0;
static LONG g_customSuppressPreloadOnce = 0;
static LONG g_customDeferIntervalBootstrapOnce = 0;
static BOOL g_customSettingsLoaded = FALSE;
extern HANDLE hmod;
extern PSTR CreateFullPathName(HINSTANCE hinst, PSTR fname);

static DWORD tc_custom_hash_text(const char* s)
{
	DWORD h = 5381;
	if (!s) return h;
	while (*s) {
		h = ((h << 5) + h) + (BYTE)(*s++);
	}
	return h;
}

static BOOL tc_custom_tick_expired(DWORD nowTick, DWORD targetTick)
{
	return ((LONG)(nowTick - targetTick) >= 0) ? TRUE : FALSE;
}

static int tc_custom_clamp_int(int v, int minv, int maxv)
{
	if (v < minv) return minv;
	if (v > maxv) return maxv;
	return v;
}

static int tc_custom_parse_whitespace_mode(const char* s, int defMode)
{
	if (!s || !s[0]) return defMode;
	if (_stricmp(s, "trim_edges") == 0) return TC_CUSTOM_WS_TRIM_EDGES;
	if (_stricmp(s, "keep") == 0) return TC_CUSTOM_WS_KEEP;
	return defMode;
}

static int tc_custom_parse_mode(const char* s)
{
	if (!s || !s[0]) return TC_CUSTOM_MODE_LINE;
	if (_stricmp(s, "line") == 0) return TC_CUSTOM_MODE_LINE;
	if (_stricmp(s, "json") == 0) return TC_CUSTOM_MODE_JSON;
	return TC_CUSTOM_MODE_LINE;
}

static int tc_custom_parse_exec_type(const char* s)
{
	if (!s || !s[0]) return TC_CUSTOM_EXEC_TYPE_COMMAND;
	if (_stricmp(s, "shell") == 0) return TC_CUSTOM_EXEC_TYPE_SHELL;
	return TC_CUSTOM_EXEC_TYPE_COMMAND;
}

static int tc_custom_parse_exec_start(const char* s)
{
	if (!s || !s[0]) return TC_CUSTOM_EXEC_START_INTERVAL;
	if (_stricmp(s, "startup") == 0) return TC_CUSTOM_EXEC_START_STARTUP;
	if (_stricmp(s, "interval") == 0) return TC_CUSTOM_EXEC_START_INTERVAL;
	if (_stricmp(s, "both") == 0) return TC_CUSTOM_EXEC_START_BOTH;
	if (_stricmp(s, "time") == 0) return TC_CUSTOM_EXEC_START_TIME;
	return TC_CUSTOM_EXEC_START_INTERVAL;
}

static void tc_custom_parse_hhmm(const char* s, int* outHour, int* outMinute)
{
	int h = -1;
	int m = -1;
	if (s && sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
		*outHour = h;
		*outMinute = m;
		return;
	}
	*outHour = -1;
	*outMinute = -1;
}

static int tc_custom_today_key(void)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	return (int)(st.wYear * 10000 + st.wMonth * 100 + st.wDay);
}

static int tc_custom_now_minutes(void)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	return (int)(st.wHour * 60 + st.wMinute);
}

static void tc_custom_try_init_inifile(void)
{
	char* full;
	WIN32_FIND_DATAW fd;
	HANDLE hfind;
	if (g_inifile[0]) return;
	if (!hmod) return;
	full = CreateFullPathName((HINSTANCE)hmod, "tclock-win11.ini");
	if (!full) return;
	hfind = tc_find_first_file_utf8_compat(full, &fd);
	if (hfind != INVALID_HANDLE_VALUE) {
		lstrcpyn(g_inifile, full, MAX_PATH);
		FindClose(hfind);
	}
	free(full);
}

static void tc_custom_build_key(int index1, const char* suffix, char* out, int outBytes)
{
	if (!out || outBytes <= 0) return;
	wsprintf(out, "Custom%d%s", index1, suffix ? suffix : "");
}

static void tc_custom_trim_edges_wide(wchar_t* w, int mode)
{
	int len;
	int start = 0;
	int end;
	if (!w || mode != TC_CUSTOM_WS_TRIM_EDGES) return;
	len = lstrlenW(w);
	end = len;
	while (start < end) {
		wchar_t ch = w[start];
		if (ch == L' ' || ch == L'\t' || ch == 0x3000) start++;
		else break;
	}
	while (end > start) {
		wchar_t ch = w[end - 1];
		if (ch == L' ' || ch == L'\t' || ch == 0x3000) end--;
		else break;
	}
	if (start > 0) {
		int i;
		for (i = 0; (start + i) < end; ++i) w[i] = w[start + i];
		w[i] = L'\0';
	}
	else {
		w[end] = L'\0';
	}
}

static BOOL tc_custom_is_abs_path(const char* path)
{
	if (!path || !path[0]) return FALSE;
	if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') return TRUE;
	if ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')) return TRUE;
	return FALSE;
}

static void tc_custom_resolve_path(const char* inPath, char* outPath, int outBytes)
{
	char base[MAX_PATH];
	char* p1;
	char* p2;
	if (!outPath || outBytes <= 0) return;
	outPath[0] = '\0';
	if (!inPath || !inPath[0]) return;
	if (tc_custom_is_abs_path(inPath) || !g_inifile[0]) {
		lstrcpyn(outPath, inPath, outBytes);
		return;
	}
	lstrcpyn(base, g_inifile, (int)sizeof(base));
	p1 = strrchr(base, '\\');
	p2 = strrchr(base, '/');
	if (p2 && (!p1 || p2 > p1)) p1 = p2;
	if (p1) *(p1 + 1) = '\0';
	else base[0] = '\0';
	if (!base[0]) lstrcpyn(outPath, inPath, outBytes);
	else wsprintf(outPath, "%s%s", base, inPath);
}

static BOOL tc_custom_decode_to_wide(const BYTE* raw, DWORD bytes, wchar_t* outWide, int outCch)
{
	int n;
	if (!outWide || outCch <= 0) return FALSE;
	outWide[0] = L'\0';
	if (!raw || bytes == 0) return FALSE;
	if (bytes >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
		n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)(raw + 3), (int)(bytes - 3), outWide, outCch - 1);
		if (n <= 0) return FALSE;
		outWide[n] = L'\0';
		return TRUE;
	}
	if (bytes >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
		DWORD usable = (bytes - 2) & ~1U;
		int wc = (int)(usable / 2);
		if (wc > outCch - 1) wc = outCch - 1;
		memcpy(outWide, raw + 2, (size_t)wc * sizeof(wchar_t));
		outWide[wc] = L'\0';
		return TRUE;
	}
	if (bytes >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
		DWORD usable = (bytes - 2) & ~1U;
		DWORD i;
		int wc = (int)(usable / 2);
		if (wc > outCch - 1) wc = outCch - 1;
		for (i = 0; i < (DWORD)wc; ++i) {
			WORD hi = raw[2 + i * 2];
			WORD lo = raw[2 + i * 2 + 1];
			outWide[i] = (wchar_t)((hi << 8) | lo);
		}
		outWide[wc] = L'\0';
		return TRUE;
	}
	n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)raw, (int)bytes, outWide, outCch - 1);
	if (n > 0) { outWide[n] = L'\0'; return TRUE; }
	/* Keep Shift-JIS (CP932) read compatibility as an explicit legacy ingress boundary. */
	n = MultiByteToWideChar(932, 0, (LPCSTR)raw, (int)bytes, outWide, outCch - 1);
	if (n > 0) { outWide[n] = L'\0'; return TRUE; }
	return FALSE;
}

static BOOL tc_custom_read_text_wide(const char* path, wchar_t* outWide, int outCch, BOOL firstLineOnly)
{
	HANDLE h;
	DWORD sizeLow;
	DWORD readBytes = 0;
	BYTE* raw = NULL;
	BOOL ok = FALSE;
	DWORD i;
	if (!path || !path[0] || !outWide || outCch <= 0) return FALSE;
	outWide[0] = L'\0';
	{
		wchar_t wPath[MAX_PATH];
		if (tc_utf8_to_utf16(path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) {
			if (tc_ansi_to_utf16_compat(0, path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) return FALSE;
		}
		h = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	if (h == INVALID_HANDLE_VALUE) return FALSE;
	sizeLow = GetFileSize(h, NULL);
	if (sizeLow == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) { CloseHandle(h); return FALSE; }
	if (sizeLow == 0 || sizeLow > TC_CUSTOM_FILE_MAX_BYTES) { CloseHandle(h); return FALSE; }
	raw = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sizeLow + 2);
	if (!raw) { CloseHandle(h); return FALSE; }
	if (!ReadFile(h, raw, sizeLow, &readBytes, NULL)) goto cleanup;
	if (readBytes == 0 || readBytes > TC_CUSTOM_FILE_MAX_BYTES) goto cleanup;
	if (!tc_custom_decode_to_wide(raw, readBytes, outWide, outCch)) goto cleanup;
	if (firstLineOnly) {
		for (i = 0; outWide[i]; ++i) {
			if (outWide[i] == L'\r' || outWide[i] == L'\n') { outWide[i] = L'\0'; break; }
		}
	}
	ok = TRUE;
cleanup:
	if (raw) HeapFree(GetProcessHeap(), 0, raw);
	CloseHandle(h);
	return ok;
}

static BOOL tc_custom_read_first_line_wide(const char* path, wchar_t* outWide, int outCch)
{
	return tc_custom_read_text_wide(path, outWide, outCch, TRUE);
}

static BOOL tc_custom_read_all_wide(const char* path, wchar_t* outWide, int outCch)
{
	return tc_custom_read_text_wide(path, outWide, outCch, FALSE);
}

typedef struct TC_CUSTOM_JSON_NODE_TAG TC_CUSTOM_JSON_NODE;

struct TC_CUSTOM_JSON_NODE_TAG {
	int type;
	char* key;
	char* text;
	int boolValue;
	TC_CUSTOM_JSON_NODE* child;
	TC_CUSTOM_JSON_NODE* next;
};

enum {
	TC_JSON_NODE_NULL = 0,
	TC_JSON_NODE_BOOL = 1,
	TC_JSON_NODE_NUMBER = 2,
	TC_JSON_NODE_STRING = 3,
	TC_JSON_NODE_OBJECT = 4,
	TC_JSON_NODE_ARRAY = 5
};

static TC_CUSTOM_JSON_NODE* tc_custom_json_new_node(int type)
{
	TC_CUSTOM_JSON_NODE* n = (TC_CUSTOM_JSON_NODE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TC_CUSTOM_JSON_NODE));
	if (n) n->type = type;
	return n;
}

static void tc_custom_json_free_node(TC_CUSTOM_JSON_NODE* n)
{
	TC_CUSTOM_JSON_NODE* c;
	TC_CUSTOM_JSON_NODE* nx;
	if (!n) return;
	if (n->key) HeapFree(GetProcessHeap(), 0, n->key);
	if (n->text) HeapFree(GetProcessHeap(), 0, n->text);
	c = n->child;
	while (c) { nx = c->next; tc_custom_json_free_node(c); c = nx; }
	HeapFree(GetProcessHeap(), 0, n);
}

static void tc_custom_json_skip_ws(const char** pp)
{
	const char* p = *pp;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	*pp = p;
}

static int tc_custom_json_is_hex(char c)
{
	if (c >= '0' && c <= '9') return 1;
	if (c >= 'a' && c <= 'f') return 1;
	if (c >= 'A' && c <= 'F') return 1;
	return 0;
}

static int tc_custom_json_hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

static int tc_custom_json_append_utf8(char* out, int* len, int cap, unsigned int cp)
{
	if (!out || !len || cap <= 0) return 0;
	if (cp <= 0x7F) {
		if ((*len) + 1 >= cap) return 0;
		out[(*len)++] = (char)cp;
		return 1;
	}
	if (cp <= 0x7FF) {
		if ((*len) + 2 >= cap) return 0;
		out[(*len)++] = (char)(0xC0 | ((cp >> 6) & 0x1F));
		out[(*len)++] = (char)(0x80 | (cp & 0x3F));
		return 1;
	}
	if (cp <= 0xFFFF) {
		if ((*len) + 3 >= cap) return 0;
		out[(*len)++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
		out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[(*len)++] = (char)(0x80 | (cp & 0x3F));
		return 1;
	}
	if ((*len) + 4 >= cap) return 0;
	out[(*len)++] = (char)(0xF0 | ((cp >> 18) & 0x07));
	out[(*len)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[(*len)++] = (char)(0x80 | (cp & 0x3F));
	return 1;
}

static char* tc_custom_json_parse_string_raw(const char** pp)
{
	const char* p = *pp;
	char* out;
	int len = 0;
	int cap = TC_CUSTOM_VALUE_MAX;
	if (*p != '"') return NULL;
	p++;
	out = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
	if (!out) return NULL;
	while (*p) {
		char c = *p++;
		if (c == '"') {
			out[len] = '\0';
			*pp = p;
			return out;
		}
		if (c == '\\') {
			char e = *p++;
			if (!e) break;
			switch (e) {
			case '"': c = '"'; break;
			case '\\': c = '\\'; break;
			case '/': c = '/'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'u':
				if (tc_custom_json_is_hex(p[0]) && tc_custom_json_is_hex(p[1]) && tc_custom_json_is_hex(p[2]) && tc_custom_json_is_hex(p[3])) {
					unsigned int cp = (unsigned int)((tc_custom_json_hex_val(p[0]) << 12) | (tc_custom_json_hex_val(p[1]) << 8) | (tc_custom_json_hex_val(p[2]) << 4) | tc_custom_json_hex_val(p[3]));
					p += 4;
					if (!tc_custom_json_append_utf8(out, &len, cap, cp)) goto fail;
					continue;
				}
				goto fail;
			default:
				goto fail;
			}
		}
		if (len + 1 >= cap) goto fail;
		out[len++] = c;
	}
fail:
	HeapFree(GetProcessHeap(), 0, out);
	return NULL;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_value(const char** pp);

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_number(const char** pp)
{
	const char* p = *pp;
	const char* s = p;
	char* ntext;
	int len;
	TC_CUSTOM_JSON_NODE* n;
	if (*p == '-') p++;
	if (*p == '0') { p++; }
	else { if (!isdigit((unsigned char)*p)) return NULL; while (isdigit((unsigned char)*p)) p++; }
	if (*p == '.') { p++; if (!isdigit((unsigned char)*p)) return NULL; while (isdigit((unsigned char)*p)) p++; }
	if (*p == 'e' || *p == 'E') { p++; if (*p == '+' || *p == '-') p++; if (!isdigit((unsigned char)*p)) return NULL; while (isdigit((unsigned char)*p)) p++; }
	len = (int)(p - s);
	if (len <= 0) return NULL;
	n = tc_custom_json_new_node(TC_JSON_NODE_NUMBER);
	if (!n) return NULL;
	ntext = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)len + 1);
	if (!ntext) { tc_custom_json_free_node(n); return NULL; }
	CopyMemory(ntext, s, len);
	ntext[len] = '\0';
	n->text = ntext;
	*pp = p;
	return n;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_array(const char** pp)
{
	const char* p = *pp;
	TC_CUSTOM_JSON_NODE* arr;
	TC_CUSTOM_JSON_NODE* tail = NULL;
	if (*p != '[') return NULL;
	p++;
	arr = tc_custom_json_new_node(TC_JSON_NODE_ARRAY);
	if (!arr) return NULL;
	tc_custom_json_skip_ws(&p);
	if (*p == ']') { p++; *pp = p; return arr; }
	for (;;) {
		TC_CUSTOM_JSON_NODE* v;
		tc_custom_json_skip_ws(&p);
		v = tc_custom_json_parse_value(&p);
		if (!v) { tc_custom_json_free_node(arr); return NULL; }
		if (!arr->child) arr->child = v; else tail->next = v;
		tail = v;
		tc_custom_json_skip_ws(&p);
		if (*p == ']') { p++; *pp = p; return arr; }
		if (*p != ',') { tc_custom_json_free_node(arr); return NULL; }
		p++;
	}
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_object(const char** pp)
{
	const char* p = *pp;
	TC_CUSTOM_JSON_NODE* obj;
	TC_CUSTOM_JSON_NODE* tail = NULL;
	if (*p != '{') return NULL;
	p++;
	obj = tc_custom_json_new_node(TC_JSON_NODE_OBJECT);
	if (!obj) return NULL;
	tc_custom_json_skip_ws(&p);
	if (*p == '}') { p++; *pp = p; return obj; }
	for (;;) {
		char* key;
		TC_CUSTOM_JSON_NODE* v;
		tc_custom_json_skip_ws(&p);
		key = tc_custom_json_parse_string_raw(&p);
		if (!key) { tc_custom_json_free_node(obj); return NULL; }
		tc_custom_json_skip_ws(&p);
		if (*p != ':') { HeapFree(GetProcessHeap(), 0, key); tc_custom_json_free_node(obj); return NULL; }
		p++;
		tc_custom_json_skip_ws(&p);
		v = tc_custom_json_parse_value(&p);
		if (!v) { HeapFree(GetProcessHeap(), 0, key); tc_custom_json_free_node(obj); return NULL; }
		v->key = key;
		if (!obj->child) obj->child = v; else tail->next = v;
		tail = v;
		tc_custom_json_skip_ws(&p);
		if (*p == '}') { p++; *pp = p; return obj; }
		if (*p != ',') { tc_custom_json_free_node(obj); return NULL; }
		p++;
	}
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_value(const char** pp)
{
	const char* p = *pp;
	TC_CUSTOM_JSON_NODE* n;
	tc_custom_json_skip_ws(&p);
	if (*p == '"') {
		n = tc_custom_json_new_node(TC_JSON_NODE_STRING);
		if (!n) return NULL;
		n->text = tc_custom_json_parse_string_raw(&p);
		if (!n->text) { tc_custom_json_free_node(n); return NULL; }
		*pp = p;
		return n;
	}
	if (*p == '{') {
		n = tc_custom_json_parse_object(&p);
		if (!n) return NULL;
		*pp = p;
		return n;
	}
	if (*p == '[') {
		n = tc_custom_json_parse_array(&p);
		if (!n) return NULL;
		*pp = p;
		return n;
	}
	if (strncmp(p, "true", 4) == 0) {
		n = tc_custom_json_new_node(TC_JSON_NODE_BOOL);
		if (!n) return NULL;
		n->boolValue = 1;
		p += 4;
		*pp = p;
		return n;
	}
	if (strncmp(p, "false", 5) == 0) {
		n = tc_custom_json_new_node(TC_JSON_NODE_BOOL);
		if (!n) return NULL;
		n->boolValue = 0;
		p += 5;
		*pp = p;
		return n;
	}
	if (strncmp(p, "null", 4) == 0) {
		n = tc_custom_json_new_node(TC_JSON_NODE_NULL);
		if (!n) return NULL;
		p += 4;
		*pp = p;
		return n;
	}
	if (*p == '-' || isdigit((unsigned char)*p)) {
		n = tc_custom_json_parse_number(&p);
		if (!n) return NULL;
		*pp = p;
		return n;
	}
	return NULL;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_parse_document(const char* text)
{
	const char* p = text;
	TC_CUSTOM_JSON_NODE* root;
	if (!text) return NULL;
	root = tc_custom_json_parse_value(&p);
	if (!root) return NULL;
	tc_custom_json_skip_ws(&p);
	if (*p != '\0') { tc_custom_json_free_node(root); return NULL; }
	return root;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_obj_find(TC_CUSTOM_JSON_NODE* obj, const char* key)
{
	TC_CUSTOM_JSON_NODE* c;
	if (!obj || obj->type != TC_JSON_NODE_OBJECT || !key) return NULL;
	c = obj->child;
	while (c) {
		if (c->key && strcmp(c->key, key) == 0) return c;
		c = c->next;
	}
	return NULL;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_arr_at(TC_CUSTOM_JSON_NODE* arr, int index)
{
	TC_CUSTOM_JSON_NODE* c;
	int i = 0;
	if (!arr || arr->type != TC_JSON_NODE_ARRAY || index < 0) return NULL;
	c = arr->child;
	while (c) {
		if (i == index) return c;
		i++;
		c = c->next;
	}
	return NULL;
}

static TC_CUSTOM_JSON_NODE* tc_custom_json_path_find(TC_CUSTOM_JSON_NODE* root, const char* path)
{
	const char* p = path;
	char seg[TC_CUSTOM_JSON_PATH_MAX];
	int segLen;
	TC_CUSTOM_JSON_NODE* cur = root;
	if (!root || !path || path[0] != '$') return NULL;
	p++;
	while (*p) {
		if (*p == '.') {
			p++;
			segLen = 0;
			while (*p && *p != '.' && *p != '[') {
				if (segLen + 1 >= (int)sizeof(seg)) return NULL;
				seg[segLen++] = *p++;
			}
			if (segLen <= 0) return NULL;
			seg[segLen] = '\0';
			cur = tc_custom_json_obj_find(cur, seg);
			if (!cur) return NULL;
			continue;
		}
		if (*p == '[') {
			int idx = 0;
			p++;
			if (!isdigit((unsigned char)*p)) return NULL;
			while (isdigit((unsigned char)*p)) { idx = (idx * 10) + (*p - '0'); p++; }
			if (*p != ']') return NULL;
			p++;
			cur = tc_custom_json_arr_at(cur, idx);
			if (!cur) return NULL;
			continue;
		}
		return NULL;
	}
	return cur;
}

static void tc_custom_json_append_escaped_string(char* out, int outCch, int* pos, const char* s)
{
	while (s && *s && *pos + 2 < outCch) {
		char c = *s++;
		if (c == '"' || c == '\\') { out[(*pos)++]='\\'; out[(*pos)++]=c; }
		else if (c == '\n') { out[(*pos)++]='\\'; out[(*pos)++]='n'; }
		else if (c == '\r') { out[(*pos)++]='\\'; out[(*pos)++]='r'; }
		else if (c == '\t') { out[(*pos)++]='\\'; out[(*pos)++]='t'; }
		else { out[(*pos)++] = c; }
	}
}

static void tc_custom_json_stringify_node(TC_CUSTOM_JSON_NODE* n, char* out, int outCch, int* pos)
{
	TC_CUSTOM_JSON_NODE* c;
	if (!n || !out || !pos || *pos + 1 >= outCch) return;
	switch (n->type) {
	case TC_JSON_NODE_NULL:
		lstrcpyn(out + *pos, "null", outCch - *pos);
		*pos += lstrlen(out + *pos);
		break;
	case TC_JSON_NODE_BOOL:
		lstrcpyn(out + *pos, n->boolValue ? "true" : "false", outCch - *pos);
		*pos += lstrlen(out + *pos);
		break;
	case TC_JSON_NODE_NUMBER:
		lstrcpyn(out + *pos, n->text ? n->text : "0", outCch - *pos);
		*pos += lstrlen(out + *pos);
		break;
	case TC_JSON_NODE_STRING:
		if (*pos + 1 < outCch) out[(*pos)++]='"';
		tc_custom_json_append_escaped_string(out, outCch, pos, n->text ? n->text : "");
		if (*pos + 1 < outCch) out[(*pos)++]='"';
		break;
	case TC_JSON_NODE_OBJECT:
		if (*pos + 1 < outCch) out[(*pos)++]='{';
		c = n->child;
		while (c && *pos + 1 < outCch) {
			if (c != n->child && *pos + 1 < outCch) out[(*pos)++]=',';
			if (*pos + 1 < outCch) out[(*pos)++]='"';
			tc_custom_json_append_escaped_string(out, outCch, pos, c->key ? c->key : "");
			if (*pos + 2 < outCch) { out[(*pos)++]='"'; out[(*pos)++]=':'; }
			tc_custom_json_stringify_node(c, out, outCch, pos);
			c = c->next;
		}
		if (*pos + 1 < outCch) out[(*pos)++]='}';
		break;
	case TC_JSON_NODE_ARRAY:
		if (*pos + 1 < outCch) out[(*pos)++]='[';
		c = n->child;
		while (c && *pos + 1 < outCch) {
			if (c != n->child && *pos + 1 < outCch) out[(*pos)++]=',';
			tc_custom_json_stringify_node(c, out, outCch, pos);
			c = c->next;
		}
		if (*pos + 1 < outCch) out[(*pos)++]=']';
		break;
	}
	if (*pos < outCch) out[*pos] = '\0';
	else out[outCch - 1] = '\0';
}

static BOOL tc_custom_utf16_to_utf8(const wchar_t* src, char* dst, int dstBytes)
{
	int r;
	if (!src || !dst || dstBytes <= 0) return FALSE;
	dst[0] = '\0';
	r = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstBytes, NULL, NULL);
	return (r > 0) ? TRUE : FALSE;
}

static BOOL tc_custom_utf8_to_utf16(const char* src, wchar_t* dst, int dstCch)
{
	int r;
	if (!src || !dst || dstCch <= 0) return FALSE;
	dst[0] = L'\0';
	r = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstCch);
	return (r > 0) ? TRUE : FALSE;
}

static BOOL tc_custom_text_to_utf16_compat(const char* src, wchar_t* dst, int dstCch)
/* ※ Shift-JISなど既存設定読み込みの互換境界（UTF-8失敗時のみ codepage 許容） */
{
	if (!src || !dst || dstCch <= 0) return FALSE;
	dst[0] = L'\0';
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, dst, dstCch) > 0) {
		return TRUE;
	}
	if (tc_ansi_to_utf16_compat(0, src, dst, dstCch) > 0) {
		return TRUE;
	}
	return FALSE;
}

static void tc_custom_wide_append(wchar_t* dst, int dstCch, int* pos, const wchar_t* src)
{
	int i = 0;
	if (!dst || !pos || !src || dstCch <= 1) return;
	while (src[i] && *pos < (dstCch - 1)) {
		dst[(*pos)++] = src[i++];
	}
	dst[*pos] = L'\0';
}

static void tc_custom_wide_append_char(wchar_t* dst, int dstCch, int* pos, wchar_t ch)
{
	if (!dst || !pos || *pos >= (dstCch - 1)) return;
	dst[(*pos)++] = ch;
	dst[*pos] = L'\0';
}

static void tc_custom_copy_trim_path(const char* src, int bytes, char* out, int outBytes)
{
	int s = 0;
	int e = bytes;
	int n;
	if (!src || !out || outBytes <= 0) return;
	while (s < e && (src[s] == ' ' || src[s] == '\t')) s++;
	while (e > s && (src[e - 1] == ' ' || src[e - 1] == '\t')) e--;
	n = e - s;
	if (n < 0) n = 0;
	if (n > outBytes - 1) n = outBytes - 1;
	if (n > 0) CopyMemory(out, src + s, n);
	out[n] = '\0';
}

static BOOL tc_custom_json_node_to_wide(
	TC_CUSTOM_VAR_ENTRY* e,
	TC_CUSTOM_JSON_NODE* target,
	int typeConstraint,
	const char* fallback,
	BOOL useFallback,
	wchar_t* outWide,
	int outCch)
{
	char outUtf8[TC_CUSTOM_VALUE_MAX];
	int pos = 0;
	if (!e || !target || !outWide || outCch <= 0) return FALSE;
	outWide[0] = L'\0';
	outUtf8[0] = '\0';

	if (target->type == TC_JSON_NODE_NULL) {
		if (e->jsonNullAsEmpty) {
			outUtf8[0] = '\0';
		} else if (useFallback) {
			return tc_custom_text_to_utf16_compat(fallback ? fallback : "", outWide, outCch);
		} else {
			return FALSE;
		}
	} else if (target->type == TC_JSON_NODE_OBJECT || target->type == TC_JSON_NODE_ARRAY) {
		if (!e->jsonStringify) {
			if (b_DebugLog) {
				char dbgJson[TC_CUSTOM_VALUE_MAX];
				int dbgPos = 0;
				tc_custom_json_stringify_node(target, dbgJson, (int)sizeof(dbgJson), &dbgPos);
				writeDebugLog_Win10("[format.c][CustomVars] Json object/array (stringify=0):", 999);
				writeDebugLog_Win10(dbgJson, 999);
			}
			if (useFallback) return tc_custom_text_to_utf16_compat(fallback ? fallback : "", outWide, outCch);
			return FALSE;
		}
		tc_custom_json_stringify_node(target, outUtf8, (int)sizeof(outUtf8), &pos);
	} else if (target->type == TC_JSON_NODE_STRING) {
		if (typeConstraint != TC_CUSTOM_JSON_TYPE_AUTO && typeConstraint != TC_CUSTOM_JSON_TYPE_STRING) {
			if (useFallback) return tc_custom_text_to_utf16_compat(fallback ? fallback : "", outWide, outCch);
			return FALSE;
		}
		lstrcpyn(outUtf8, target->text ? target->text : "", (int)sizeof(outUtf8));
	} else if (target->type == TC_JSON_NODE_NUMBER) {
		if (typeConstraint != TC_CUSTOM_JSON_TYPE_AUTO && typeConstraint != TC_CUSTOM_JSON_TYPE_NUMBER) {
			if (useFallback) return tc_custom_text_to_utf16_compat(fallback ? fallback : "", outWide, outCch);
			return FALSE;
		}
		lstrcpyn(outUtf8, target->text ? target->text : "0", (int)sizeof(outUtf8));
	} else if (target->type == TC_JSON_NODE_BOOL) {
		if (typeConstraint != TC_CUSTOM_JSON_TYPE_AUTO && typeConstraint != TC_CUSTOM_JSON_TYPE_BOOL) {
			if (useFallback) return tc_custom_text_to_utf16_compat(fallback ? fallback : "", outWide, outCch);
			return FALSE;
		}
		lstrcpyn(outUtf8, target->boolValue ? "true" : "false", (int)sizeof(outUtf8));
	}

	if (!tc_custom_utf8_to_utf16(outUtf8, outWide, outCch)) {
		return tc_custom_text_to_utf16_compat(outUtf8, outWide, outCch);
	}
	return TRUE;
}

static BOOL tc_custom_json_expand_template(TC_CUSTOM_VAR_ENTRY* e, TC_CUSTOM_JSON_NODE* root, wchar_t* outWide, int outCch)
{
	const char* p;
	int pos = 0;
	wchar_t segWide[TC_CUSTOM_VALUE_MAX];
	if (!e || !root || !outWide || outCch <= 0) return FALSE;
	if (!e->jsonValueExpr[0]) return FALSE;
	outWide[0] = L'\0';
	p = e->jsonValueExpr;

	while (*p && pos < (outCch - 1)) {
		if (p[0] == '{' && p[1] == '{') {
			tc_custom_wide_append_char(outWide, outCch, &pos, L'{');
			p += 2;
			continue;
		}
		if (p[0] == '}' && p[1] == '}') {
			tc_custom_wide_append_char(outWide, outCch, &pos, L'}');
			p += 2;
			continue;
		}
		if (p[0] == '{') {
			const char* q = p + 1;
			char path[TC_CUSTOM_JSON_PATH_MAX];
			TC_CUSTOM_JSON_NODE* target;
			wchar_t valueWide[TC_CUSTOM_VALUE_MAX];
			while (*q && *q != '}') q++;
			if (*q != '}') return FALSE;
			tc_custom_copy_trim_path(p + 1, (int)(q - (p + 1)), path, (int)sizeof(path));
			if (!path[0]) return FALSE;
			target = tc_custom_json_path_find(root, path);
			if (!target) return FALSE;
			if (!tc_custom_json_node_to_wide(e, target, e->jsonValueType, NULL, FALSE, valueWide, (int)(sizeof(valueWide) / sizeof(valueWide[0])))) return FALSE;
			tc_custom_wide_append(outWide, outCch, &pos, valueWide);
			p = q + 1;
			continue;
		}
		if (p[0] == '}') {
			return FALSE;
		}
		{
			const char* q = p;
			while (*q) {
				if ((q[0] == '{' && q[1] == '{') || (q[0] == '}' && q[1] == '}') || q[0] == '{' || q[0] == '}') break;
				q++;
			}
			if (q > p) {
				char lit[TC_CUSTOM_VALUE_MAX];
				int n = (int)(q - p);
				if (n > (int)sizeof(lit) - 1) n = (int)sizeof(lit) - 1;
				CopyMemory(lit, p, n);
				lit[n] = '\0';
				if (!tc_custom_text_to_utf16_compat(lit, segWide, (int)(sizeof(segWide) / sizeof(segWide[0])))) return FALSE;
				tc_custom_wide_append(outWide, outCch, &pos, segWide);
				p = q;
			}
		}
	}
	outWide[pos] = L'\0';
	return TRUE;
}


static BOOL tc_custom_json_extract_text(TC_CUSTOM_VAR_ENTRY* e, const wchar_t* wjson, wchar_t* outWide, int outCch)
{
	char* utf8;
	TC_CUSTOM_JSON_NODE* root;
	int need;
	BOOL ok;
	if (!e || !wjson || !outWide || outCch <= 0) return FALSE;
	need = WideCharToMultiByte(CP_UTF8, 0, wjson, -1, NULL, 0, NULL, NULL);
	if (need <= 0 || need > (TC_CUSTOM_FILE_MAX_BYTES * 4)) return FALSE;
	utf8 = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)need + 8);
	if (!utf8) return FALSE;
	if (!tc_custom_utf16_to_utf8(wjson, utf8, need + 8)) { HeapFree(GetProcessHeap(), 0, utf8); return FALSE; }
	root = tc_custom_json_parse_document(utf8);
	HeapFree(GetProcessHeap(), 0, utf8);
	if (!root) return FALSE;
	ok = tc_custom_json_expand_template(e, root, outWide, outCch);
	tc_custom_json_free_node(root);
	return ok;
}


static void tc_custom_set_fallback(TC_CUSTOM_VAR_ENTRY* e)
{
	if (!e) return;
	lstrcpyn(e->value, e->failValue, (int)sizeof(e->value));
	lstrcpyn(e->valueUtf8, e->failValue, (int)sizeof(e->valueUtf8));
}

static const char* tc_custom_get_emit_value(const TC_CUSTOM_VAR_ENTRY* e);
static const char* tc_custom_get_value(int index1);

static void tc_custom_run_script(TC_CUSTOM_VAR_ENTRY* e)
{
	char resolvedCwd[TC_CUSTOM_PATH_MAX + MAX_PATH];
	WCHAR wcmd[2048];
	WCHAR wline[2304];
	WCHAR wapp[MAX_PATH];
	WCHAR wCwd[MAX_PATH];
	LPCWSTR cwd = NULL;
	LPCWSTR app = NULL;
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	if (!e || !e->execCommand[0]) return;
	if (tc_utf8_to_utf16(e->execCommand, wcmd, (int)(sizeof(wcmd) / sizeof(wcmd[0]))) <= 0) {
		if (!tc_ansi_to_utf16_compat(0, e->execCommand, wcmd, (int)(sizeof(wcmd) / sizeof(wcmd[0])))) return;
	}
	tc_custom_resolve_path(e->execCwd, resolvedCwd, (int)sizeof(resolvedCwd));
	if (resolvedCwd[0]) {
		if (tc_utf8_to_utf16(resolvedCwd, wCwd, (int)(sizeof(wCwd) / sizeof(wCwd[0]))) <= 0) {
			if (!tc_ansi_to_utf16_compat(0, resolvedCwd, wCwd, (int)(sizeof(wCwd) / sizeof(wCwd[0])))) {
				wCwd[0] = L'\0';
			}
		}
		if (wCwd[0]) cwd = wCwd;
	}
	ZeroMemory(&si, sizeof(si));
	ZeroMemory(&pi, sizeof(pi));
	si.cb = sizeof(si);
	if (e->execType == TC_CUSTOM_EXEC_TYPE_SHELL) {
		lstrcpynW(wapp, L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", (int)(sizeof(wapp) / sizeof(wapp[0])));
		swprintf_s(wline, (size_t)(sizeof(wline) / sizeof(wline[0])), L"powershell.exe -NoProfile -Command %s", wcmd);
	} else {
		lstrcpynW(wapp, L"C:\\Windows\\System32\\cmd.exe", (int)(sizeof(wapp) / sizeof(wapp[0])));
		swprintf_s(wline, (size_t)(sizeof(wline) / sizeof(wline[0])), L"cmd.exe /C %s", wcmd);
	}
	app = wapp;
	if (!CreateProcessW(app, wline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi)) {
		if (b_DebugLog) writeDebugLog_Win10("[format.c][CustomVars] script launch failed", 999);
		return;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (b_DebugLog) writeDebugLog_Win10("[format.c][CustomVars] script launched", 999);
}

static void tc_custom_maybe_run_script(TC_CUSTOM_VAR_ENTRY* e, DWORD nowTick, BOOL forceRefresh)
{
	int runNow = 0;
	int todayKey;
	if (!e || !e->execEnable || !e->execCommand[0]) return;
	if ((e->execStart == TC_CUSTOM_EXEC_START_STARTUP || e->execStart == TC_CUSTOM_EXEC_START_BOTH) && forceRefresh && !e->execStartupDone) runNow = 1;
	if (!runNow && (e->execStart == TC_CUSTOM_EXEC_START_INTERVAL || e->execStart == TC_CUSTOM_EXEC_START_BOTH)) {
		if (e->nextExecTick == 0 || tc_custom_tick_expired(nowTick, e->nextExecTick)) runNow = 1;
	}
	if (!runNow && e->execStart == TC_CUSTOM_EXEC_START_TIME && e->execTimeHour >= 0 && e->execTimeMinute >= 0) {
		todayKey = tc_custom_today_key();
		if (e->lastExecDate != todayKey) {
			int nowMin = tc_custom_now_minutes();
			int targetMin = e->execTimeHour * 60 + e->execTimeMinute;
			if (nowMin >= targetMin) runNow = 1;
		}
	}
	if (!runNow) return;
	tc_custom_run_script(e);
	e->execStartupDone = TRUE;
	e->lastExecDate = tc_custom_today_key();
	if (e->execStart == TC_CUSTOM_EXEC_START_INTERVAL || e->execStart == TC_CUSTOM_EXEC_START_BOTH) {
		e->nextExecTick = nowTick + (DWORD)(tc_custom_clamp_int(e->execIntervalSec, 1, 86400) * 1000);
	}
}

static void tc_custom_refresh_one(int idx, DWORD nowTick, BOOL forceRefresh)
{
	TC_CUSTOM_VAR_ENTRY* e;
	wchar_t wbuf[TC_CUSTOM_VALUE_MAX];
	wchar_t wjson[TC_CUSTOM_FILE_MAX_BYTES + 8];
	char resolved[TC_CUSTOM_PATH_MAX + MAX_PATH];
	char ansi[TC_CUSTOM_VALUE_MAX];
	if (idx < 0 || idx >= TC_CUSTOM_VAR_MAX) return;
	e = &g_customVars[idx];
	tc_custom_maybe_run_script(e, nowTick, forceRefresh);
	if (!forceRefresh && !tc_custom_tick_expired(nowTick, e->nextRefreshTick)) return;
	if (!e->hasPath) {
		tc_custom_set_fallback(e);
		e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
		return;
	}
	tc_custom_resolve_path(e->path, resolved, (int)sizeof(resolved));
	if (!resolved[0]) {
		tc_custom_set_fallback(e);
		e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
		return;
	}
	if (e->mode == TC_CUSTOM_MODE_JSON) {
		if (!tc_custom_read_all_wide(resolved, wjson, (int)(sizeof(wjson) / sizeof(wjson[0])))) {
			tc_custom_set_fallback(e);
			e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
			return;
		}
		if (!tc_custom_json_extract_text(e, wjson, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])))) {
			tc_custom_set_fallback(e);
			e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
			return;
		}
	} else {
		if (!tc_custom_read_first_line_wide(resolved, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])))) {
			tc_custom_set_fallback(e);
			e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
			return;
		}
	}
	tc_custom_trim_edges_wide(wbuf, e->whitespaceMode);
	if (e->maxChars > 0 && lstrlenW(wbuf) > e->maxChars) wbuf[e->maxChars] = L'\0';
	if (wbuf[0] == L'\0') {
		tc_custom_set_fallback(e);
		e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
		return;
	}
	/* CUSTOMn is currently on a char contract; keep this ACP compatibility boundary for now. */
	if (tc_utf16_to_ansi_compat(0, wbuf, ansi, (int)sizeof(ansi)) <= 0) {
		tc_custom_set_fallback(e);
		e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
		return;
	}
	lstrcpyn(e->value, ansi, (int)sizeof(e->value));
	if (tc_utf16_to_utf8(wbuf, e->valueUtf8, (int)sizeof(e->valueUtf8)) <= 0) {
		/* Stage-B dual-write safety: keep UTF-8 companion aligned with legacy value. */
		lstrcpyn(e->valueUtf8, e->value, (int)sizeof(e->valueUtf8));
	}
	e->nextRefreshTick = nowTick + (DWORD)(e->refreshSec * 1000);
}

static int tc_custom_try_parse_token(const char* sp, int* outIndex)
{
	const char* p;
	int num = 0;
	if (!sp || !outIndex) return 0;
	if (_strnicmp(sp, "CUSTOM", 6) != 0) return 0;
	p = sp + 6;
	if (!isdigit((unsigned char)*p)) return 0;
	while (isdigit((unsigned char)*p)) {
		num = (num * 10) + (*p - '0');
		if (num > TC_CUSTOM_VAR_MAX) return 0;
		p++;
	}
	if (num < 1 || num > TC_CUSTOM_VAR_MAX) return 0;
	if (*p && (isalnum((unsigned char)*p) || *p == '_')) return 0;
	*outIndex = num;
	return (int)(p - sp);
}

static void tc_custom_append_text(char** dp, char** infop, const char* text)
{
	while (text && *text) {
		*(*dp)++ = *text++;
		*(*infop)++ = 0x01;
	}
}

static BOOL tc_custom_emit_if_token(char** dp, char** infop, char** sp)
{
	int idx = 0;
	int len;
	if (!dp || !infop || !sp || !*sp) return FALSE;
	len = tc_custom_try_parse_token(*sp, &idx);
	if (len <= 0) return FALSE;
	tc_custom_append_text(dp, infop, tc_custom_get_value(idx));
	*sp += len;
	return TRUE;
}

void CustomFormatVarsReadSettings(void)
{
	int i;
	tc_custom_try_init_inifile();
	char key[64];
	char tmp[TC_CUSTOM_FAIL_MAX];

	g_customDefaultRefreshSec = tc_custom_clamp_int((int)GetMyRegLong("CustomVars", "RefreshSec", TC_CUSTOM_REFRESH_DEFAULT), 1, 86400);
	g_customDefaultMaxChars = tc_custom_clamp_int((int)GetMyRegLong("CustomVars", "MaxChars", TC_CUSTOM_MAX_CHARS_DEFAULT), 1, 4096);
	lstrcpyn(g_customDefaultFailValue, "N/A", (int)sizeof(g_customDefaultFailValue));
	if (GetMyRegStr("CustomVars", "FailValue", g_customDefaultFailValue, (int)sizeof(g_customDefaultFailValue), "N/A") <= 0) {
		lstrcpyn(g_customDefaultFailValue, "N/A", (int)sizeof(g_customDefaultFailValue));
	}
	lstrcpyn(tmp, "trim_edges", (int)sizeof(tmp));
	if (GetMyRegStr("CustomVars", "Whitespace", tmp, (int)sizeof(tmp), "trim_edges") <= 0) {
		lstrcpyn(tmp, "trim_edges", (int)sizeof(tmp));
	}
	g_customDefaultWhitespaceMode = tc_custom_parse_whitespace_mode(tmp, TC_CUSTOM_WS_TRIM_EDGES);
	g_customPreloadOnStartup = GetMyRegLong("CustomVars", "PreloadOnStartup", TC_CUSTOM_PRELOAD_DEFAULT) ? 1 : 0;

	for (i = 0; i < TC_CUSTOM_VAR_MAX; ++i) {
		TC_CUSTOM_VAR_ENTRY* e = &g_customVars[i];
		DWORD h = 0;

		e->path[0] = '\0';
		e->refreshSec = g_customDefaultRefreshSec;
		e->maxChars = g_customDefaultMaxChars;
		lstrcpyn(e->failValue, g_customDefaultFailValue, (int)sizeof(e->failValue));
		e->whitespaceMode = g_customDefaultWhitespaceMode;
		e->mode = TC_CUSTOM_MODE_LINE;
		e->jsonDefault[0] = '\0';
		e->jsonValueType = TC_CUSTOM_JSON_TYPE_AUTO;
		e->jsonStringify = 0;
		e->jsonNullAsEmpty = 0;
		e->jsonValueExpr[0] = '\0';
		e->execEnable = 0;
		e->execType = TC_CUSTOM_EXEC_TYPE_COMMAND;
		e->execStart = TC_CUSTOM_EXEC_START_INTERVAL;
		e->execIntervalSec = 60;
		e->execTimeHour = -1;
		e->execTimeMinute = -1;
		e->execCommand[0] = '\0';
		lstrcpyn(e->execCwd, ".", (int)sizeof(e->execCwd));
		e->nextExecTick = 0;
		e->lastExecDate = 0;
		e->execStartupDone = FALSE;
		e->valueUtf8[0] = '\0';
		e->jsonValueType = TC_CUSTOM_JSON_TYPE_AUTO;

		if (g_inifile[0]) {
			tc_custom_build_key(i + 1, "Path", key, (int)sizeof(key));
			GetMyRegStr("CustomVars", key, e->path, (int)sizeof(e->path), "");
			tc_custom_build_key(i + 1, "RefreshSec", key, (int)sizeof(key));
			e->refreshSec = tc_custom_clamp_int((int)GetMyRegLong("CustomVars", key, g_customDefaultRefreshSec), 1, 86400);
			tc_custom_build_key(i + 1, "MaxChars", key, (int)sizeof(key));
			e->maxChars = tc_custom_clamp_int((int)GetMyRegLong("CustomVars", key, g_customDefaultMaxChars), 1, 4096);
			tc_custom_build_key(i + 1, "FailValue", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, e->failValue, (int)sizeof(e->failValue), g_customDefaultFailValue) <= 0) {
				lstrcpyn(e->failValue, g_customDefaultFailValue, (int)sizeof(e->failValue));
			}
			tc_custom_build_key(i + 1, "Whitespace", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, tmp, (int)sizeof(tmp), "") <= 0) {
				tmp[0] = '\0';
			}
			e->whitespaceMode = tc_custom_parse_whitespace_mode(tmp, g_customDefaultWhitespaceMode);
			tc_custom_build_key(i + 1, "Mode", key, (int)sizeof(key));
			tmp[0] = '\0';
			if (GetMyRegStr("CustomVars", key, tmp, (int)sizeof(tmp), "") <= 0) tmp[0] = '\0';
			e->mode = tc_custom_parse_mode(tmp);
			tc_custom_build_key(i + 1, "JsonDefault", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, e->jsonDefault, (int)sizeof(e->jsonDefault), "") <= 0) e->jsonDefault[0] = '\0';
			tc_custom_build_key(i + 1, "JsonStringify", key, (int)sizeof(key));
			e->jsonStringify = GetMyRegLong("CustomVars", key, 0) ? 1 : 0;
			tc_custom_build_key(i + 1, "JsonNullAsEmpty", key, (int)sizeof(key));
			e->jsonNullAsEmpty = GetMyRegLong("CustomVars", key, 0) ? 1 : 0;
			tc_custom_build_key(i + 1, "JsonValue", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, e->jsonValueExpr, (int)sizeof(e->jsonValueExpr), "") <= 0) e->jsonValueExpr[0] = '\0';
			tc_custom_build_key(i + 1, "ExecEnable", key, (int)sizeof(key));
			e->execEnable = GetMyRegLong("CustomVars", key, 0) ? 1 : 0;
			tc_custom_build_key(i + 1, "ExecType", key, (int)sizeof(key));
			tmp[0] = '\0';
			if (GetMyRegStr("CustomVars", key, tmp, (int)sizeof(tmp), "") <= 0) tmp[0] = '\0';
			e->execType = tc_custom_parse_exec_type(tmp);
			tc_custom_build_key(i + 1, "ExecStart", key, (int)sizeof(key));
			tmp[0] = '\0';
			if (GetMyRegStr("CustomVars", key, tmp, (int)sizeof(tmp), "") <= 0) tmp[0] = '\0';
			e->execStart = tc_custom_parse_exec_start(tmp);
			tc_custom_build_key(i + 1, "ExecIntervalSec", key, (int)sizeof(key));
			e->execIntervalSec = tc_custom_clamp_int((int)GetMyRegLong("CustomVars", key, 60), 1, 86400);
			tc_custom_build_key(i + 1, "ExecTime", key, (int)sizeof(key));
			tmp[0] = '\0';
			if (GetMyRegStr("CustomVars", key, tmp, (int)sizeof(tmp), "") <= 0) tmp[0] = '\0';
			tc_custom_parse_hhmm(tmp, &e->execTimeHour, &e->execTimeMinute);
			tc_custom_build_key(i + 1, "ExecCommand", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, e->execCommand, (int)sizeof(e->execCommand), "") <= 0) e->execCommand[0] = '\0';
			tc_custom_build_key(i + 1, "ExecCwd", key, (int)sizeof(key));
			if (GetMyRegStr("CustomVars", key, e->execCwd, (int)sizeof(e->execCwd), ".") <= 0) lstrcpyn(e->execCwd, ".", (int)sizeof(e->execCwd));
			if (e->mode == TC_CUSTOM_MODE_JSON && e->refreshSec < 5) e->refreshSec = 5;
		}

		e->hasPath = e->path[0] ? TRUE : FALSE;
		h ^= tc_custom_hash_text(e->path);
		h ^= tc_custom_hash_text(e->failValue);
		h ^= (DWORD)e->refreshSec;
		h ^= ((DWORD)e->maxChars << 8);
		h ^= ((DWORD)e->whitespaceMode << 16);
		h ^= ((DWORD)e->mode << 20);
		h ^= ((DWORD)e->jsonValueType << 22);
		h ^= ((DWORD)e->jsonStringify << 24);
		h ^= ((DWORD)e->jsonNullAsEmpty << 25);
		h ^= tc_custom_hash_text(e->jsonDefault);
		h ^= tc_custom_hash_text(e->jsonValueExpr);
		h ^= ((DWORD)e->execEnable << 26);
		h ^= ((DWORD)e->execType << 27);
		h ^= ((DWORD)e->execStart << 28);
		h ^= tc_custom_hash_text(e->execCommand);
		h ^= tc_custom_hash_text(e->execCwd);
		h ^= (DWORD)e->execIntervalSec;
		h ^= ((DWORD)((e->execTimeHour + 1) & 0x1F) << 5);
		h ^= ((DWORD)((e->execTimeMinute + 1) & 0x3F) << 10);
		if (h != e->configHash) {
			e->configHash = h;
			e->nextRefreshTick = 0;
			e->nextExecTick = 0;
			e->lastExecDate = 0;
			e->execStartupDone = FALSE;
			e->value[0] = '\0';
			e->valueUtf8[0] = '\0';
		}
		if (!e->hasPath) tc_custom_set_fallback(e);
	}
	g_customSettingsLoaded = TRUE;
}

void CustomFormatVarsPreloadIfEnabled(void)
{
	int i;
	DWORD nowTick;
	if (!g_customSettingsLoaded) CustomFormatVarsReadSettings();
	if (InterlockedExchange(&g_customSuppressPreloadOnce, 0) != 0) return;
	if (!g_customPreloadOnStartup) return;
	nowTick = GetTickCount();
	for (i = 0; i < TC_CUSTOM_VAR_MAX; ++i) tc_custom_refresh_one(i, nowTick, TRUE);
}

void CustomFormatVarsSuppressNextPreload(void)
{
	InterlockedExchange(&g_customSuppressPreloadOnce, 1);
	InterlockedExchange(&g_customDeferIntervalBootstrapOnce, 1);
}

static void tc_custom_defer_interval_bootstrap(DWORD nowTick)
{
	int i;
	for (i = 0; i < TC_CUSTOM_VAR_MAX; ++i) {
		TC_CUSTOM_VAR_ENTRY* e = &g_customVars[i];
		if (!e->execEnable || !e->execCommand[0]) continue;
		if (!(e->execStart == TC_CUSTOM_EXEC_START_INTERVAL || e->execStart == TC_CUSTOM_EXEC_START_BOTH)) continue;
		if (e->nextExecTick != 0) continue;
		e->nextExecTick = nowTick + (DWORD)(tc_custom_clamp_int(e->execIntervalSec, 1, 86400) * 1000);
	}
}

void CustomFormatVarsTick(void)
{
	int i;
	DWORD nowTick;
	if (!g_customSettingsLoaded) CustomFormatVarsReadSettings();
	nowTick = GetTickCount();
	if (InterlockedExchange(&g_customDeferIntervalBootstrapOnce, 0) != 0) tc_custom_defer_interval_bootstrap(nowTick);
	for (i = 0; i < TC_CUSTOM_VAR_MAX; ++i) tc_custom_refresh_one(i, nowTick, FALSE);
}

void CustomFormatVarsInvalidateSettings(void)
{
	g_customSettingsLoaded = FALSE;
}

static void tc_gip_lock(void)
{
	LONG state = g_gipLockState;
	if (state == 2) return;
	if (InterlockedCompareExchange(&g_gipLockState, 1, 0) == 0) {
		InitializeCriticalSection(&g_gipLock);
		InterlockedExchange(&g_gipLockState, 2);
		return;
	}
	while ((state = g_gipLockState) != 2) Sleep(0);
}

static const TC_GIP_PROVIDER* tc_gip_find(const char* key)
{
	int i;
	if (!key || !key[0]) return NULL;
	for (i = 0; i < (int)(sizeof(g_gipProviders) / sizeof(g_gipProviders[0])); ++i) {
		if (_stricmp(g_gipProviders[i].key, key) == 0) return &g_gipProviders[i];
	}
	return NULL;
}

static void tc_gip_set(const WCHAR* value)
{
	if (value && value[0]) {
		lstrcpynW(g_gipValue, value, TC_GIP_VALUE_MAX);
		if (tc_utf16_to_utf8(g_gipValue, g_gipValueUtf8, (int)sizeof(g_gipValueUtf8)) <= 0) {
			strcpy_s(g_gipValueUtf8, sizeof(g_gipValueUtf8), "N/A");
		}
	}
	else {
		lstrcpynW(g_gipValue, L"N/A", TC_GIP_VALUE_MAX);
		strcpy_s(g_gipValueUtf8, sizeof(g_gipValueUtf8), "N/A");
	}
}

static BOOL tc_gip_extract_string(TC_CUSTOM_JSON_NODE* node, char* out, int outCch)
{
	const char* p;
	int len;
	if (!node || !out || outCch <= 1) return FALSE;
	out[0] = '\0';
	if ((node->type != TC_JSON_NODE_STRING && node->type != TC_JSON_NODE_NUMBER) || !node->text || !node->text[0]) return FALSE;
	p = node->text;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	len = (int)strlen(p);
	while (len > 0) {
		char c = p[len - 1];
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
		len--;
	}
	if (len <= 0 || len >= outCch) return FALSE;
	memcpy(out, p, (size_t)len);
	out[len] = '\0';
	if (strchr(out, ' ') || strchr(out, '\t') || strchr(out, '\r') || strchr(out, '\n')) return FALSE;
	return TRUE;
}

static BOOL tc_gip_get(const WCHAR* url, char* out, int outCch)
{
	URL_COMPONENTSW parts;
	WCHAR host[128];
	WCHAR path[512];
	WCHAR extra[512];
	WCHAR target[1024];
	HINTERNET session = NULL;
	HINTERNET connect = NULL;
	HINTERNET request = NULL;
	DWORD status = 0;
	DWORD statusLen = sizeof(status);
	BOOL ok = FALSE;
	DWORD total = 0;
	DWORD read = 0;

	if (!url || !url[0] || !out || outCch <= 1) return FALSE;
	out[0] = '\0';
	ZeroMemory(&parts, sizeof(parts));
	ZeroMemory(host, sizeof(host));
	ZeroMemory(path, sizeof(path));
	ZeroMemory(extra, sizeof(extra));
	ZeroMemory(target, sizeof(target));
	parts.dwStructSize = sizeof(parts);
	parts.lpszHostName = host;
	parts.dwHostNameLength = (DWORD)_countof(host);
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = (DWORD)_countof(path);
	parts.lpszExtraInfo = extra;
	parts.dwExtraInfoLength = (DWORD)_countof(extra);
	if (!WinHttpCrackUrl(url, 0, 0, &parts)) { goto cleanup; }

	lstrcpynW(target, path[0] ? path : L"/", (int)_countof(target));
	if (extra[0]) {
		int cur = lstrlenW(target);
		lstrcpynW(target + cur, extra, (int)_countof(target) - cur);
	}

	session = WinHttpOpen(L"TClock-Win11 GIP/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) { goto cleanup; }
	WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
	connect = WinHttpConnect(session, host, parts.nPort, 0);
	if (!connect) { goto cleanup; }
	request = WinHttpOpenRequest(connect, L"GET", target, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
	if (!request) { goto cleanup; }
	if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) { goto cleanup; }
	if (!WinHttpReceiveResponse(request, NULL)) { goto cleanup; }
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusLen, WINHTTP_NO_HEADER_INDEX)) { goto cleanup; }
	if (status != 200) { goto cleanup; }

	for (;;) {
		char chunk[1024];
		read = 0;
		if (!WinHttpReadData(request, chunk, sizeof(chunk), &read)) { goto cleanup; }
		if (read == 0) break;
		if (total + read >= (DWORD)outCch) { goto cleanup; }
		memcpy(out + total, chunk, read);
		total += read;
	}
	out[total] = '\0';
	ok = TRUE;

cleanup:
	if (request) WinHttpCloseHandle(request);
	if (connect) WinHttpCloseHandle(connect);
	if (session) WinHttpCloseHandle(session);
	return ok;
}

static DWORD WINAPI tc_gip_thread(LPVOID param)
{
	TC_GIP_REQUEST* req = (TC_GIP_REQUEST*)param;
	TC_CUSTOM_VAR_ENTRY tempEntry;
	char json[8192];
	char ip[TC_GIP_VALUE_MAX];
	wchar_t jsonWide[8192];
	WCHAR valueW[TC_GIP_VALUE_MAX];
	BOOL ok = FALSE;
	DWORD nowTick = GetTickCount();
	int hours;

	json[0] = '\0';
	ip[0] = '\0';
	jsonWide[0] = L'\0';
	valueW[0] = L'\0';

	if (req && tc_gip_get(req->url, json, (int)sizeof(json))) {
		ZeroMemory(&tempEntry, sizeof(tempEntry));
		tempEntry.jsonValueType = TC_CUSTOM_JSON_TYPE_STRING;
		sprintf_s(tempEntry.jsonValueExpr, sizeof(tempEntry.jsonValueExpr), "{$.%s}", req->field);
		if (tc_utf8_to_utf16(json, jsonWide, (int)_countof(jsonWide)) > 0 &&
			tc_custom_json_extract_text(&tempEntry, jsonWide, valueW, (int)_countof(valueW)) &&
			tc_utf16_to_utf8(valueW, ip, (int)sizeof(ip)) > 0) {
			ok = TRUE;
		}
	}

	tc_gip_lock();
	EnterCriticalSection(&g_gipLock);
	if (ok) tc_gip_set(valueW);
	else tc_gip_set(NULL);
	hours = g_gipRefreshHours;
	if (hours < 1) hours = 1;
	if (hours > 168) hours = 168;
	g_gipNextRefreshTick = nowTick + (DWORD)(hours * 60 * 60 * 1000);
	g_gipStartupPending = 0;
	g_gipPersistPending = 1;
	LeaveCriticalSection(&g_gipLock);

	if (req) HeapFree(GetProcessHeap(), 0, req);
	InterlockedExchange(&g_gipFetchRunning, 0);
	return 0;
}

void GipRead(void)
{
	char provider[TC_GIP_PROVIDER_MAX];
	char urlUtf8[TC_GIP_URL_MAX];
	char field[TC_GIP_FIELD_MAX];
	WCHAR urlWide[TC_GIP_URL_MAX];
	const TC_GIP_PROVIDER* preset;
	BOOL enabled;
	int hours;

	urlWide[0] = L'\0';
	GetMyRegStr("ETC", "GipProvider", provider, (int)sizeof(provider), "ipify");
	if (!provider[0]) strncpy_s(provider, sizeof(provider), "ipify", _TRUNCATE);
	enabled = GetMyRegLong("ETC", "GipEnabled", 0) ? TRUE : FALSE;
	hours = (int)GetMyRegLong("ETC", "GipRefreshHours", 6);
	if (hours < 1) hours = 1;
	if (hours > 168) hours = 168;
	preset = tc_gip_find(provider);
	if (preset) {
		strncpy_s(field, sizeof(field), preset->field, _TRUNCATE);
		lstrcpynW(urlWide, preset->url, TC_GIP_URL_MAX);
	}
	else {
		GetMyRegStr("ETC", "GipUrl", urlUtf8, (int)sizeof(urlUtf8), "");
		GetMyRegStr("ETC", "GipJsonField", field, (int)sizeof(field), "ip");
		if (!field[0]) strncpy_s(field, sizeof(field), "ip", _TRUNCATE);
		if (tc_utf8_to_utf16(urlUtf8, urlWide, TC_GIP_URL_MAX) <= 0) urlWide[0] = L'\0';
	}

	tc_gip_lock();
	EnterCriticalSection(&g_gipLock);
	g_gipEnabled = enabled ? 1 : 0;
	g_gipRefreshHours = hours;
	strncpy_s(g_gipProvider, sizeof(g_gipProvider), provider, _TRUNCATE);
	lstrcpynW(g_gipUrl, urlWide, TC_GIP_URL_MAX);
	strncpy_s(g_gipJsonField, sizeof(g_gipJsonField), field, _TRUNCATE);
	g_gipNextRefreshTick = 0;
	g_gipStartupPending = enabled ? 1 : 0;
	if (!enabled) {
		tc_gip_set(NULL);
	}
	LeaveCriticalSection(&g_gipLock);

	if (!preset) {
		SetMyRegStr("ETC", "GipJsonField", field);
	}
}

void GipTick(void)
{
	TC_GIP_REQUEST* req;
	HANDLE thread;
	BOOL enabled;
	BOOL startupPending;
	BOOL persistPending;
	DWORD nextTick;
	DWORD nowTick;
	char persisted[TC_GIP_VALUE_MAX];

	tc_gip_lock();
	EnterCriticalSection(&g_gipLock);
	enabled = g_gipEnabled ? TRUE : FALSE;
	startupPending = g_gipStartupPending ? TRUE : FALSE;
	nextTick = g_gipNextRefreshTick;
	persistPending = g_gipPersistPending ? TRUE : FALSE;
	if (persistPending) {
		strcpy_s(persisted, sizeof(persisted), g_gipValueUtf8);
		g_gipPersistPending = 0;
	}
	LeaveCriticalSection(&g_gipLock);
	if (persistPending) SetMyRegStr("ETC", "GipLastValue", persisted);
	if (!enabled) return;
	if (InterlockedCompareExchange(&g_gipFetchRunning, 1, 0) != 0) return;

	nowTick = GetTickCount();
	if (!startupPending && nextTick != 0 && !tc_custom_tick_expired(nowTick, nextTick)) {
		InterlockedExchange(&g_gipFetchRunning, 0);
		return;
	}

	req = (TC_GIP_REQUEST*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TC_GIP_REQUEST));
	if (!req) {
		InterlockedExchange(&g_gipFetchRunning, 0);
		return;
	}

	EnterCriticalSection(&g_gipLock);
	lstrcpynW(req->url, g_gipUrl, (int)_countof(req->url));
	strncpy_s(req->field, sizeof(req->field), g_gipJsonField, _TRUNCATE);
	LeaveCriticalSection(&g_gipLock);

	if (!req->url[0] || !req->field[0]) {
		HeapFree(GetProcessHeap(), 0, req);
		InterlockedExchange(&g_gipFetchRunning, 0);
		EnterCriticalSection(&g_gipLock);
		tc_gip_set(NULL);
		g_gipStartupPending = 0;
		g_gipNextRefreshTick = nowTick + (DWORD)(g_gipRefreshHours * 60 * 60 * 1000);
		LeaveCriticalSection(&g_gipLock);
		return;
	}

	thread = CreateThread(NULL, 0, tc_gip_thread, req, 0, NULL);
	if (!thread) {
		HeapFree(GetProcessHeap(), 0, req);
		InterlockedExchange(&g_gipFetchRunning, 0);
		return;
	}
	CloseHandle(thread);
}

static const char* tc_custom_get_emit_value(const TC_CUSTOM_VAR_ENTRY* e)
{
	if (!e) return "";
#if TC_CUSTOM_USE_UTF8_VALUE
	/* Guarded cutover: prefer UTF-8 companion value when enabled. */
	if (e->valueUtf8[0]) return e->valueUtf8;
#endif
	return e->value;
}

static const char* tc_custom_get_value(int index1)
{
	TC_CUSTOM_VAR_ENTRY* e;
	DWORD nowTick;
	if (index1 < 1 || index1 > TC_CUSTOM_VAR_MAX) return "";
	if (!g_customSettingsLoaded) CustomFormatVarsReadSettings();
	e = &g_customVars[index1 - 1];
	nowTick = GetTickCount();
	tc_custom_refresh_one(index1 - 1, nowTick, FALSE);
	return tc_custom_get_emit_value(e);
}

extern int iFreeRes[3], totalCPUUsage, iBatteryLife, iVolume, totalGPUUsage;
extern int iCPUClock[];
extern int CPUClock2[];
extern int CPUClock2Ave;

extern MEMORYSTATUSEX msMemory;
extern double temperatures[];
extern double voltages[];
extern double fans[];

extern int CPUUsage[];
extern double net[];
extern double diskFree[];
extern double diskAll[];
extern double diskRateRead[];
extern double diskRateWrite[];
extern double diskRateTotal[];
extern int blt_h, blt_m, blt_s, pw_mode;
extern BYTE bat_flag;	//added for charge status by TTTT
extern BOOL b_Charging;

extern int g_InternetConnectStat_Win10;	//added for Internet Connectionstatus by TTTT
extern char icp_SSID_APName[];


extern int currentLTEProfNum;

extern int internetConnectProfNum;


extern const int kMegabytesPerGigabyte;

extern BOOL flag_VPN;
extern int active_physical_adapter_Win10;



extern BOOL b_SafeMode;
extern BOOL b_ExcessNetProfiles;

extern BOOL muteStatus;

extern char strLTE[];
extern char charLTE[];
extern char strMute[];

// IP addresses added by TTTT
extern char ipLTE[];
extern char ipEther[];
extern char ipWiFi[];
extern char ipVPN[];
extern char ipActive[];

extern BOOL b_MeteredNetNow;

extern COLORREF colfore;

extern char strDispStatus[];



extern BOOL b_DataPlanRetreveOK;

extern char activeSSID[];
extern char activeAPName[];

extern int NetMIX_Length;
extern int SSID_AP_Length;
extern int ExtTXT_Length;
extern char ExtTXT_String[];

extern BOOL b_FlagTimerAdjust;

BOOL b_FlagNextDay;
BOOL b_FlagPrevDay;
BOOL b_FlagNextMonth;
BOOL b_FlagPrevMonth;

BOOL b_SummerTime_US = FALSE;
BOOL b_SummerTime_Europe = FALSE;

BOOL b_exist_DOWzone = FALSE;

extern int nLogicalProcessors;
extern BOOL b_EnableClock2;

extern int numPDHGPUInstance;
extern int pdhTemperature;
extern double pdhTemperatureDouble;
extern BOOL b_TempAvailable;

/*------------------------------------------------
  GetLocaleInfo() for 95/NT
--------------------------------------------------*/
int GetLocaleInfoCompat(WORD wLanguageID, LCTYPE LCType, char* dst, int n)
{
	int r;
	LCID Locale;

	*dst = 0;
	Locale = MAKELCID(wLanguageID, SORT_DEFAULT);
	{
		WCHAR* pw;
		pw = (WCHAR*)GlobalAllocPtr(GHND, sizeof(WCHAR)*(n+1));
		*pw = 0;
		r = GetLocaleInfoW(Locale, LCType, pw, n);
		if(r) {
			/* Compatibility boundary: locale helper keeps ANSI-return contract for legacy callers. */
			tc_utf16_to_ansi_compat((UINT)codepage, pw, dst, n);
		}
		GlobalFreePtr(pw);
	}
	return r;
}

/*------------------------------------------------
  GetDateFormat() for 95/NT
--------------------------------------------------*/
int GetDateFormatCompat(WORD wLanguageID, DWORD dwFlags, CONST SYSTEMTIME *t,
	char* fmt, char* dst, int n)
{
	int r;
	LCID Locale;

	*dst = 0;
	Locale = MAKELCID(wLanguageID, SORT_DEFAULT);
	{
		WCHAR* pw1, *pw2;
		pw1 = NULL;
		if(fmt)
		{
			pw1 = (WCHAR*)GlobalAllocPtr(GHND,
				sizeof(WCHAR)*(strlen(fmt)+1));
			if(pw1) {
				/* ※ 旧形式フォーマット文字列互換：設定 codepage で wide 化 */
				tc_ansi_to_utf16_compat((UINT)codepage, fmt, pw1, (int)strlen(fmt) + 1);
			}
		}
		pw2 = (WCHAR*)GlobalAllocPtr(GHND, sizeof(WCHAR)*(n+1));
		r = GetDateFormatW(Locale, dwFlags, t, pw1, pw2, n);
		if(r) {
			/* Compatibility boundary: date-format helper keeps ANSI-return contract for legacy callers. */
			tc_utf16_to_ansi_compat((UINT)codepage, pw2, dst, n);
		}
		if(pw1) GlobalFreePtr(pw1);
		GlobalFreePtr(pw2);
	}
	return r;
}

/*------------------------------------------------
  GetTimeFormat() for 95/NT
--------------------------------------------------*/
int GetTimeFormatCompat(WORD wLanguageID, DWORD dwFlags, CONST SYSTEMTIME *t,
	char* fmt, char* dst, int n)
{
	int r;
	LCID Locale;

	*dst = 0;
	Locale = MAKELCID(wLanguageID, SORT_DEFAULT);
	{
		WCHAR* pw1, *pw2;
		pw1 = NULL;
		if(fmt)
		{
			pw1 = (WCHAR*)GlobalAllocPtr(GHND,
				sizeof(WCHAR)*(strlen(fmt)+1));
			if(pw1) {
				/* Legacy format pattern bytes follow selected locale codepage; decode using that boundary. */
				tc_ansi_to_utf16_compat((UINT)codepage, fmt, pw1, (int)strlen(fmt) + 1);
			}
		}
		pw2 = (WCHAR*)GlobalAllocPtr(GHND, sizeof(WCHAR)*(n+1));
		r = GetTimeFormatW(Locale, dwFlags, t, pw1, pw2, n);
		if(r) {
			/* Compatibility boundary: time-format helper keeps ANSI-return contract for legacy callers. */
			tc_utf16_to_ansi_compat((UINT)codepage, pw2, dst, n);
		}
		if(pw1) GlobalFreePtr(pw1);
		GlobalFreePtr(pw2);
	}
	return r;
}


/*------------------------------------------------
  load strings of day, month
--------------------------------------------------*/
void InitFormat(SYSTEMTIME* lt)
{
	char s[80], *p;
//	int i, ilang, ioptcal;
	int i, ioptcal;

	ilang = GetMyRegLong("Format", "Locale", (int)GetUserDefaultLangID());

	codepage = 0;
	{
		wchar_t wcp[16];
		/* Default ANSI codepage number is numeric-only, so read it directly via W API. */
		if (GetLocaleInfoW(MAKELCID((WORD)ilang, SORT_DEFAULT), LOCALE_IDEFAULTANSICODEPAGE, wcp, (int)(sizeof(wcp) / sizeof(wcp[0]))) > 0)
		{
			const wchar_t* pcp = wcp;
			while (L'0' <= *pcp && *pcp <= L'9')
				codepage = codepage * 10 + (int)(*pcp++ - L'0');
			if(!IsValidCodePage(codepage)) codepage = 0;
		}
	}

	i = lt->wDayOfWeek;
	if (i > 6) i = 0;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVDAYNAME1 + i,
		DayOfWeekShortNext, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SDAYNAME1 + i,
		DayOfWeekLongNext, 30);
	i--;
	if(i < 0) i = 6;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVDAYNAME1 + i,
		DayOfWeekShort, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SDAYNAME1 + i,
		DayOfWeekLong, 30);
	i--;
	if (i < 0) i = 6;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVDAYNAME1 + i,
		DayOfWeekShortPrev, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SDAYNAME1 + i,
		DayOfWeekLongPrev, 30);

	


	i = lt->wMonth; 
	if (i > 11) i = 0;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVMONTHNAME1 + i,
		MonthShortNext, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SMONTHNAME1 + i,
		MonthLongNext, 30);
	i--;
	if (i < 0) i = 11;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVMONTHNAME1 + i,
		MonthShort, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SMONTHNAME1 + i,
		MonthLong, 30);
	i--;
	if (i < 0) i = 11;
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SABBREVMONTHNAME1 + i,
		MonthShortPrev, 10);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_SMONTHNAME1 + i,
		MonthLongPrev, 30);


	GetLocaleInfoCompat((WORD)ilang, LOCALE_S1159, AM, 10);
	GetMyRegStr("Format", "AMsymbol", s, 80, AM);
	if(s[0] == 0) strcpy(s, "AM");
	strcpy(AM, s);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_S2359, PM, 10);
	GetMyRegStr("Format", "PMsymbol", s, 80, PM);
	if(s[0] == 0) strcpy(s, "PM");
	strcpy(PM, s);

	GetLocaleInfoCompat((WORD)ilang, LOCALE_SDATE, SDate, 4);
	GetLocaleInfoCompat((WORD)ilang, LOCALE_STIME, STime, 4);

	EraStr[0] = 0;
	AltYear = -1;

	ioptcal = 0;
	if(GetLocaleInfoCompat((WORD)ilang, LOCALE_IOPTIONALCALENDAR,
		s, 10))
	{
		ioptcal = 0;
		p = s;
		while('0' <= *p && *p <= '9')
			ioptcal = ioptcal * 10 + *p++ - '0';
	}
	if(ioptcal < 3) ilang = LANG_USER_DEFAULT;

	if(GetDateFormatCompat((WORD)ilang,
		DATE_USE_ALT_CALENDAR, lt, "gg", s, 12) != 0);
		strcpy(EraStr, s);

	if(GetDateFormatCompat((WORD)ilang,
		DATE_USE_ALT_CALENDAR, lt, "yyyy", s, 6) != 0)
	{
		if(s[0])
		{
			p = s;
			AltYear = 0;
			while('0' <= *p && *p <= '9')
				AltYear = AltYear * 10 + *p++ - '0';
		}
	}
}

BOOL GetNumFormat(char **sp, char x, char c, int *len, int *slen, BOOL *bComma)
{
	char *p;
	int n, ns;

	p = *sp;
	n = 0;
	ns = 0;

	while (*p == '_')
	{
		ns++;
		p++;
	}
	if (*p != x && *p != c) return FALSE;
	while (*p == x)
	{
		n++;
		p++;
	}
	while (*p == c)
	{
		n++;
		p++;
		*bComma = TRUE;
	}

	*len = n+ns;
	*slen = ns;
	*sp = p;
	return TRUE;
}

int SetNumFormat(char **dp, int n, int len, int slen, BOOL bComma)	//返される文字列の長さは、lenと実際の必要文字数のうち長いほうになる。
{
	char *p;
	int minlen,i,ii;
	int int_max_value = 1000000000; // 10^nしたときに桁あふれを起こさずに処理できる最大値
	int ret;

	p = *dp;

	for (i=10,minlen=1; i<int_max_value +1; i*=10,minlen++)
		if (n < i) break;
	if (bComma)
	{
		if (minlen%3 == 0)
			minlen += minlen/3 - 1;
		else
			minlen += minlen/3;
	}

	if (minlen < len)
	{
		ret = len;
	}
	else
	{
		ret = minlen;
	}

	while (minlen < len)
	{
		if (slen > 0) { *p++ = ' '; slen--; }
		else { *p++ = '0'; }
		len--;
	}
	for (i=minlen-1,ii=1; i>=0; i--,ii++)
	{
		*(p+i) = (char)((n%10)+'0');
		if (ii%3 == 0 && i != 0 && bComma)
			*(p+--i) = ',';
		n/=10;
	}
	p += minlen;

	*dp = p;

	return ret;		//文字数を返答
}




static BOOL tc_is_alpha_ascii_w(WCHAR ch)
{
	return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

static void tc_wappend_char(WCHAR** dp, int* remain, WCHAR ch)
{
	if (!dp || !*dp || !remain || *remain <= 1) return;
	**dp = ch;
	(*dp)++;
	(*remain)--;
	**dp = L'\0';
}

static void tc_wappend_text(WCHAR** dp, int* remain, const WCHAR* src)
{
	if (!src) return;
	while (*src) {
		tc_wappend_char(dp, remain, *src++);
	}
}

static void tc_wappend_ascii(WCHAR** dp, int* remain, const char* src)
{
	if (!src) return;
	while (*src) {
		tc_wappend_char(dp, remain, (WCHAR)(unsigned char)(*src));
		src++;
	}
}

static void tc_iappend_char(char** ip, int* remain, char mark)
{
	if (!ip || !*ip || !remain || *remain <= 1) return;
	**ip = mark;
	(*ip)++;
	(*remain)--;
	**ip = '\0';
}

static void tc_iappend_span(char** ip, int* remain, const WCHAR* start, const WCHAR* end, char mark)
{
	int count;
	if (!start || !end || end <= start) return;
	count = (int)(end - start);
	while (count-- > 0) {
		tc_iappend_char(ip, remain, mark);
	}
}

static BOOL tc_custom_emit_if_token_w(WCHAR** dp, int* remain, const WCHAR** spPtr)
{
	const WCHAR* sp;
	const WCHAR* p;
	int num = 0;
	char outUtf8[TC_CUSTOM_VALUE_MAX];
	if (!dp || !remain || !spPtr || !*spPtr) return FALSE;
	sp = *spPtr;
	if (_wcsnicmp(sp, L"CUSTOM", 6) != 0) return FALSE;
	p = sp + 6;
	if (!(*p >= L'0' && *p <= L'9')) return FALSE;
	while (*p >= L'0' && *p <= L'9') {
		num = (num * 10) + (int)(*p - L'0');
		if (num > TC_CUSTOM_VAR_MAX) return FALSE;
		p++;
	}
	if (num < 1 || num > TC_CUSTOM_VAR_MAX) return FALSE;
	if (*p && (((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) || (*p >= L'0' && *p <= L'9') || *p == L'_')) return FALSE;
	lstrcpyn(outUtf8, tc_custom_get_value(num), (int)sizeof(outUtf8));
	if (outUtf8[0]) {
		WCHAR outW[TC_CUSTOM_VALUE_MAX];
		if (tc_custom_text_to_utf16_compat(outUtf8, outW, (int)(sizeof(outW) / sizeof(outW[0])))) {
			tc_wappend_text(dp, remain, outW);
		}
		else {
			tc_wappend_ascii(dp, remain, outUtf8);
		}
	}
	*spPtr = p;
	return TRUE;
}

static void tc_wappend_uint_fixed(WCHAR** dp, int* remain, int value, int width)
{
	WCHAR tmp[16];
	int i;
	for (i = width - 1; i >= 0; --i) {
		tmp[i] = (WCHAR)(L'0' + (value % 10));
		value /= 10;
	}
	for (i = 0; i < width; ++i) tc_wappend_char(dp, remain, tmp[i]);
}

static void tc_wappend_uint_var(WCHAR** dp, int* remain, int value)
{
	WCHAR tmp[16];
	int n = 0;
	if (value == 0) {
		tc_wappend_char(dp, remain, L'0');
		return;
	}
	while (value > 0 && n < (int)(sizeof(tmp) / sizeof(tmp[0]))) {
		tmp[n++] = (WCHAR)(L'0' + (value % 10));
		value /= 10;
	}
	while (n > 0) tc_wappend_char(dp, remain, tmp[--n]);
}

static void tc_get_locale_sdate_w(WCHAR* buf, int cch)
{
	if (!buf || cch <= 0) return;
	if (GetLocaleInfoW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), LOCALE_SDATE, buf, cch) <= 0) {
		lstrcpynW(buf, L"/", cch);
	}
}

static void tc_get_locale_stime_w(WCHAR* buf, int cch)
{
	if (!buf || cch <= 0) return;
	if (GetLocaleInfoW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), LOCALE_STIME, buf, cch) <= 0) {
		lstrcpynW(buf, L":", cch);
	}
}

static void tc_get_locale_ampm_w(BOOL isAm, WCHAR* buf, int cch)
{
	int lctype = isAm ? LOCALE_S1159 : LOCALE_S2359;
	const char* entry = isAm ? "AMsymbol" : "PMsymbol";
	char value[80];
	if (!buf || cch <= 0) return;

	value[0] = 0;

	GetMyRegStr("Format", entry, value, (int)sizeof(value), "");

	if (value[0] != 0 && MultiByteToWideChar(CP_UTF8, 0, value, -1, buf, cch) > 0) {

		return;

	}

	if (GetLocaleInfoW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), lctype, buf, cch) <= 0) {
		lstrcpynW(buf, isAm ? L"AM" : L"PM", cch);
	}
}

static int tc_hour_adjust_w(int hour)
{
	if (bHour12) {
		if (hour > 12) hour -= 12;
		else if (hour == 0) hour = 12;
		if (hour == 12 && bHourZero) hour = 0;
	}
	return hour;
}

static BOOL tc_is_digit_ascii_w(WCHAR ch)
{
	return (ch >= L'0' && ch <= L'9') ? TRUE : FALSE;
}

static BOOL tc_parse_num_format_w(const WCHAR** psp, int* len, int* slen, BOOL* bComma)
{
	const WCHAR* p;
	int n = 0;
	int ns = 0;
	BOOL comma = FALSE;

	if (!psp || !*psp) return FALSE;
	p = *psp;
	while (*p == L'_') {
		ns++;
		p++;
	}
	if (*p != L'x' && *p != L',') return FALSE;
	while (*p == L'x') {
		n++;
		p++;
	}
	while (*p == L',') {
		n++;
		p++;
		comma = TRUE;
	}

	if (len) *len = n + ns;
	if (slen) *slen = ns;
	if (bComma) *bComma = comma;
	*psp = p;
	return TRUE;
}

static BOOL tc_parse_num_format_w_tok(const WCHAR** psp, WCHAR token, WCHAR commaTok, int* len, int* slen, BOOL* bComma)
{
	const WCHAR* p;
	int n = 0;
	int ns = 0;
	BOOL comma = FALSE;

	if (!psp || !*psp) return FALSE;
	p = *psp;
	while (*p == L'_') {
		ns++;
		p++;
	}
	if (*p != token && *p != commaTok) return FALSE;
	while (*p == token) {
		n++;
		p++;
	}
	while (*p == commaTok) {
		n++;
		p++;
		comma = TRUE;
	}

	if (len) *len = n + ns;
	if (slen) *slen = ns;
	if (bComma) *bComma = comma;
	*psp = p;
	return TRUE;
}

static void tc_wappend_num_format(WCHAR** dp, int* remain, int n, int len, int slen, BOOL bComma)
{
	WCHAR tmp[64];
	int minlen = 1;
	int i;
	int ii;
	int pos = 0;
	int emitLen;

	if (!dp || !*dp || !remain || *remain <= 1) return;
	if (n < 0) n = 0;

	for (i = 10; i < 1000000001; i *= 10, minlen++) {
		if (n < i) break;
	}
	if (bComma) {
		if (minlen % 3 == 0) minlen += minlen / 3 - 1;
		else minlen += minlen / 3;
	}
	emitLen = (minlen < len) ? len : minlen;

	while (minlen < len && pos < (int)(sizeof(tmp) / sizeof(tmp[0])) - 1) {
		if (slen > 0) {
			tmp[pos++] = L' ';
			slen--;
		}
		else {
			tmp[pos++] = L'0';
		}
		len--;
	}
	for (i = minlen - 1, ii = 1; i >= 0 && (pos + i) < (int)(sizeof(tmp) / sizeof(tmp[0])); i--, ii++) {
		tmp[pos + i] = (WCHAR)((n % 10) + L'0');
		if (ii % 3 == 0 && i != 0 && bComma) {
			tmp[pos + --i] = L',';
		}
		n /= 10;
	}
	for (i = 0; i < emitLen && i < (int)(sizeof(tmp) / sizeof(tmp[0])); ++i) {
		tc_wappend_char(dp, remain, tmp[i]);
	}
}

static BOOL tc_scan_uptime_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;
	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'S') return FALSE;
	p++;
	if (*p == L'T') {
		*psp = p + 1;
		return TRUE;
	}
	if (*p == L'd' || *p == L'a' || *p == L'h' || *p == L'n' || *p == L's') {
		p++;
		q = p;
		if (!tc_parse_num_format_w(&q, NULL, NULL, NULL)) return FALSE;
		*psp = q;
		return TRUE;
	}
	return FALSE;
}

static BOOL tc_scan_cpu_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;

	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'C') return FALSE;
	if (*(p + 1) != L'U' && *(p + 1) != L'C') return FALSE;
	p += 2;
	if (tc_is_digit_ascii_w(*p)) {
		p += 1;
	}
	else if (*p == L'e' && tc_is_digit_ascii_w(*(p + 1)) && tc_is_digit_ascii_w(*(p + 2))) {
		p += 3;
	}
	if (*p == L'M' || *p == L'G') {
		p += 1;
	}
	q = p;
	if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) {
		p = q;
	}
	if (*p == L'.') {
		p++;
		q = p;
		if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_scan_memory_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;

	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'M') return FALSE;

	if (*(p + 1) == L'K' || *(p + 1) == L'M' || *(p + 1) == L'G' || *(p + 1) == L'S') {
		p += 2;
	}
	else if ((*(p + 1) == L'T' || *(p + 1) == L'A' || *(p + 1) == L'U') &&
		(*(p + 2) == L'P' || *(p + 2) == L'F' || *(p + 2) == L'V') &&
		(*(p + 3) == L'K' || *(p + 3) == L'M' || *(p + 3) == L'P' || *(p + 3) == L'G')) {
		p += 4;
	}
	else {
		return FALSE;
	}

	q = p;
	if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) {
		p = q;
	}
	if (*p == L'.') {
		p++;
		q = p;
		if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) {
			p = q;
		}
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_emit_memory_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	ULONGLONG ms = (ULONGLONG)-1;
	ULONGLONG mst = (ULONGLONG)-1;
	double d_ms = 0.0;
	BOOL bFlagGB = FALSE;
	const ULONGLONG intMax = 2147483647ULL;

	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'M') return FALSE;

	if (*(p + 1) == L'K') {
		p += 2;
		ms = msMemory.ullAvailPhys / 1024ULL;
	}
	else if (*(p + 1) == L'M') {
		p += 2;
		ms = msMemory.ullAvailPhys / (1024ULL * 1024ULL);
	}
	else if (*(p + 1) == L'G') {
		p += 2;
		d_ms = (double)msMemory.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
		bFlagGB = TRUE;
	}
	else if (*(p + 1) == L'S') {
		p += 2;
		ms = (ULONGLONG)((double)msMemory.ullTotalPhys / (1024.0 * 1024.0 * (double)kMegabytesPerGigabyte));
	}
	else if (*(p + 1) == L'T') {
		if (*(p + 2) == L'P') ms = msMemory.ullTotalPhys;
		else if (*(p + 2) == L'F') ms = msMemory.ullTotalPageFile;
		else if (*(p + 2) == L'V') ms = msMemory.ullTotalVirtual;
		if (ms != (ULONGLONG)-1) {
			if (*(p + 3) == L'K') { ms /= 1024ULL; p += 4; }
			else if (*(p + 3) == L'M') { ms /= (1024ULL * 1024ULL); p += 4; }
			else if (*(p + 3) == L'G') {
				d_ms = (double)ms / (1024.0 * 1024.0 * 1024.0);
				bFlagGB = TRUE;
				p += 4;
			}
			else ms = (ULONGLONG)-1;
		}
		else {
			return FALSE;
		}
	}
	else if (*(p + 1) == L'A') {
		if (*(p + 2) == L'P') { ms = msMemory.ullAvailPhys; mst = msMemory.ullTotalPhys; }
		else if (*(p + 2) == L'F') { ms = msMemory.ullAvailPageFile; mst = msMemory.ullTotalPageFile; }
		else if (*(p + 2) == L'V') { ms = msMemory.ullAvailVirtual; mst = msMemory.ullTotalVirtual; }
		if (ms != (ULONGLONG)-1) {
			if (*(p + 3) == L'K') { ms /= 1024ULL; p += 4; }
			else if (*(p + 3) == L'M') { ms /= (1024ULL * 1024ULL); p += 4; }
			else if (*(p + 3) == L'P') { mst /= 100ULL; ms = mst ? (ms / mst) : 0ULL; p += 4; }
			else if (*(p + 3) == L'G') {
				d_ms = (double)ms / (1024.0 * 1024.0 * 1024.0);
				bFlagGB = TRUE;
				p += 4;
			}
			else ms = (ULONGLONG)-1;
		}
		else {
			return FALSE;
		}
	}
	else if (*(p + 1) == L'U') {
		if (*(p + 2) == L'P') { ms = msMemory.ullTotalPhys - msMemory.ullAvailPhys; mst = msMemory.ullTotalPhys; }
		else if (*(p + 2) == L'F') { ms = msMemory.ullTotalPageFile - msMemory.ullAvailPageFile; mst = msMemory.ullTotalPageFile; }
		else if (*(p + 2) == L'V') { ms = msMemory.ullTotalVirtual - msMemory.ullAvailVirtual; mst = msMemory.ullTotalVirtual; }
		if (ms != (ULONGLONG)-1) {
			if (*(p + 3) == L'K') { ms /= 1024ULL; p += 4; }
			else if (*(p + 3) == L'M') { ms /= (1024ULL * 1024ULL); p += 4; }
			else if (*(p + 3) == L'P') { mst /= 100ULL; ms = mst ? (ms / mst) : 0ULL; p += 4; }
			else if (*(p + 3) == L'G') {
				d_ms = (double)ms / (1024.0 * 1024.0 * 1024.0);
				bFlagGB = TRUE;
				p += 4;
			}
			else ms = (ULONGLONG)-1;
		}
		else {
			return FALSE;
		}
	}
	else {
		return FALSE;
	}

	if (bFlagGB) {
		int len = 1;
		int slen = 0;
		BOOL bComma = FALSE;
		int msi;
		const WCHAR* p2 = p;
		if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) p = p2;
		msi = (int)d_ms;
		if (msi < 0) msi = 0;
		tc_wappend_num_format(dp, remain, msi, len, slen, bComma);
		d_ms = d_ms - (double)msi;
		if (*p == L'.') {
			int dlen;
			int dslen;
			BOOL dComma = FALSE;
			int frac;
			p++;
			p2 = p;
			if (tc_parse_num_format_w(&p2, &dlen, &dslen, &dComma)) {
				int fmtLen;
				tc_wappend_char(dp, remain, L'.');
				fmtLen = dlen;
				if (fmtLen > 3) fmtLen = 3;
				while (fmtLen-- > 0) d_ms *= 10.0;
				frac = (int)d_ms;
				if (frac < 0) frac = 0;
				tc_wappend_num_format(dp, remain, frac, dlen > 3 ? 3 : dlen, dslen, FALSE);
				p = p2;
			}
		}
		*psp = p;
		return TRUE;
	}
	if (ms == (ULONGLONG)-1) return FALSE;
	{
		int len = 1;
		int slen = 0;
		BOOL bComma = FALSE;
		const WCHAR* p2 = p;
		int v;
		if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) p = p2;
		if (ms > intMax) ms = intMax;
		v = (int)ms;
		tc_wappend_num_format(dp, remain, v, len, slen, bComma);
		*psp = p;
		return TRUE;
	}
}

static void tc_net_auto_label_w(WCHAR* out, int outcch, double netk, double netm)
{
	if (!out || outcch <= 0) return;
	if ((netk < 1024.0) && (netk >= 0.0)) {
		swprintf(out, outcch, L"%4.0fKB", netk);
	}
	else if (netm < 10.0) {
		swprintf(out, outcch, L"%1.2fMB", netm);
	}
	else if (netm < 100.0) {
		swprintf(out, outcch, L"%2.1fMB", netm);
	}
	else if (netm < (double)kMegabytesPerGigabyte) {
		swprintf(out, outcch, L"%4.0fMB", netm);
	}
	else if (netm < (10.0 * (double)kMegabytesPerGigabyte)) {
		swprintf(out, outcch, L"%1.2fGB", (netm / (double)kMegabytesPerGigabyte));
	}
	else if (netm < (100.0 * (double)kMegabytesPerGigabyte)) {
		swprintf(out, outcch, L"%2.1fGB", (netm / (double)kMegabytesPerGigabyte));
	}
	else if (netm < (10000.0 * (double)kMegabytesPerGigabyte)) {
		swprintf(out, outcch, L"%4.0fGB", (netm / (double)kMegabytesPerGigabyte));
	}
	else {
		swprintf(out, outcch, L"%dGB", (int)(netm / (double)kMegabytesPerGigabyte));
	}
}

static void tc_diskrate_auto_label_w(WCHAR* out, int outcch, double bytesPerSec)
{
	double kbytesPerSec;
	double mbytesPerSec;
	double gbytesPerSec;

	if (!out || outcch <= 0) return;
	if (bytesPerSec < 0.0) bytesPerSec = 0.0;

	kbytesPerSec = bytesPerSec / 1024.0;
	mbytesPerSec = kbytesPerSec / 1024.0;
	gbytesPerSec = mbytesPerSec / 1024.0;

	if (bytesPerSec < 1024.0) {
		swprintf(out, outcch, L"%4.0fB/s", bytesPerSec);
	}
	else if (kbytesPerSec < 10.0) {
		swprintf(out, outcch, L"%1.2fKB/s", kbytesPerSec);
	}
	else if (kbytesPerSec < 100.0) {
		swprintf(out, outcch, L"%2.1fKB/s", kbytesPerSec);
	}
	else if (kbytesPerSec < 1024.0) {
		swprintf(out, outcch, L"%4.0fKB/s", kbytesPerSec);
	}
	else if (mbytesPerSec < 10.0) {
		swprintf(out, outcch, L"%1.2fMB/s", mbytesPerSec);
	}
	else if (mbytesPerSec < 100.0) {
		swprintf(out, outcch, L"%2.1fMB/s", mbytesPerSec);
	}
	else if (mbytesPerSec < 1024.0) {
		swprintf(out, outcch, L"%4.0fMB/s", mbytesPerSec);
	}
	else if (gbytesPerSec < 10.0) {
		swprintf(out, outcch, L"%1.2fGB/s", gbytesPerSec);
	}
	else if (gbytesPerSec < 100.0) {
		swprintf(out, outcch, L"%2.1fGB/s", gbytesPerSec);
	}
	else {
		swprintf(out, outcch, L"%4.0fGB/s", gbytesPerSec);
	}
}

static BOOL tc_scan_network_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;
	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (_wcsnicmp(p, L"SSID", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"WiFi", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"EthS", 4) == 0 || _wcsnicmp(p, L"EthL", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"EWLL", 4) == 0 || _wcsnicmp(p, L"EWLS", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"ICP", 3) == 0) {
		*psp = p + 3;
		return TRUE;
	}
	if (_wcsnicmp(p, L"LTE", 3) == 0) {
		*psp = p + 3;
		return TRUE;
	}
	if (_wcsnicmp(p, L"VPNS", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"WANP", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"APN", 3) == 0) {
		*psp = p + 3;
		return TRUE;
	}
	if (*p != L'N') return FALSE;

	if (_wcsnicmp(p, L"NMX1", 4) == 0 || _wcsnicmp(p, L"NMX2", 4) == 0) {

		*psp = p + 4;

		return TRUE;

	}


	if (_wcsnicmp(p, L"NRAA", 4) == 0 || _wcsnicmp(p, L"NSAA", 4) == 0) {
		*psp = p + 4;
		return TRUE;
	}

	if ((*(p + 1) == L'R' || *(p + 1) == L'S') &&
		(*(p + 2) == L'A' || *(p + 2) == L'S')) {
		WCHAR u = *(p + 3);
		BOOL ok = FALSE;
		if (*(p + 2) == L'A') {
			ok = (u == L'B' || u == L'K' || u == L'M' || u == L'G') ? TRUE : FALSE;
		}
		else {
			ok = (u == L'B' || u == L'K' || u == L'M') ? TRUE : FALSE;
		}
		if (!ok) return FALSE;
		p += 4;
		q = p;
		if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
		if (*p == L'.') {
			p++;
			q = p;
			if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
		}
		*psp = p;
		return TRUE;
	}
	return FALSE;
}

static BOOL tc_emit_network_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (_wcsnicmp(p, L"SSID", 4) == 0) {
		tc_wappend_utf8_fixed_w(dp, remain, activeSSID, SSID_AP_Length);
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"WiFi", 4) == 0) {
		if (net[15] == 2) tc_wappend_text(dp, remain, L"WiFi*");
		else if (net[15] == 1) tc_wappend_text(dp, remain, L"WiFi ");
		else tc_wappend_text(dp, remain, L"     ");
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"EthS", 4) == 0) {
		if (net[18] == 2) tc_wappend_text(dp, remain, L"Eth*");
		else if (net[18] == 1) tc_wappend_text(dp, remain, L"Eth ");
		else tc_wappend_text(dp, remain, L"    ");
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"EthL", 4) == 0) {
		if (net[18] == 2) tc_wappend_text(dp, remain, L"Ethernet*");
		else if (net[18] == 1) tc_wappend_text(dp, remain, L"Ethernet ");
		else tc_wappend_text(dp, remain, L"         ");
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"EWLL", 4) == 0 || _wcsnicmp(p, L"EWLS", 4) == 0) {
		WCHAR lteBuf[8];
		WCHAR lteCh = L'L';
		BOOL spaced = (_wcsnicmp(p, L"EWLL", 4) == 0) ? TRUE : FALSE;
		if (tc_custom_utf8_to_utf16(charLTE, lteBuf, (int)(sizeof(lteBuf) / sizeof(lteBuf[0]))) && lteBuf[0] != L'\0') {
			lteCh = lteBuf[0];
		}
		tc_wappend_char(dp, remain, (net[18] > 0) ? L'E' : L' ');
		tc_wappend_char(dp, remain, (net[18] == 2) ? L'*' : L' ');
		if (spaced) tc_wappend_char(dp, remain, L' ');
		tc_wappend_char(dp, remain, (net[15] > 0) ? L'W' : L' ');
		tc_wappend_char(dp, remain, (net[15] == 2) ? L'*' : L' ');
		if (spaced) tc_wappend_char(dp, remain, L' ');
		tc_wappend_char(dp, remain, (net[12] > 0) ? lteCh : L' ');
		tc_wappend_char(dp, remain, (net[12] == 2) ? L'*' : L' ');
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"ICP", 3) == 0) {
		WCHAR lteBuf[8];
		WCHAR lteCh = L'L';
		if (tc_custom_utf8_to_utf16(charLTE, lteBuf, (int)(sizeof(lteBuf) / sizeof(lteBuf[0]))) && lteBuf[0] != L'\0') {
			lteCh = lteBuf[0];
		}
		if (g_InternetConnectStat_Win10 == 0 && active_physical_adapter_Win10 == 0) {
			tc_wappend_char(dp, remain, L'E');
		}
		else if (g_InternetConnectStat_Win10 == 1) {
			tc_wappend_char(dp, remain, L'W');
		}
		else if (g_InternetConnectStat_Win10 == 2) {
			tc_wappend_char(dp, remain, lteCh);
		}
		else if (g_InternetConnectStat_Win10 == 4 || b_MeteredNetNow) {
			tc_wappend_char(dp, remain, L'M');
		}
		else {
			tc_wappend_char(dp, remain, L'-');
		}
		*psp = p + 3;
		return TRUE;
	}
	if (_wcsnicmp(p, L"LTE", 3) == 0) {
		WCHAR wbuf[64];
		int len;
		int i;
		if (!tc_custom_utf8_to_utf16(strLTE, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])))) {
			wbuf[0] = L'\0';
		}
		len = (int)lstrlenW(wbuf);
		if (net[12] == 2) {
			tc_wappend_text(dp, remain, wbuf);
			tc_wappend_char(dp, remain, L'*');
		}
		else if (net[12] == 1) {
			tc_wappend_text(dp, remain, wbuf);
			tc_wappend_char(dp, remain, L' ');
		}
		else {
			for (i = 0; i < len + 1; ++i) tc_wappend_char(dp, remain, L' ');
		}
		*psp = p + 3;
		return TRUE;
	}
	if (_wcsnicmp(p, L"VPNS", 4) == 0) {
		tc_wappend_text(dp, remain, flag_VPN ? L"VPN" : L"   ");
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"WANP", 4) == 0) {
		WCHAR wbuf[16];
		if (currentLTEProfNum != -1) {
			wsprintfW(wbuf, L"%2d", currentLTEProfNum);
		}
		else {
			lstrcpyW(wbuf, L"N/A");
		}
		tc_wappend_text(dp, remain, wbuf);
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"APN", 3) == 0) {
		tc_wappend_utf8_fixed_w(dp, remain, activeAPName, SSID_AP_Length);
		*psp = p + 3;
		return TRUE;
	}
	if (*p != L'N') return FALSE;

	if (_wcsnicmp(p, L"NMX1", 4) == 0 || _wcsnicmp(p, L"NMX2", 4) == 0) {

		tc_emit_net_mix_w(dp, remain);

		*psp = p + 4;

		return TRUE;

	}


	if (_wcsnicmp(p, L"NRAA", 4) == 0) {
		WCHAR buf[32];
		tc_net_auto_label_w(buf, (int)(sizeof(buf)/sizeof(buf[0])), net[4], net[8]);
		tc_wappend_text(dp, remain, buf);
		*psp = p + 4;
		return TRUE;
	}
	if (_wcsnicmp(p, L"NSAA", 4) == 0) {
		WCHAR buf[32];
		tc_net_auto_label_w(buf, (int)(sizeof(buf)/sizeof(buf[0])), net[5], net[9]);
		tc_wappend_text(dp, remain, buf);
		*psp = p + 4;
		return TRUE;
	}

	if ((*(p + 1) == L'R' || *(p + 1) == L'S') &&
		(*(p + 2) == L'A' || *(p + 2) == L'S')) {
		double ntd = -1.0;
		int nt;
		int len = 1;
		int slen = 0;
		BOOL bComma = FALSE;
		const WCHAR* p2;
		WCHAR u = *(p + 3);

		if (*(p + 1) == L'R') {
			if (*(p + 2) == L'A') {
				if (u == L'B') ntd = net[0];
				else if (u == L'K') ntd = net[4];
				else if (u == L'M') ntd = net[8];
				else if (u == L'G') ntd = (int)(net[8] / kMegabytesPerGigabyte);
			}
			else {
				if (u == L'B') ntd = net[2];
				else if (u == L'K') ntd = net[6];
				else if (u == L'M') ntd = net[10];
			}
		}
		else {
			if (*(p + 2) == L'A') {
				if (u == L'B') ntd = net[1];
				else if (u == L'K') ntd = net[5];
				else if (u == L'M') ntd = net[9];
				else if (u == L'G') ntd = (int)(net[9] / kMegabytesPerGigabyte);
			}
			else {
				if (u == L'B') ntd = net[3];
				else if (u == L'K') ntd = net[7];
				else if (u == L'M') ntd = net[11];
			}
		}
		if (ntd < 0.0) return FALSE;

		p += 4;
		nt = (int)ntd;
		if (nt < 0) nt = 0;
		p2 = p;
		if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) p = p2;
		tc_wappend_num_format(dp, remain, nt, len, slen, bComma);
		ntd = ntd - (double)nt;
		if (*p == L'.') {
			int dlen;
			int dslen;
			BOOL dComma = FALSE;
			p++;
			p2 = p;
			if (tc_parse_num_format_w(&p2, &dlen, &dslen, &dComma)) {
				int fmtLen = dlen;
				if (fmtLen > 3) fmtLen = 3;
				tc_wappend_char(dp, remain, L'.');
				while (fmtLen-- > 0) ntd *= 10.0;
				nt = (int)ntd;
				if (nt < 0) nt = 0;
				tc_wappend_num_format(dp, remain, nt, dlen > 3 ? 3 : dlen, 0, FALSE);
				p = p2;
			}
		}
		*psp = p;
		return TRUE;
	}
	return FALSE;
}

static BOOL tc_hdd_token_dv_w(WCHAR ch, int* dv)
{
	if (!dv) return FALSE;
	if (ch >= L'A' && ch <= L'Z') {
		*dv = (int)(ch - L'A');
		return TRUE;
	}
	if (ch >= L'0' && ch <= L'9') {
		*dv = (int)(ch - L'0') + 26;
		return TRUE;
	}
	return FALSE;
}

static BOOL tc_scan_hdd_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;
	int dv;
	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'H') return FALSE;
	if (*(p + 1) != L'A' && *(p + 1) != L'U' && *(p + 1) != L'T') return FALSE;
	if (!tc_hdd_token_dv_w(*(p + 2), &dv)) return FALSE;
	if (*(p + 3) != L'M' && *(p + 3) != L'G' && *(p + 3) != L'T' && *(p + 3) != L'P') return FALSE;
	if (*(p + 3) == L'P' && *(p + 1) == L'T') return FALSE;
	p += 4;
	q = p;
	if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	if (*p == L'.') {
		p++;
		q = p;
		if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_scan_diskrate_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;
	int dv;

	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'H') return FALSE;
	if (*(p + 1) != L'R' && *(p + 1) != L'W' && *(p + 1) != L'D') return FALSE;
	if (!tc_hdd_token_dv_w(*(p + 2), &dv)) return FALSE;
	if (dv < 0 || dv >= 26) return FALSE;
	if (*(p + 3) != L'B' && *(p + 3) != L'K' && *(p + 3) != L'M' && *(p + 3) != L'A') return FALSE;
	p += 4;
	q = p;
	if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	if (*p == L'.') {
		p++;
		q = p;
		if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_emit_hdd_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	int dv;
	double dsk = 0.0;
	int dski;
	int len = 1;
	int slen = 0;
	BOOL bComma = FALSE;
	const WCHAR* p2;
	WCHAR mode;
	WCHAR unit;

	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'H') return FALSE;
	mode = *(p + 1);
	unit = *(p + 3);
	if (mode != L'A' && mode != L'U' && mode != L'T') return FALSE;
	if (!tc_hdd_token_dv_w(*(p + 2), &dv)) return FALSE;
	if (unit != L'M' && unit != L'G' && unit != L'T' && unit != L'P') return FALSE;
	if (unit == L'P' && mode == L'T') return FALSE;

	if (mode == L'T') {
		if (unit == L'M') dsk = diskAll[dv];
		else if (unit == L'G') dsk = diskAll[dv + 36];
		else if (unit == L'T') dsk = diskAll[dv + 36] / 1024.0;
	}
	else if (mode == L'A') {
		if (unit == L'M') dsk = diskFree[dv];
		else if (unit == L'G') dsk = diskFree[dv + 36];
		else if (unit == L'T') dsk = diskFree[dv + 36] / 1024.0;
		else if (unit == L'P') {
			if (diskAll[dv] != 0.0) dsk = (diskFree[dv] / diskAll[dv]) * 100.0;
			else dsk = 0.0;
		}
	}
	else {
		if (unit == L'M') dsk = diskAll[dv] - diskFree[dv];
		else if (unit == L'G') dsk = diskAll[dv + 36] - diskFree[dv + 36];
		else if (unit == L'T') dsk = (diskAll[dv + 36] - diskFree[dv + 36]) / 1024.0;
		else if (unit == L'P') {
			if (diskAll[dv] != 0.0) dsk = ((diskAll[dv] - diskFree[dv]) / diskAll[dv]) * 100.0;
			else dsk = 0.0;
		}
	}

	p += 4;
	dski = (int)dsk;
	if (dski < 0) dski = 0;
	p2 = p;
	if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) p = p2;
	tc_wappend_num_format(dp, remain, dski, len, slen, bComma);
	dsk = dsk - (double)dski;
	if (*p == L'.') {
		int dlen;
		int dslen;
		BOOL dComma = FALSE;
		int frac;
		p++;
		p2 = p;
		if (tc_parse_num_format_w(&p2, &dlen, &dslen, &dComma)) {
			int fmtLen = dlen;
			if (fmtLen > 6) fmtLen = 6;
			tc_wappend_char(dp, remain, L'.');
			while (fmtLen-- > 0) dsk *= 10.0;
			frac = (int)dsk;
			if (frac < 0) frac = 0;
			tc_wappend_num_format(dp, remain, frac, dlen > 6 ? 6 : dlen, 0, FALSE);
			p = p2;
		}
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_emit_diskrate_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* p2;
	double rate = -1.0;
	int dv;
	int len = 1;
	int slen = 0;
	BOOL bComma = FALSE;
	WCHAR mode;
	WCHAR unit;

	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'H') return FALSE;
	mode = *(p + 1);
	unit = *(p + 3);
	if (mode != L'R' && mode != L'W' && mode != L'D') return FALSE;
	if (!tc_hdd_token_dv_w(*(p + 2), &dv)) return FALSE;
	if (dv < 0 || dv >= 26) return FALSE;
	if (unit != L'B' && unit != L'K' && unit != L'M' && unit != L'A') return FALSE;

	if (mode == L'R') rate = diskRateRead[dv];
	else if (mode == L'W') rate = diskRateWrite[dv];
	else rate = diskRateTotal[dv];

	if (rate < 0.0) rate = 0.0;

	if (unit == L'A') {
		WCHAR buf[32];
		tc_diskrate_auto_label_w(buf, (int)(sizeof(buf) / sizeof(buf[0])), rate);
		tc_wappend_text(dp, remain, buf);
		*psp = p + 4;
		return TRUE;
	}

	if (unit == L'K') rate /= 1024.0;
	else if (unit == L'M') rate /= (1024.0 * 1024.0);

	p += 4;
	{
		int ratei;
		p2 = p;
		if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) p = p2;
		ratei = (int)rate;
		if (ratei < 0) ratei = 0;
		tc_wappend_num_format(dp, remain, ratei, len, slen, bComma);
		rate = rate - (double)ratei;
		if (*p == L'.') {
			int dlen;
			int dslen;
			BOOL dComma = FALSE;
			int frac;
			p++;
			p2 = p;
			if (tc_parse_num_format_w(&p2, &dlen, &dslen, &dComma)) {
				int fmtLen = dlen;
				if (fmtLen > 3) fmtLen = 3;
				tc_wappend_char(dp, remain, L'.');
				while (fmtLen-- > 0) rate *= 10.0;
				frac = (int)rate;
				if (frac < 0) frac = 0;
				tc_wappend_num_format(dp, remain, frac, dlen > 3 ? 3 : dlen, 0, FALSE);
				p = p2;
			}
		}
	}

	*psp = p;
	return TRUE;
}

static BOOL tc_scan_gpu_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	const WCHAR* q;
	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'G') return FALSE;
	if (*(p + 1) != L'U' && *(p + 1) != L'I') return FALSE;
	p += 2;
	q = p;
	if (tc_parse_num_format_w(&q, NULL, NULL, NULL)) p = q;
	*psp = p;
	return TRUE;
}

static BOOL tc_scan_ip_token_w(const WCHAR** psp)
{
	const WCHAR* p;
	if (!psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'I' || *(p + 1) != L'P') return FALSE;
	if (*(p + 2) != L'A' && *(p + 2) != L'E' && *(p + 2) != L'W' && *(p + 2) != L'L' && *(p + 2) != L'V') return FALSE;
	*psp = p + 3;
	return TRUE;
}

static void tc_wappend_ansi_fixed_w(WCHAR** dp, int* remain, const char* src, int fixed)
{
	WCHAR wbuf[128];
	int len = 0;
	int i;
	if (!dp || !*dp || !remain || !src || fixed <= 0) return;
	/* ※ 表示トークンは codepage 指定互換入力を許可（Shift-JIS 経路維持） */
	if (tc_ansi_to_utf16_compat((UINT)codepage, src, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) {
		wbuf[0] = L'\0';
	}
	len = (int)lstrlenW(wbuf);
	if (len > fixed) len = fixed;
	for (i = 0; i < len; ++i) tc_wappend_char(dp, remain, wbuf[i]);
	for (; i < fixed; ++i) tc_wappend_char(dp, remain, L' ');
}

static void tc_wappend_utf8_fixed_w(WCHAR** dp, int* remain, const char* src, int fixed)
{
	WCHAR wbuf[128];
	int len = 0;
	int i;
	if (!dp || !*dp || !remain || !src || fixed <= 0) return;
	if (!tc_custom_utf8_to_utf16(src, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])))) {
		wbuf[0] = L'\0';
	}
	len = (int)lstrlenW(wbuf);
	if (len > fixed) len = fixed;
	for (i = 0; i < len; ++i) tc_wappend_char(dp, remain, wbuf[i]);
	for (; i < fixed; ++i) tc_wappend_char(dp, remain, L' ');
}

static void tc_emit_net_mix_w(WCHAR** dp, int* remain)

{

	char label[64];

	if (!dp || !*dp || !remain) return;

	label[0] = '\0';

	if (g_InternetConnectStat_Win10 == 0 && net[18] == 2) {

		lstrcpyn(label, "Ethernet*", (int)sizeof(label));

	}

	else if (g_InternetConnectStat_Win10 == 0 && net[18] == 1) {

		lstrcpyn(label, "Ethernet", (int)sizeof(label));

	}

	else if (g_InternetConnectStat_Win10 == 1 || g_InternetConnectStat_Win10 == 4 ||

		active_physical_adapter_Win10 == 1) {

		lstrcpyn(label, activeSSID, (int)sizeof(label));

		if (label[0] == '\0') lstrcpyn(label, "SSID:N/A", (int)sizeof(label));

	}

	else if (g_InternetConnectStat_Win10 == 2 ||

		active_physical_adapter_Win10 == 2) {

		lstrcpyn(label, activeAPName, (int)sizeof(label));

		if (label[0] == '\0') lstrcpyn(label, "APN: N/A", (int)sizeof(label));

	}

	tc_wappend_ansi_fixed_w(dp, remain, label, NetMIX_Length);

}


static BOOL tc_gip_align(const char* src, char* out, int outCch)
{
	unsigned int a, b, c, d;
	if (!src || !out || outCch <= 0) return FALSE;
	out[0] = '\0';
	if (sscanf_s(src, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return FALSE;
	if (a > 255 || b > 255 || c > 255 || d > 255) return FALSE;
	sprintf_s(out, outCch, "%3u.%3u.%3u.%3u", a, b, c, d);
	return TRUE;
}

static void tc_gip_emit(char** dp, char** infop, const char* src)
{
	int len;
	if (!dp || !*dp || !infop || !*infop || !src) return;
	len = (int)strlen(src);
	for (int i = 0; i < len; ++i) {
		*(*dp)++ = src[i];
		*(*infop)++ = 0x01;
	}
}

static BOOL tc_emit_uptime_token_w(WCHAR** dp, int* remain, const WCHAR** psp, ULONGLONG* tickCache)
{
	const WCHAR* p;
	const WCHAR* p2;
	int len = 0;
	int slen = 0;
	BOOL bComma = FALSE;
	int st;

	if (!dp || !*dp || !remain || !psp || !*psp || !tickCache) return FALSE;
	p = *psp;
	if (*p != L'S') return FALSE;
	p++;

	if (*p == L'd' || *p == L'a' || *p == L'h' || *p == L'n' || *p == L's') {
		p2 = p + 1;
		if (!tc_parse_num_format_w(&p2, &len, &slen, &bComma)) return FALSE;
		if (!*tickCache) *tickCache = GetTickCount64();
		if (*p == L'd') {
			st = (int)(*tickCache / 86400000ULL);
			tc_wappend_num_format(dp, remain, st, len, slen, bComma);
		}
		else if (*p == L'a') {
			st = (int)(*tickCache / 3600000ULL);
			tc_wappend_num_format(dp, remain, st, len, slen, bComma);
		}
		else if (*p == L'h') {
			st = (int)((*tickCache / 3600000ULL) % 24ULL);
			tc_wappend_num_format(dp, remain, st, len, slen, FALSE);
		}
		else if (*p == L'n') {
			st = (int)((*tickCache / 60000ULL) % 60ULL);
			tc_wappend_num_format(dp, remain, st, len, slen, FALSE);
		}
		else {
			st = (int)((*tickCache / 1000ULL) % 60ULL);
			tc_wappend_num_format(dp, remain, st, len, slen, FALSE);
		}
		*psp = p2;
		return TRUE;
	}

	if (*p == L'T') {
		ULONGLONG dw;
		int sth;
		int stm;
		int sts;
		if (!*tickCache) *tickCache = GetTickCount64();
		dw = *tickCache / 1000ULL;
		sts = (int)(dw % 60ULL); dw /= 60ULL;
		stm = (int)(dw % 60ULL); dw /= 60ULL;
		sth = (int)dw;
		tc_wappend_num_format(dp, remain, sth, 2, 0, FALSE);
		tc_wappend_char(dp, remain, L':');
		tc_wappend_num_format(dp, remain, stm, 2, 0, FALSE);
		tc_wappend_char(dp, remain, L':');
		tc_wappend_num_format(dp, remain, sts, 2, 0, FALSE);
		*psp = p + 1;
		return TRUE;
	}

	return FALSE;
}

static BOOL tc_emit_gpu_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	int value;
	int len;
	int slen;
	BOOL bComma = FALSE;
	const WCHAR* p2;
	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'G') return FALSE;
	if (*(p + 1) == L'U') value = totalGPUUsage;
	else if (*(p + 1) == L'I') value = numPDHGPUInstance;
	else return FALSE;
	if (value < 0) value = 0;
	p += 2;
	p2 = p;
	if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) {
		tc_wappend_num_format(dp, remain, value, len, slen, bComma);
		p = p2;
	}
	else if (*(p - 1) == L'U') {
		if (value > 99) tc_wappend_uint_fixed(dp, remain, value, 3);
		else tc_wappend_uint_fixed(dp, remain, value, 2);
	}
	else {
		if (value > 999) tc_wappend_uint_fixed(dp, remain, value, 4);
		else if (value > 99) tc_wappend_uint_fixed(dp, remain, value, 3);
		else tc_wappend_uint_fixed(dp, remain, value, 2);
	}
	*psp = p;
	return TRUE;
}

static BOOL tc_emit_ip_token_w(WCHAR** dp, int* remain, const WCHAR** psp)
{
	const WCHAR* p;
	char buf[32];
	if (!dp || !*dp || !remain || !psp || !*psp) return FALSE;
	p = *psp;
	if (*p != L'I' || *(p + 1) != L'P') return FALSE;

	if (*(p + 2) == L'E') {
		tc_wappend_ansi_fixed_w(dp, remain, ipEther, 15);
	}
	else if (*(p + 2) == L'W') {
		tc_wappend_ansi_fixed_w(dp, remain, ipWiFi, 15);
	}
	else if (*(p + 2) == L'L') {
		tc_wappend_ansi_fixed_w(dp, remain, ipLTE, 15);
	}
	else if (*(p + 2) == L'V') {
		tc_wappend_ansi_fixed_w(dp, remain, ipVPN, 15);
	}
	else if (*(p + 2) == L'A') {
		strcpy(buf, "IP[Active] -NA-");
		if (flag_VPN && lstrcmpi(ipVPN, "--- --- --- ---") != 0) {
			strcpy(buf, ipVPN);
		}
		else if (active_physical_adapter_Win10 == 0) {
			strcpy(buf, ipEther);
		}
		else if (active_physical_adapter_Win10 == 1) {
			strcpy(buf, ipWiFi);
		}
		else if (active_physical_adapter_Win10 == 2) {
			strcpy(buf, ipLTE);
		}
		else if (g_InternetConnectStat_Win10 == 0) {
			strcpy(buf, ipEther);
		}
		else if (g_InternetConnectStat_Win10 == 1 || g_InternetConnectStat_Win10 == 4) {
			strcpy(buf, ipWiFi);
		}
		else if (g_InternetConnectStat_Win10 == 2) {
			strcpy(buf, ipLTE);
		}
		tc_wappend_ansi_fixed_w(dp, remain, buf, 15);
	}
	else {
		return FALSE;
	}

	*psp = p + 3;
	return TRUE;
}

static BOOL tc_wfmt_core(WCHAR* s, int sCch, char* s_info, SYSTEMTIME* pt, int beat100, const WCHAR* fmt)
{
	const WCHAR* sp = fmt;
	WCHAR* dp = s;
	int remain = sCch;
	char* ip = s_info;
	int infoRemain = sCch;
	WCHAR sdate[16], stime[16], amStr[32], pmStr[32];
	SYSTEMTIME disptime;
	ULONGLONG tickCount = 0;

	if (!s || sCch <= 0 || !pt || !fmt) return FALSE;
	s[0] = L'\0';
	if (s_info) s_info[0] = '\0';
	disptime = *pt;
	tc_get_locale_sdate_w(sdate, (int)(sizeof(sdate) / sizeof(sdate[0])));
	tc_get_locale_stime_w(stime, (int)(sizeof(stime) / sizeof(stime[0])));
	tc_get_locale_ampm_w(TRUE, amStr, (int)(sizeof(amStr) / sizeof(amStr[0])));
	tc_get_locale_ampm_w(FALSE, pmStr, (int)(sizeof(pmStr) / sizeof(pmStr[0])));

#define TC_MARK(zone, expr) do { WCHAR* __mark = dp; expr; tc_iappend_span(&ip, &infoRemain, __mark, dp, (char)(zone)); } while (0)

	while (*sp) {
		if (*sp == L'<' && *(sp + 1) == L'%') {
			sp += 2;
			while (*sp) {
				if (*sp == L'%' && *(sp + 1) == L'>') { sp += 2; break; }
				if (*sp == L'\"') {
					sp++;
					while (*sp && *sp != L'\"') TC_MARK(0x01, tc_wappend_char(&dp, &remain, *sp++));
					if (*sp == L'\"') sp++;
					continue;
				}
				{
					WCHAR* mark = dp;
					if (tc_custom_emit_if_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; }
				}
				if (*sp == L'/') { TC_MARK(0x02, tc_wappend_text(&dp, &remain, sdate)); sp++; continue; }
				if (*sp == L':') { TC_MARK(0x08, tc_wappend_text(&dp, &remain, stime)); sp++; continue; }
				if (*sp == L'\\' && *(sp + 1) == L'n') { TC_MARK(0x08, tc_wappend_char(&dp, &remain, L'\r'); tc_wappend_char(&dp, &remain, L'\n')); sp += 2; continue; }

				if (*sp == L'@' && *(sp + 1) == L'@' && *(sp + 2) == L'@')
				{
					TC_MARK(0x08,
						tc_wappend_char(&dp, &remain, L'@');
						tc_wappend_char(&dp, &remain, (WCHAR)(L'0' + (beat100 / 10000)));
						tc_wappend_char(&dp, &remain, (WCHAR)(L'0' + ((beat100 % 10000) / 1000)));
						tc_wappend_char(&dp, &remain, (WCHAR)(L'0' + ((beat100 % 1000) / 100))));
					sp += 3;
					if (*sp == L'.' && *(sp + 1) == L'@') {
						TC_MARK(0x08,
							tc_wappend_char(&dp, &remain, L'.');
							tc_wappend_char(&dp, &remain, (WCHAR)(L'0' + ((beat100 % 100) / 10))));
						sp += 2;
					}
					continue;
				}

				if (_wcsnicmp(sp, L"LDATE", 5) == 0) {
					WCHAR buf[128];
					if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), DATE_LONGDATE, &disptime, NULL, buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x02, tc_wappend_text(&dp, &remain, buf));
					sp += 5; continue;
				}
				if (_wcsnicmp(sp, L"DATE", 4) == 0) {
					WCHAR buf[128];
					if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), DATE_SHORTDATE, &disptime, NULL, buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x02, tc_wappend_text(&dp, &remain, buf));
					sp += 4; continue;
				}
				if (_wcsnicmp(sp, L"TIME", 4) == 0) {
					WCHAR buf[128];
					if (GetTimeFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), TIME_FORCE24HOURFORMAT, &disptime, NULL, buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x08, tc_wappend_text(&dp, &remain, buf));
					sp += 4; continue;
				}

				if (*sp == L'C' && (*(sp + 1) == L'U' || *(sp + 1) == L'C')) {
					WCHAR* mark = dp;
					BOOL isClock = (*(sp + 1) == L'C') ? TRUE : FALSE;
					const WCHAR* pnum = sp + 2;
					int value = 0;
					WCHAR clockUnit = L'M';
					double dvalue = 0.0;
					if (!isClock) {
						if (tc_is_digit_ascii_w(*pnum)) {
							int processorNum = (int)(*pnum - L'0');
							value = (processorNum >= nLogicalProcessors) ? 0 : CPUUsage[processorNum];
							pnum += 1;
						}
						else if (*pnum == L'e' && tc_is_digit_ascii_w(*(pnum + 1)) && tc_is_digit_ascii_w(*(pnum + 2))) {
							int processorNum = (int)(*(pnum + 1) - L'0') * 10 + (int)(*(pnum + 2) - L'0');
							value = (processorNum >= nLogicalProcessors) ? 0 : CPUUsage[processorNum];
							pnum += 3;
						}
						else {
							value = (totalCPUUsage >= 0) ? totalCPUUsage : 0;
						}
					}
					else {
						if (tc_is_digit_ascii_w(*pnum)) {
							int processorNum = (int)(*pnum - L'0');
							value = 0;
							if (processorNum < nLogicalProcessors) value = b_EnableClock2 ? CPUClock2[processorNum] : iCPUClock[processorNum];
							pnum += 1;
						}
						else if (*pnum == L'e' && tc_is_digit_ascii_w(*(pnum + 1)) && tc_is_digit_ascii_w(*(pnum + 2))) {
							int processorNum = (int)(*(pnum + 1) - L'0') * 10 + (int)(*(pnum + 2) - L'0');
							value = 0;
							if (processorNum < nLogicalProcessors) value = b_EnableClock2 ? CPUClock2[processorNum] : iCPUClock[processorNum];
							pnum += 3;
						}
						else {
							value = b_EnableClock2 ? CPUClock2Ave : iCPUClock[0];
						}
						if (*pnum == L'M' || *pnum == L'G') {
							clockUnit = *pnum;
							pnum += 1;
						}
					}
					if (value < 0) value = 0;
					if (isClock) {
						dvalue = (double)value;
						if (clockUnit == L'G') dvalue /= 1000.0;
					}
					{
						int len;
						int slen;
						BOOL bComma = FALSE;
						const WCHAR* p2 = pnum;
						if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) {
							if (isClock && clockUnit == L'G') {
								int clockInt = (int)dvalue;
								tc_wappend_num_format(&dp, &remain, clockInt, len, slen, bComma);
								dvalue -= (double)clockInt;
								if (*p2 == L'.') {
									int dlen;
									int dslen;
									BOOL dComma = FALSE;
									const WCHAR* p3 = p2 + 1;
									if (tc_parse_num_format_w(&p3, &dlen, &dslen, &dComma)) {
										int fmtLen = dlen;
										int frac;
										if (fmtLen > 3) fmtLen = 3;
										tc_wappend_char(&dp, &remain, L'.');
										while (fmtLen-- > 0) dvalue *= 10.0;
										frac = (int)dvalue;
										if (frac < 0) frac = 0;
										tc_wappend_num_format(&dp, &remain, frac, dlen > 3 ? 3 : dlen, 0, FALSE);
										p2 = p3;
									}
								}
							}
							else {
								tc_wappend_num_format(&dp, &remain, value, len, slen, bComma);
							}
							pnum = p2;
						}
						else if (!isClock) {
							if (value > 99) tc_wappend_uint_fixed(&dp, &remain, value, 3);
							else tc_wappend_uint_fixed(&dp, &remain, value, 2);
						}
						else if (clockUnit == L'G') {
							tc_wappend_uint_var(&dp, &remain, (int)dvalue);
						}
						else {
							tc_wappend_num_format(&dp, &remain, value, 3, 0, FALSE);
						}
					}
					sp = pnum;
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}

				{ WCHAR* mark = dp; if (tc_emit_memory_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				{ WCHAR* mark = dp; if (tc_emit_network_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				{ WCHAR* mark = dp; if (tc_emit_hdd_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				{ WCHAR* mark = dp; if (tc_emit_diskrate_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				if (*sp == L'G' && *(sp + 1) == L'I' && *(sp + 2) == L'P') {
					WCHAR* mark = dp;
					char gipBuf[TC_GIP_VALUE_MAX];
					char alignedBuf[TC_GIP_VALUE_MAX];
					BOOL isAligned = FALSE;
					tc_gip_lock();
					EnterCriticalSection(&g_gipLock);
					strcpy_s(gipBuf, sizeof(gipBuf), g_gipValueUtf8);
					LeaveCriticalSection(&g_gipLock);
					if (*(sp + 3) != L'A' && tc_gip_align(gipBuf, alignedBuf, (int)sizeof(alignedBuf))) {
						isAligned = TRUE;
					}
					if (isAligned) {
						tc_wappend_ansi_fixed_w(&dp, &remain, alignedBuf, 15);
					}
					else {
						tc_wappend_ascii(&dp, &remain, gipBuf);
					}
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					sp += (*(sp + 3) == L'A') ? 4 : 3;
					continue;
				}
				{ WCHAR* mark = dp; if (tc_emit_gpu_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				{ WCHAR* mark = dp; if (tc_emit_ip_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
				{ WCHAR* mark = dp; if (tc_emit_uptime_token_w(&dp, &remain, &sp, &tickCount)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }

				if (*sp == L'A' && *(sp + 1) == L'D') {
					TC_MARK(0x01, if (pw_mode == 0) tc_wappend_text(&dp, &remain, L"DC"); else if (pw_mode == 1) tc_wappend_text(&dp, &remain, L"AC"); else tc_wappend_text(&dp, &remain, L"UN"));
					sp += 2;
					continue;
				}
				if (*sp == L'a' && *(sp + 1) == L'd') {
					TC_MARK(0x01, if (pw_mode == 0) tc_wappend_char(&dp, &remain, L'D'); else if (pw_mode == 1) tc_wappend_char(&dp, &remain, L'A'); else tc_wappend_char(&dp, &remain, L'U'));
					sp += 2;
					continue;
				}
				if (*sp == L'B' && *(sp + 1) == L'C' && *(sp + 2) == L'S') {
					TC_MARK(0x01, tc_wappend_char(&dp, &remain, b_Charging ? L'*' : L' '));
					sp += 3;
					continue;
				}
				if (*sp == L'B' && *(sp + 1) == L'L') {
					WCHAR* mark = dp;
					sp += 2;
					if (iBatteryLife <= 100) {
						int len = 0;
						int slen = 0;
						BOOL bComma = FALSE;
						const WCHAR* p2 = sp;
						if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) {
							tc_wappend_num_format(&dp, &remain, iBatteryLife, len, slen, bComma);
							sp = p2;
						}
						else {
							if (iBatteryLife > 99) tc_wappend_uint_fixed(&dp, &remain, iBatteryLife, 3);
							else tc_wappend_uint_fixed(&dp, &remain, iBatteryLife, 2);
						}
					}
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}
				if (*sp == L'B' && (*(sp + 1) == L'h' || *(sp + 1) == L'n' || *(sp + 1) == L's' || *(sp + 1) == L'_')) {
					WCHAR* mark = dp;
					int len = 0;
					int slen = 0;
					BOOL bComma = FALSE;
					const WCHAR* p2;
					sp++;
					p2 = sp;
					if (tc_parse_num_format_w_tok(&p2, L'h', L',', &len, &slen, &bComma)) {
						tc_wappend_num_format(&dp, &remain, blt_h, len, slen, bComma);
						sp = p2;
					}
					p2 = sp;
					if (tc_parse_num_format_w_tok(&p2, L'n', L',', &len, &slen, &bComma)) {
						tc_wappend_num_format(&dp, &remain, blt_m, len, slen, bComma);
						sp = p2;
					}
					p2 = sp;
					if (tc_parse_num_format_w_tok(&p2, L's', L',', &len, &slen, &bComma)) {
						tc_wappend_num_format(&dp, &remain, blt_s, len, slen, bComma);
						sp = p2;
					}
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}
				if (*sp == L'T' && *(sp + 1) == L'E' && *(sp + 2) == L'M' && *(sp + 3) == L'P') {
					WCHAR* mark = dp;
					int len = 0;
					int slen = 0;
					BOOL bComma = FALSE;
					const WCHAR* p2;
					sp += 4;
					if (!b_TempAvailable) {
						tc_wappend_text(&dp, &remain, L"NA");
					}
					else {
						p2 = sp;
						if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) {
							tc_wappend_num_format(&dp, &remain, pdhTemperature, len, slen, bComma);
							sp = p2;
						}
						else {
							if (pdhTemperature > 99) tc_wappend_uint_fixed(&dp, &remain, pdhTemperature, 3);
							else tc_wappend_uint_fixed(&dp, &remain, pdhTemperature, 2);
						}
					}
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}
				if (*sp == L'V' && *(sp + 1) == L'L') {
					WCHAR* mark = dp;
					int len = 0;
					int slen = 0;
					BOOL bComma = FALSE;
					const WCHAR* p2;
					sp += 2;
					p2 = sp;
					if (tc_parse_num_format_w(&p2, &len, &slen, &bComma)) {
						tc_wappend_num_format(&dp, &remain, iVolume, len, slen, FALSE);
						sp = p2;
					}
					else {
						tc_wappend_num_format(&dp, &remain, iVolume, 3, 2, FALSE);
					}
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}
				if (*sp == L'V' && *(sp + 1) == L'M') {
					WCHAR* mark = dp;
					WCHAR muteW[128];
					int muteLen;
					sp += 2;
					if (tc_ansi_to_utf16_compat((UINT)codepage, strMute, muteW, (int)(sizeof(muteW) / sizeof(muteW[0]))) <= 0) {
						muteW[0] = L'\0';
					}
					muteLen = (int)lstrlenW(muteW);
					if (muteStatus) tc_wappend_text(&dp, &remain, muteW);
					else while (muteLen-- > 0) tc_wappend_char(&dp, &remain, L' ');
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01);
					continue;
				}

				if (_wcsnicmp(sp, L"PCORE", 5) == 0) {
					extern int nCores; TC_MARK(0x01, { int cores = (nCores < 0) ? 0 : nCores; tc_wappend_uint_var(&dp, &remain, cores); });
					sp += 5; continue;
				}
				if (_wcsnicmp(sp, L"LPROC", 5) == 0) {
					extern int nLogicalProcessors; TC_MARK(0x01, { int lproc = (nLogicalProcessors < 0) ? 0 : nLogicalProcessors; tc_wappend_uint_var(&dp, &remain, lproc); });
					sp += 5; continue;
				}

				if (*sp == L't' && ((*(sp + 1) == L'd') || (*(sp + 1) == L'u') || (*(sp + 1) == L'e')) &&
					((*(sp + 2) == L'+') || (*(sp + 2) == L'-')) &&
					((*(sp + 3) >= L'0') && (*(sp + 3) <= L'2')) &&
					((*(sp + 4) >= L'0') && (*(sp + 4) <= L'9')) &&
					(*(sp + 5) == L':') &&
					((*(sp + 6) >= L'0') && (*(sp + 6) <= L'5')) &&
					((*(sp + 7) >= L'0') && (*(sp + 7) <= L'9')))
				{
					int td_hour = 0;
					int td_min = 0;
					BOOL td_neg = FALSE;
					if (*(sp + 2) == L'-') td_neg = TRUE;
					td_hour = (int)(*(sp + 3) - L'0') * 10 + (int)(*(sp + 4) - L'0');
					td_min = (int)(*(sp + 6) - L'0') * 10 + (int)(*(sp + 7) - L'0');
					if (td_hour > 23) { td_hour = 0; td_min = 0; }
					if (*(sp + 1) == L'd') {
						disptime = CalcTimeDifference_Win10(pt, td_hour, td_min, td_neg);
					}
					else if (*(sp + 1) == L'u') {
						if (disptime.wYear > 2023) {
							disptime = CalcTimeDifference_Win10(pt, td_hour - 1, td_min, td_neg);
						}
						else {
							disptime = CalcTimeDifference_US_Win10(pt, td_hour, td_min, td_neg);
						}
					}
					else {
						disptime = CalcTimeDifference_Europe_Win10(pt, td_hour, td_min, td_neg);
					}
					sp += 8;
					continue;
				}

				if (*sp == L'S' && *(sp + 1) == L't' && ((*(sp + 2) == L'U') || (*(sp + 2) == L'E')))
				{
					if (*(sp + 2) == L'U') {
						TC_MARK(0x08, tc_wappend_char(&dp, &remain, b_SummerTime_US ? L'*' : L' '));
					}
					else {
						TC_MARK(0x08, tc_wappend_char(&dp, &remain, b_SummerTime_Europe ? L'*' : L' '));
					}
					sp += 3;
					continue;
				}

				if (*sp == L'w' && ((*(sp + 1) == L'+') || (*(sp + 1) == L'-')) &&
					((*(sp + 2) >= L'0') && (*(sp + 2) <= L'9')) &&
					((*(sp + 3) >= L'0') && (*(sp + 3) <= L'9')))
				{
					int xdiff = (int)(*(sp + 2) - L'0') * 10 + (int)(*(sp + 3) - L'0');
					int hour = 0;
					if (*(sp + 1) == L'-') xdiff = -xdiff;
					hour = ((int)disptime.wHour + xdiff) % 24;
					if (hour < 0) hour += 24;
					hour = tc_hour_adjust_w(hour);
					TC_MARK(0x08, tc_wappend_uint_fixed(&dp, &remain, hour, 2));
					sp += 4;
					continue;
				}

				if (*sp == L'a' && *(sp + 1) == L'a' && *(sp + 2) == L'a')
				{
					WCHAR buf[64];
					if (*(sp + 3) == L'a') {
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"dddd", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x04, tc_wappend_text(&dp, &remain, buf));
						sp += 4;
					}
					else {
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"ddd", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x04, tc_wappend_text(&dp, &remain, buf));
						sp += 3;
					}
					continue;
				}

				if (*sp == L'y' && *(sp + 1) == L'y') {
					TC_MARK(0x02, if (*(sp + 2) == L'y' && *(sp + 3) == L'y') tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wYear, 4); else tc_wappend_uint_fixed(&dp, &remain, (int)(disptime.wYear % 100), 2));
					sp += (*(sp + 2) == L'y' && *(sp + 3) == L'y') ? 4 : 2;
					continue;
				}

				if (*sp == L'Y' && AltYear > -1)
				{
					int n = 1;
					while (*sp == L'Y') { n *= 10; sp++; }
					if (n < AltYear) {
						n = 1;
						while (n < AltYear) n *= 10;
					}
					TC_MARK(0x02, for (;;) { tc_wappend_char(&dp, &remain, (WCHAR)(L'0' + ((AltYear % n) / (n / 10)))); if (n == 10) break; n /= 10; });
					continue;
				}
				if (*sp == L'g')
				{
					WCHAR* mark = dp;
					WCHAR eraW[32];
					const WCHAR* pEra;
					if (tc_ansi_to_utf16_compat((UINT)codepage, EraStr, eraW, (int)(sizeof(eraW) / sizeof(eraW[0]))) <= 0) {
						eraW[0] = L'\0';
					}
					pEra = eraW;
					while (*pEra && *sp == L'g') {
						tc_wappend_char(&dp, &remain, *pEra++);
						sp++;
					}
					while (*sp == L'g') sp++;
					tc_iappend_span(&ip, &infoRemain, mark, dp, 0x08);
					continue;
				}

				if (*sp == L'd') {
					if (_wcsnicmp(sp, L"dddd", 4) == 0) {
						WCHAR buf[64];
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"dddd", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x04, tc_wappend_text(&dp, &remain, buf));
						sp += 4; continue;
					}
					if (_wcsnicmp(sp, L"dde", 3) == 0) {
						TC_MARK(0x04, tc_wappend_ascii(&dp, &remain, DayOfWeekEng[disptime.wDayOfWeek]));
						sp += 3; continue;
					}
					if (_wcsnicmp(sp, L"ddd", 3) == 0) {
						WCHAR buf[64];
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"ddd", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x04, tc_wappend_text(&dp, &remain, buf));
						sp += 3; continue;
					}
					TC_MARK(0x02, if (*(sp + 1) == L'd') { tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wDay, 2); sp += 2; } else { if (disptime.wDay > 9) tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wDay, 2); else tc_wappend_uint_var(&dp, &remain, (int)disptime.wDay); sp++; });
					continue;
				}

				if (*sp == L'm') {
					if (_wcsnicmp(sp, L"mmmm", 4) == 0) {
						WCHAR buf[64];
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"MMMM", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x02, tc_wappend_text(&dp, &remain, buf));
						sp += 4; continue;
					}
					if (_wcsnicmp(sp, L"mme", 3) == 0) {
						TC_MARK(0x02, tc_wappend_ascii(&dp, &remain, MonthEng[disptime.wMonth - 1]));
						sp += 3; continue;
					}
					if (_wcsnicmp(sp, L"mmm", 3) == 0) {
						WCHAR buf[64];
						if (GetDateFormatW(MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 0, &disptime, L"MMM", buf, (int)(sizeof(buf) / sizeof(buf[0]))) > 0) TC_MARK(0x02, tc_wappend_text(&dp, &remain, buf));
						sp += 3; continue;
					}
					TC_MARK(0x02, if (*(sp + 1) == L'm') { tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wMonth, 2); sp += 2; } else { if (disptime.wMonth > 9) tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wMonth, 2); else tc_wappend_uint_var(&dp, &remain, (int)disptime.wMonth); sp++; });
					continue;
				}

				if (*sp == L'h') {
					int hour = tc_hour_adjust_w((int)disptime.wHour);
					TC_MARK(0x08, if (*(sp + 1) == L'h') { tc_wappend_uint_fixed(&dp, &remain, hour, 2); sp += 2; } else { if (hour > 9) tc_wappend_uint_fixed(&dp, &remain, hour, 2); else tc_wappend_uint_var(&dp, &remain, hour); sp++; });
					continue;
				}

				if (*sp == L'n') {
					TC_MARK(0x08, if (*(sp + 1) == L'n') { tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wMinute, 2); sp += 2; } else { if (disptime.wMinute > 9) tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wMinute, 2); else tc_wappend_uint_var(&dp, &remain, (int)disptime.wMinute); sp++; });
					continue;
				}

				if (*sp == L's') {
					TC_MARK(0x08, if (*(sp + 1) == L's') { tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wSecond, 2); sp += 2; } else { if (disptime.wSecond > 9) tc_wappend_uint_fixed(&dp, &remain, (int)disptime.wSecond, 2); else tc_wappend_uint_var(&dp, &remain, (int)disptime.wSecond); sp++; });
					continue;
				}

				if (*sp == L't' && *(sp + 1) == L't') {
					TC_MARK(0x08, tc_wappend_text(&dp, &remain, (disptime.wHour < 12) ? amStr : pmStr));
					sp += 2; continue;
				}

				if (_wcsnicmp(sp, L"AM/PM", 5) == 0) {
					TC_MARK(0x08, tc_wappend_char(&dp, &remain, (disptime.wHour < 12) ? L'A' : L'P'); tc_wappend_char(&dp, &remain, L'M'));
					sp += 5; continue;
				}
				if (_wcsnicmp(sp, L"AMPM", 4) == 0) {
					TC_MARK(0x08, tc_wappend_text(&dp, &remain, (disptime.wHour < 12) ? amStr : pmStr));
					sp += 4; continue;
				}
				if (_wcsnicmp(sp, L"am/pm", 5) == 0) {
					TC_MARK(0x08, tc_wappend_char(&dp, &remain, (disptime.wHour < 12) ? L'a' : L'p'); tc_wappend_char(&dp, &remain, L'm'));
					sp += 5; continue;
				}

				if (tc_is_alpha_ascii_w(*sp)) {
					/* Keep unknown ASCII token chars as literals to avoid unnecessary ANSI fallback. */
					TC_MARK(0x01, tc_wappend_char(&dp, &remain, *sp++));
					continue;
				}
				TC_MARK(0x01, tc_wappend_char(&dp, &remain, *sp++));
			}
		}
		else {
			{ WCHAR* mark = dp; if (tc_custom_emit_if_token_w(&dp, &remain, &sp)) { tc_iappend_span(&ip, &infoRemain, mark, dp, 0x01); continue; } }
			TC_MARK(0x01, tc_wappend_char(&dp, &remain, *sp++));
		}
	}
#undef TC_MARK
	return TRUE;
}

void MakeFormatW(WCHAR* s, int sCch, char* s_info, SYSTEMTIME* pt, int beat100, const WCHAR* fmt)
{
	static const WCHAR kSafePrefix[] = L"[SafeMode] ";
	int prefixLen;
	int textLen;
	int i;

	if (!s || sCch <= 0) return;
	s[0] = L'\0';
	if (!fmt || !pt) return;
	if (!tc_wfmt_core(s, sCch, s_info, pt, beat100, fmt)) {
		s[0] = L'\0';
		if (s_info && sCch > 0) s_info[0] = '\0';
		return;
	}
	if (!b_SafeMode) return;
	prefixLen = (int)(sizeof(kSafePrefix) / sizeof(kSafePrefix[0])) - 1;
	if (_wcsnicmp(s, kSafePrefix, prefixLen) == 0) return;
	textLen = lstrlenW(s);
	if (textLen + prefixLen >= sCch) {
		textLen = sCch - prefixLen - 1;
		if (textLen < 0) textLen = 0;
	}
	for (i = textLen; i >= 0; --i) {
		s[i + prefixLen] = s[i];
	}
	for (i = 0; i < prefixLen; ++i) {
		s[i] = kSafePrefix[i];
	}
	if (s_info && sCch > 0) {
		textLen = lstrlen(s_info);
		if (textLen + prefixLen >= sCch) {
			textLen = sCch - prefixLen - 1;
			if (textLen < 0) textLen = 0;
		}
		for (i = textLen; i >= 0; --i) {
			s_info[i + prefixLen] = s_info[i];
		}
		for (i = 0; i < prefixLen; ++i) {
			s_info[i] = 0x01;
		}
	}
}


/*------------------------------------------------
  check format
--------------------------------------------------*/
DWORD FindFormat(char* fmt)
{
	char *sp;
	DWORD ret = 0;

	sp = fmt;
	while(*sp)
	{
		if(*sp == '<' && *(sp + 1) == '%')
		{
			sp += 2;
			while(*sp)
			{
				if(*sp == '%' && *(sp + 1) == '>')
				{
					sp += 2;
					break;
				}
				if(*sp == '\"')
				{
					sp++;
					while(*sp != '\"' && *sp) sp++;
					if(*sp == '\"') sp++;
				}
				else if(*sp == 's')
				{
					sp++;
					ret |= FORMAT_SECOND;
				}
				else if (*sp == '@' && *(sp + 1) == '@' && *(sp + 2) == '@')
				{
					sp += 3;
					if(*sp == '.' && *(sp + 1) == '@')
					{
						ret |= FORMAT_BEAT2;
						sp += 2;
					}
					else
						ret |= FORMAT_BEAT1;
				}
				//else if(*sp == 'R' &&
				//	(*(sp + 1) == 'S' || *(sp + 1) == 'G' || *(sp + 1) == 'U') )
				//{
				//	sp += 2;
				//	ret |= FORMAT_SYSINFO;
				//}
				else if(*sp == 'C' && *(sp + 1) == 'U' &&
					(isdigit(*(sp + 2)) && *(sp + 2) != '8' && *(sp + 2) != '9') )
				{
					sp += 3;
					//ret |= FORMAT_PERMON;
					ret |= FORMAT_CPU;
				}
				else if(*sp == 'C' && *(sp + 1) == 'U')
				{
					sp += 2;
					//ret |= FORMAT_SYSINFO;
					//ret |= FORMAT_PERMON;
					ret |= FORMAT_CPU;
				}
				else if(*sp == 'C' && *(sp + 1) == 'C')
				{
					sp += 2;
					//ret |= FORMAT_PERMON;
					ret |= FORMAT_CPU;
				}
				else if(*sp == 'B' && *(sp + 1) == 'L')
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if(*sp == 'B' && (*(sp + 1) == 'h' || *(sp + 1) == 'n' || *(sp + 1) == 's' || *(sp + 1) == '_'))
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if(*sp == 'A' && *(sp + 1) == 'D' )
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if(*sp == 'a' && *(sp + 1) == 'd' )
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if(*sp == 'M' && (*(sp + 1) == 'K' || *(sp + 1) == 'M'))
				{
					sp += 2;
					ret |= FORMAT_MEMORY;
				}
				else if(*sp == 'M' &&
					(*(sp + 1) == 'T' || *(sp + 1) == 'A' || *(sp + 1) == 'U') &&
					(*(sp + 2) == 'P' || *(sp + 2) == 'F' || *(sp + 2) == 'V') &&
					(*(sp + 3) == 'K' || *(sp + 3) == 'M' || *(sp + 3) == 'P' || *(sp + 3) == 'G'))
				{
					sp += 4;
					ret |= FORMAT_MEMORY;
				}
				//else if(*sp == 'B' && *(sp + 1) == 'T')
				//{
				//	sp += 2;
				//	ret |= FORMAT_MOTHERBRD;
				//}
				//else if(*sp == 'B' && *(sp + 1) == 'V')
				//{
				//	sp += 2;
				//	ret |= FORMAT_MOTHERBRD;
				//}
				//else if(*sp == 'B' && *(sp + 1) == 'F')
				//{
				//	sp += 2;
				//	ret |= FORMAT_MOTHERBRD;
				//}
				else if(*sp == 'N' &&
					(*(sp + 1) == 'R' || *(sp + 1) == 'S') &&
					(*(sp + 2) == 'S' || *(sp + 2) == 'A') &&
					(*(sp + 3) == 'M' || *(sp + 3) == 'K' || *(sp + 3) == 'B' || *(sp + 3) == 'G' || *(sp + 3) == 'A'))
				{
					sp += 4;
					ret |= FORMAT_NET;
				}
				else if(*sp == 'H' && (*(sp + 1) == 'R' || *(sp + 1) == 'W' || *(sp + 1) == 'D') && (*(sp + 2) >= 'A' && *(sp + 2) <= 'Z') && (*(sp + 3) == 'B' || *(sp + 3) == 'K' || *(sp + 3) == 'M' || *(sp + 3) == 'A'))
				{
					int dv;
					dv = *(sp + 2) - 'A';
					actdvl[dv] = 1;
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if(*sp == 'H' && (*(sp + 1) == 'A' || *(sp + 1) == 'U' || *(sp + 1) == 'T') && (*(sp + 2) >= 'A' && *(sp + 2) <= 'Z') && (*(sp + 3) == 'M' || *(sp + 3) == 'G' || *(sp + 3) == 'P'))
				{
					int dv;
					dv = *(sp + 2) - 'A';
					actdvl[dv] = 1;
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if (*sp == 'H' && (*(sp + 1) == 'A' || *(sp + 1) == 'U' || *(sp + 1) == 'T') && (*(sp + 2) >= '0' && *(sp + 2) <= '9') && (*(sp + 3) == 'M' || *(sp + 3) == 'G' || *(sp + 3) == 'P'))
				{
					int dv;
					extern char strAdditionalMountPath;
					dv = *(sp + 2) - '0';
					if (strlen(&strAdditionalMountPath + 64 * dv) > 0) {
						actdvl[dv + 26] = 1;
					}
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if(*sp == 'V' && *(sp + 1) == 'L')
				{
					sp += 2;
					ret |= FORMAT_VOL;
				}
				else if (*sp == 'V' && *(sp + 1) == 'M')
				{
					sp += 2;
					ret |= FORMAT_VOL;
				}
				else if (*sp == 'G' && (*(sp + 1) == 'U' || *(sp + 1) == 'I'))
				{
					sp += 2;
					ret |= FORMAT_GPU;
				}
				else if (*sp == 'T' && (*(sp + 1) == 'E') && (*(sp + 2) == 'M') && (*(sp + 3) == 'P')) {
					sp += 4;
					ret |= FORMAT_TEMP;
				}
				else sp = CharNext(sp);
			}
		}
		else sp = CharNext(sp);
	}
	return ret;
}

DWORD FindFormatW(const WCHAR* fmt)
{
	const WCHAR* sp;
	DWORD ret = 0;

	if (!fmt) return 0;

	sp = fmt;
	while (*sp)
	{
		if (*sp == L'<' && *(sp + 1) == L'%')
		{
			sp += 2;
			while (*sp)
			{
				if (*sp == L'%' && *(sp + 1) == L'>')
				{
					sp += 2;
					break;
				}
				if (*sp == L'\"')
				{
					sp++;
					while (*sp != L'\"' && *sp) sp++;
					if (*sp == L'\"') sp++;
				}
				else if (*sp == L's')
				{
					sp++;
					ret |= FORMAT_SECOND;
				}
				else if (*sp == L'@' && *(sp + 1) == L'@' && *(sp + 2) == L'@')
				{
					sp += 3;
					if (*sp == L'.' && *(sp + 1) == L'@')
					{
						ret |= FORMAT_BEAT2;
						sp += 2;
					}
					else
					{
						ret |= FORMAT_BEAT1;
					}
				}
				else if (*sp == L'C' && *(sp + 1) == L'U' &&
					(*(sp + 2) >= L'0' && *(sp + 2) <= L'7'))
				{
					sp += 3;
					ret |= FORMAT_CPU;
				}
				else if (*sp == L'C' && *(sp + 1) == L'U')
				{
					sp += 2;
					ret |= FORMAT_CPU;
				}
				else if (*sp == L'C' && *(sp + 1) == L'C')
				{
					sp += 2;
					ret |= FORMAT_CPU;
				}
				else if (*sp == L'B' && *(sp + 1) == L'L')
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if (*sp == L'B' && (*(sp + 1) == L'h' || *(sp + 1) == L'n' || *(sp + 1) == L's' || *(sp + 1) == L'_'))
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if (*sp == L'A' && *(sp + 1) == L'D')
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if (*sp == L'a' && *(sp + 1) == L'd')
				{
					sp += 2;
					ret |= FORMAT_BATTERY;
				}
				else if (*sp == L'M' && (*(sp + 1) == L'K' || *(sp + 1) == L'M'))
				{
					sp += 2;
					ret |= FORMAT_MEMORY;
				}
				else if (*sp == L'M' &&
					(*(sp + 1) == L'T' || *(sp + 1) == L'A' || *(sp + 1) == L'U') &&
					(*(sp + 2) == L'P' || *(sp + 2) == L'F' || *(sp + 2) == L'V') &&
					(*(sp + 3) == L'K' || *(sp + 3) == L'M' || *(sp + 3) == L'P' || *(sp + 3) == L'G'))
				{
					sp += 4;
					ret |= FORMAT_MEMORY;
				}
				else if (*sp == L'N' &&
					(*(sp + 1) == L'R' || *(sp + 1) == L'S') &&
					(*(sp + 2) == L'S' || *(sp + 2) == L'A') &&
					(*(sp + 3) == L'M' || *(sp + 3) == L'K' || *(sp + 3) == L'B' || *(sp + 3) == L'G' || *(sp + 3) == L'A'))
				{
					sp += 4;
					ret |= FORMAT_NET;
				}
				else if (*sp == L'H' && (*(sp + 1) == L'R' || *(sp + 1) == L'W' || *(sp + 1) == L'D') && (*(sp + 2) >= L'A' && *(sp + 2) <= L'Z') && (*(sp + 3) == L'B' || *(sp + 3) == L'K' || *(sp + 3) == L'M' || *(sp + 3) == L'A'))
				{
					int dv;
					dv = (int)(*(sp + 2) - L'A');
					actdvl[dv] = 1;
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if (*sp == L'H' && (*(sp + 1) == L'A' || *(sp + 1) == L'U' || *(sp + 1) == L'T') && (*(sp + 2) >= L'A' && *(sp + 2) <= L'Z') && (*(sp + 3) == L'M' || *(sp + 3) == L'G' || *(sp + 3) == L'T' || *(sp + 3) == L'P'))
				{
					int dv;
					dv = (int)(*(sp + 2) - L'A');
					actdvl[dv] = 1;
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if (*sp == L'H' && (*(sp + 1) == L'A' || *(sp + 1) == L'U' || *(sp + 1) == L'T') && (*(sp + 2) >= L'0' && *(sp + 2) <= L'9') && (*(sp + 3) == L'M' || *(sp + 3) == L'G' || *(sp + 3) == L'T' || *(sp + 3) == L'P'))
				{
					int dv;
					extern char strAdditionalMountPath;
					dv = (int)(*(sp + 2) - L'0');
					if (strlen(&strAdditionalMountPath + 64 * dv) > 0) {
						actdvl[dv + 26] = 1;
					}
					sp += 4;
					ret |= FORMAT_HDD;
				}
				else if (*sp == L'V' && *(sp + 1) == L'L')
				{
					sp += 2;
					ret |= FORMAT_VOL;
				}
				else if (*sp == L'V' && *(sp + 1) == L'M')
				{
					sp += 2;
					ret |= FORMAT_VOL;
				}
				else if (*sp == L'G' && (*(sp + 1) == L'U' || *(sp + 1) == L'I'))
				{
					sp += 2;
					ret |= FORMAT_GPU;
				}
				else if (*sp == L'T' && (*(sp + 1) == L'E') && (*(sp + 2) == L'M') && (*(sp + 3) == L'P'))
				{
					sp += 4;
					ret |= FORMAT_TEMP;
				}
				else
				{
					sp++;
				}
			}
		}
		else
		{
			sp++;
		}
	}
	return ret;
}


SYSTEMTIME CalcTimeDifference_Win10(SYSTEMTIME* pt, int td_h, int td_m, BOOL pol_neg)
{
	SYSTEMTIME systemtime_temp;
	FILETIME filetime_temp;
	systemtime_temp = *pt;
	ULARGE_INTEGER ularge_integer_temp;

	SystemTimeToFileTime(&systemtime_temp, &filetime_temp);

	ularge_integer_temp.HighPart = filetime_temp.dwHighDateTime;
	ularge_integer_temp.LowPart = filetime_temp.dwLowDateTime;

	if (pol_neg)
	{
		ularge_integer_temp.QuadPart -= (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}
	else
	{
		ularge_integer_temp.QuadPart += (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}



	filetime_temp.dwHighDateTime = ularge_integer_temp.HighPart;
	filetime_temp.dwLowDateTime = ularge_integer_temp.LowPart;

	FileTimeToSystemTime(&filetime_temp, &systemtime_temp);

	b_FlagPrevDay = FALSE;
	b_FlagNextDay = FALSE;
	b_FlagPrevMonth = FALSE;
	b_FlagNextMonth = FALSE;

	if ((systemtime_temp.wDay == pt->wDay + 1) || ((systemtime_temp.wDay + 1) < pt->wDay))
	{
		b_FlagNextDay = TRUE;
		if ((systemtime_temp.wDay + 1) < pt->wDay) b_FlagNextMonth = TRUE;
	}
	else if ((systemtime_temp.wDay + 1 == pt->wDay) || (systemtime_temp.wDay > (pt->wDay + 1))) 
	{
		b_FlagPrevDay = TRUE;
		if (systemtime_temp.wDay > (pt->wDay + 1)) b_FlagPrevMonth = TRUE;
	}

	return(systemtime_temp);
}














SYSTEMTIME CalcTimeDifference_US_Win10(SYSTEMTIME* pt, int td_h, int td_m, BOOL pol_neg)
{
	SYSTEMTIME systemtime_temp;
	FILETIME filetime_temp;
	systemtime_temp = *pt;
	ULARGE_INTEGER ularge_integer_temp;
	int i = 0;

	SystemTimeToFileTime(&systemtime_temp, &filetime_temp);

	ularge_integer_temp.HighPart = filetime_temp.dwHighDateTime;
	ularge_integer_temp.LowPart = filetime_temp.dwLowDateTime;

	if (pol_neg)
	{
		ularge_integer_temp.QuadPart -= (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}
	else
	{
		ularge_integer_temp.QuadPart += (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}


	filetime_temp.dwHighDateTime = ularge_integer_temp.HighPart;
	filetime_temp.dwLowDateTime = ularge_integer_temp.LowPart;

	FileTimeToSystemTime(&filetime_temp, &systemtime_temp);

	i = systemtime_temp.wDayOfWeek;
	if (i == 0) i = 7;


	if (((systemtime_temp.wMonth > 3) && (systemtime_temp.wMonth < 11))
		|| ((systemtime_temp.wMonth == 3) && ((systemtime_temp.wDay - i) > 7))
		|| ((systemtime_temp.wMonth == 3) && (systemtime_temp.wDay > 7) && (systemtime_temp.wDay <= 14) && (systemtime_temp.wDayOfWeek == 0) && (systemtime_temp.wHour >= 2))
		|| ((systemtime_temp.wMonth == 11) && (systemtime_temp.wDay <= systemtime_temp.wDayOfWeek))
		|| ((systemtime_temp.wMonth == 11) && (systemtime_temp.wDay <= 7) && (systemtime_temp.wDayOfWeek == 0) && (systemtime_temp.wHour == 0))
		) {
		b_SummerTime_US = TRUE;
	}
	else {
		b_SummerTime_US = FALSE;
	}

	if (b_SummerTime_US) {
		ularge_integer_temp.QuadPart += 36000000000;
		filetime_temp.dwHighDateTime = ularge_integer_temp.HighPart;
		filetime_temp.dwLowDateTime = ularge_integer_temp.LowPart;
		FileTimeToSystemTime(&filetime_temp, &systemtime_temp);
	}

	b_FlagPrevDay = FALSE;
	b_FlagNextDay = FALSE;
	b_FlagPrevMonth = FALSE;
	b_FlagNextMonth = FALSE;

	if ((systemtime_temp.wDay == pt->wDay + 1) || ((systemtime_temp.wDay + 1) < pt->wDay))
	{
		b_FlagNextDay = TRUE;
		if ((systemtime_temp.wDay + 1) < pt->wDay) b_FlagNextMonth = TRUE;
	}
	else if ((systemtime_temp.wDay + 1 == pt->wDay) || (systemtime_temp.wDay > (pt->wDay + 1)))
	{
		b_FlagPrevDay = TRUE;
		if (systemtime_temp.wDay > (pt->wDay + 1)) b_FlagPrevMonth = TRUE;
	}

	return(systemtime_temp);
}



//This function is not called in 2022 or later.
SYSTEMTIME CalcTimeDifference_Europe_Win10(SYSTEMTIME* pt, int td_h, int td_m, BOOL pol_neg)
{
	SYSTEMTIME systemtime_temp, systemtime_utc;
	FILETIME filetime_temp, filetime_utc;
	ULARGE_INTEGER ularge_integer_temp;
	int i = 0;

	extern int currentTimeZoneBiasMin;

	systemtime_temp = *pt;

	SystemTimeToFileTime(&systemtime_temp, &filetime_temp);

	ularge_integer_temp.HighPart = filetime_temp.dwHighDateTime;
	ularge_integer_temp.LowPart = filetime_temp.dwLowDateTime;

	ularge_integer_temp.QuadPart += (LONGLONG)currentTimeZoneBiasMin * 600000000;		//UTC

	filetime_utc.dwHighDateTime = ularge_integer_temp.HighPart;
	filetime_utc.dwLowDateTime = ularge_integer_temp.LowPart;

	FileTimeToSystemTime(&filetime_utc, &systemtime_utc);

	i = systemtime_utc.wDayOfWeek;
	if (i == 0) i = 7;



	if (((systemtime_utc.wMonth > 3) && (systemtime_utc.wMonth < 10))
		|| ((systemtime_utc.wMonth == 3) && ((systemtime_utc.wDay + 7 - i) > 31))
		|| ((systemtime_utc.wMonth == 3) && (systemtime_utc.wDay >= 24) && (i == 7) && (systemtime_utc.wHour >= 1))
		|| ((systemtime_utc.wMonth == 10) && ((systemtime_utc.wDay + 7 - systemtime_utc.wDayOfWeek) <= 31))
		|| ((systemtime_utc.wMonth == 10) && (systemtime_utc.wDay >= 24) && (i == 7) && (systemtime_utc.wHour == 0))
		) {
		b_SummerTime_Europe = TRUE;
	}
	else {
		b_SummerTime_Europe = FALSE;
	}


	ularge_integer_temp.HighPart = filetime_temp.dwHighDateTime;
	ularge_integer_temp.LowPart = filetime_temp.dwLowDateTime;

	if (pol_neg)
	{
		ularge_integer_temp.QuadPart -= (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}
	else
	{
		ularge_integer_temp.QuadPart += (ULONGLONG)(td_h * 60 + td_m) * 600000000;
	}

	if (b_SummerTime_Europe) {
		ularge_integer_temp.QuadPart += 36000000000;
	}

	filetime_temp.dwHighDateTime = ularge_integer_temp.HighPart;
	filetime_temp.dwLowDateTime = ularge_integer_temp.LowPart;

	FileTimeToSystemTime(&filetime_temp, &systemtime_temp);


	b_FlagPrevDay = FALSE;
	b_FlagNextDay = FALSE;
	b_FlagPrevMonth = FALSE;
	b_FlagNextMonth = FALSE;

	if ((systemtime_temp.wDay == pt->wDay + 1) || ((systemtime_temp.wDay + 1) < pt->wDay))
	{
		b_FlagNextDay = TRUE;
		if ((systemtime_temp.wDay + 1) < pt->wDay) b_FlagNextMonth = TRUE;
	}
	else if ((systemtime_temp.wDay + 1 == pt->wDay) || (systemtime_temp.wDay > (pt->wDay + 1)))
	{
		b_FlagPrevDay = TRUE;
		if (systemtime_temp.wDay > (pt->wDay + 1)) b_FlagPrevMonth = TRUE;
	}

	return(systemtime_temp);
}
