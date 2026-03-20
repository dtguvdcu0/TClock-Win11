#include "tclock.h"
#include "..\common\ini_io_utf8.h"
#include "..\common\text_codec.h"
#include "..\common\text_file_utf8.h"
#include <stdlib.h>
#include <stdarg.h>

extern int Language_Offset;

typedef struct {
	const char* section;
	const char* key;
} INIR_KEY;

typedef struct {
	const char* section;
	const char* combinedKey;
	const char* legacyPrefix;
	int firstIndex;
	int lastIndex;
} INIR_LEGACY_FAMILY;

typedef struct {
	const char* section;
	const char* key;
	const char* baseKey;
} INIR_UTF8HEX_KEY;

typedef struct {
	char section[64];
	char key[128];
	BOOL seeded;
	BOOL classified;
	char classSource[32];
	char auditBucket[48];
	char auditConfidence[16];
} INIR_INV_KEY;

static const INIR_KEY k_inirStaleKeys[] = {
	{ "ETC", "UseHideClockPolicyFlow" },
	{ "ETC", "2chHelpURL" },
	{ "Status_DoNotEdit", "ModernStandbySupported" },
	{ "Status_DoNotEdit", "Win11LayoutDegraded" },
	{ "Status_DoNotEdit", "Win11IconSize" },
	{ "Status_DoNotEdit", "NumberOfProfiles" },
	{ "Status_DoNotEdit", "CurrentInternetProfileNumber" },
	{ "AnalogClock", "AnalogClockAtStartBtn" },
};

static const INIR_UTF8HEX_KEY k_inirUtf8HexKeys[] = {
	{ "Color_Font", "FontUtf8Hex", "Font" },
	{ "Format", "FormatUtf8Hex", "Format" },
	{ "Format", "CustomFormatUtf8Hex", "CustomFormat" },
	{ "Tooltip", "TipFontUtf8Hex", "TipFont" },
	{ "Tooltip", "TipTitleUtf8Hex", "TipTitle" },
	{ "Tooltip", "TooltipUtf8Hex", "Tooltip" },
	{ "Tooltip", "Tooltip2Utf8Hex", "Tooltip2" },
	{ "Tooltip", "Tooltip3Utf8Hex", "Tooltip3" },
	{ "ETC", "TCapturePathUtf8Hex", "TCapturePath" },
};

static const INIR_LEGACY_FAMILY k_inirLegacyFamilies[] = {
	{ "ETC", "EthernetKeywords", "Ethernet_Keyword", 1, 5 },
	{ "VPN", "VPNKeywords", "VPN_Keyword", 1, 5 },
	{ "VPN", "VPNExcludeKeywords", "VPN_Exclude", 1, 5 },
};

static void inir_append(char* report, int cchReport, const char* fmt, ...)
{
	va_list ap;
	char tmp[512];
	int used;
	int remain;

	if (!report || cchReport <= 0 || !fmt) return;
	used = lstrlen(report);
	if (used >= cchReport - 1) return;
	remain = cchReport - used;
	va_start(ap, fmt);
	wvsprintfA(tmp, fmt, ap);
	va_end(ap);
	lstrcpynA(report + used, tmp, remain);
}

static BOOL inir_is_checked(HWND hDlg, int id)
{
	return IsDlgButtonChecked(hDlg, id) == BST_CHECKED ? TRUE : FALSE;
}

static BOOL inir_read_raw_file(const char* path, char** outRaw, DWORD* outSize)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	wchar_t wPath[MAX_PATH];
	DWORD size = 0;
	DWORD readSize = 0;
	char* raw = NULL;

	if (outRaw) *outRaw = NULL;
	if (outSize) *outSize = 0;
	if (!path || !outRaw || !outSize) return FALSE;
	if (!tc_path_utf8_or_ansi_to_utf16(path, wPath, (int)_countof(wPath))) return FALSE;
	hFile = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return FALSE;
	size = GetFileSize(hFile, NULL);
	raw = (char*)malloc((size_t)size + 1);
	if (!raw) {
		CloseHandle(hFile);
		return FALSE;
	}
	if (size > 0 && !ReadFile(hFile, raw, size, &readSize, NULL)) {
		free(raw);
		CloseHandle(hFile);
		return FALSE;
	}
	CloseHandle(hFile);
	raw[readSize] = '\0';
	*outRaw = raw;
	*outSize = readSize;
	return TRUE;
}

static BOOL inir_bytes_are_valid_utf8(const char* s, int len)
{
	if (!s || len <= 0) return FALSE;
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0) > 0 ? TRUE : FALSE;
}

static BOOL inir_append_utf8_chunk(char** outBuf, DWORD* outCap, DWORD* outLen, const char* src, DWORD srcLen)
{
	char* grown = NULL;
	DWORD need = 0;
	if (!outBuf || !outCap || !outLen) return FALSE;
	if (!src || srcLen == 0) return TRUE;
	need = *outLen + srcLen + 1;
	if (need > *outCap) {
		DWORD newCap = (*outCap == 0) ? 4096 : *outCap;
		while (newCap < need) {
			if (newCap > 0x7FFFFFFF / 2) return FALSE;
			newCap *= 2;
		}
		grown = (char*)malloc((size_t)newCap);
		if (!grown) return FALSE;
		if (*outBuf && *outLen > 0) {
			CopyMemory(grown, *outBuf, *outLen);
		}
		if (*outBuf) free(*outBuf);
		*outBuf = grown;
		*outCap = newCap;
	}
	CopyMemory(*outBuf + *outLen, src, srcLen);
	*outLen += srcLen;
	(*outBuf)[*outLen] = '\0';
	return TRUE;
}

static BOOL inir_append_line_as_utf8(char** outBuf, DWORD* outCap, DWORD* outLen, const char* src, DWORD srcLen)
{
	wchar_t* wide = NULL;
	int wideLen = 0;
	int utf8Len = 0;
	char* utf8 = NULL;

	if (!srcLen) return TRUE;
	if (inir_bytes_are_valid_utf8(src, (int)srcLen)) {
		return inir_append_utf8_chunk(outBuf, outCap, outLen, src, srcLen);
	}

	wide = (wchar_t*)malloc(sizeof(wchar_t) * ((size_t)srcLen + 1));
	if (!wide) return FALSE;
	wideLen = tc_ansi_bytes_to_utf16_compat(0, src, (int)srcLen, wide, (int)srcLen + 1);
	if (wideLen <= 0) {
		free(wide);
		return FALSE;
	}
	utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, NULL, 0, NULL, NULL);
	if (utf8Len <= 0) {
		free(wide);
		return FALSE;
	}
	utf8 = (char*)malloc((size_t)utf8Len);
	if (!utf8) {
		free(wide);
		return FALSE;
	}
	if (WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, utf8, utf8Len, NULL, NULL) <= 0) {
		free(utf8);
		free(wide);
		return FALSE;
	}
	free(wide);
	if (!inir_append_utf8_chunk(outBuf, outCap, outLen, utf8, (DWORD)utf8Len)) {
		free(utf8);
		return FALSE;
	}
	free(utf8);
	return TRUE;
}

static BOOL inir_append_kv_line_as_utf8(char** outBuf, DWORD* outCap, DWORD* outLen, const char* src, DWORD srcLen)
{
	DWORD i = 0;

	if (!srcLen) return TRUE;
	while (i < srcLen) {
		if (src[i] == '=') {
			if (!inir_append_line_as_utf8(outBuf, outCap, outLen, src, i + 1)) return FALSE;
			return inir_append_line_as_utf8(outBuf, outCap, outLen, src + i + 1, srcLen - (i + 1));
		}
		if (src[i] == ';' || src[i] == '#') break;
		i++;
	}
	return inir_append_line_as_utf8(outBuf, outCap, outLen, src, srcLen);
}

static BOOL inir_convert_mixed_text_to_utf8(const char* raw, DWORD rawSize, char** outText, DWORD* outSize)
{
	DWORD pos = 0;
	DWORD lineStart = 0;
	DWORD outCap = 0;
	DWORD outLen = 0;
	char* outBuf = NULL;

	if (outText) *outText = NULL;
	if (outSize) *outSize = 0;
	if (!raw || !outText || !outSize) return FALSE;

	while (pos < rawSize) {
		if (raw[pos] == '\r' || raw[pos] == '\n') {
			DWORD lineLen = pos - lineStart;
			if (!inir_append_kv_line_as_utf8(&outBuf, &outCap, &outLen, raw + lineStart, lineLen)) {
				free(outBuf);
				return FALSE;
			}
			if (raw[pos] == '\r' && pos + 1 < rawSize && raw[pos + 1] == '\n') {
				if (!inir_append_utf8_chunk(&outBuf, &outCap, &outLen, "\r\n", 2)) {
					free(outBuf);
					return FALSE;
				}
				pos += 2;
			}
			else {
				if (!inir_append_utf8_chunk(&outBuf, &outCap, &outLen, (raw[pos] == '\r') ? "\r" : "\n", 1)) {
					free(outBuf);
					return FALSE;
				}
				pos += 1;
			}
			lineStart = pos;
			continue;
		}
		pos++;
	}
	if (lineStart < rawSize) {
		if (!inir_append_kv_line_as_utf8(&outBuf, &outCap, &outLen, raw + lineStart, rawSize - lineStart)) {
			free(outBuf);
			return FALSE;
		}
	}
	if (!outBuf) {
		outBuf = (char*)malloc(1);
		if (!outBuf) return FALSE;
		outBuf[0] = '\0';
	}
	*outText = outBuf;
	*outSize = outLen;
	return TRUE;
}

static BOOL inir_load_text_any(const char* path, char** outText, DWORD* outSize, BOOL* outHadBom, BOOL* outIsUtf8)
{
	char* utf8Text = NULL;
	DWORD utf8Size = 0;
	BOOL hadBom = FALSE;
	char* raw = NULL;
	DWORD rawSize = 0;
	char* outBuf = NULL;

	if (outText) *outText = NULL;
	if (outSize) *outSize = 0;
	if (outHadBom) *outHadBom = FALSE;
	if (outIsUtf8) *outIsUtf8 = FALSE;
	if (!path || !outText || !outSize) return FALSE;

	if (tc_read_text_file_utf8(path, &utf8Text, &utf8Size, &hadBom)) {
		outBuf = (char*)malloc((size_t)utf8Size + 1);
		if (!outBuf) {
			tc_free_text_buffer(utf8Text);
			return FALSE;
		}
		CopyMemory(outBuf, utf8Text, utf8Size);
		outBuf[utf8Size] = '\0';
		tc_free_text_buffer(utf8Text);
		*outText = outBuf;
		*outSize = utf8Size;
		if (outHadBom) *outHadBom = hadBom;
		if (outIsUtf8) *outIsUtf8 = TRUE;
		return TRUE;
	}

	if (!inir_read_raw_file(path, &raw, &rawSize)) return FALSE;
	if (!inir_convert_mixed_text_to_utf8(raw, rawSize, &outBuf, outSize)) {
		free(raw);
		return FALSE;
	}
	free(raw);
	*outText = outBuf;
	if (outHadBom) *outHadBom = FALSE;
	if (outIsUtf8) *outIsUtf8 = FALSE;
	return TRUE;
}

static BOOL inir_apply_encoding(char* report, int cchReport, int* changedCount)
{
	char* text = NULL;
	DWORD size = 0;
	BOOL hadBom = FALSE;
	BOOL isUtf8 = FALSE;

	if (changedCount) *changedCount = 0;
	inir_append(report, cchReport, "[Encoding]\r\n");
	if (!inir_load_text_any(g_inifile, &text, &size, &hadBom, &isUtf8)) {
		inir_append(report, cchReport, "  failed: detection error\r\n");
		return FALSE;
	}
	if (isUtf8) {
		inir_append(report, cchReport, "  no changes (already UTF-8)\r\n");
		free(text);
		return TRUE;
	}
	if (!tc_write_text_file_utf8(g_inifile, text, size, FALSE)) {
		inir_append(report, cchReport, "  failed: write error\r\n");
		free(text);
		return FALSE;
	}
	free(text);
	if (changedCount) *changedCount = 1;
	inir_append(report, cchReport, "  converted: ACP-compatible text rewritten as UTF-8\r\n");
	return TRUE;
}

static void inir_trim_lr(const char* s, int len, int* l, int* r)
{
	int left = 0;
	int right = len;

	if (!s || len <= 0) {
		if (l) *l = 0;
		if (r) *r = 0;
		return;
	}
	while (left < right && (s[left] == ' ' || s[left] == '\t' || s[left] == '\r' || s[left] == '\n')) left++;
	while (right > left && (s[right - 1] == ' ' || s[right - 1] == '\t' || s[right - 1] == '\r' || s[right - 1] == '\n')) right--;
	if (l) *l = left;
	if (r) *r = right;
}

static BOOL inir_line_is_section(const char* s, int len, const char* section)
{
	int l = 0;
	int r = 0;
	int nameLen;

	if (!s || !section) return FALSE;
	inir_trim_lr(s, len, &l, &r);
	if (r - l < 3 || s[l] != '[' || s[r - 1] != ']') return FALSE;
	nameLen = r - l - 2;
	if (nameLen != lstrlen(section)) return FALSE;
	return _strnicmp(s + l + 1, section, nameLen) == 0 ? TRUE : FALSE;
}

static BOOL inir_line_is_any_section(const char* s, int len)
{
	int l = 0;
	int r = 0;

	if (!s) return FALSE;
	inir_trim_lr(s, len, &l, &r);
	return (r - l >= 3 && s[l] == '[' && s[r - 1] == ']') ? TRUE : FALSE;
}

static int inir_parse_section_multisz(const char* text, DWORD size, const char* section, char* outBuf, int outBytes)
{
	DWORD i = 0;
	int pos = 0;
	int count = 0;
	BOOL inTarget = FALSE;

	if (!text || !section || !outBuf || outBytes <= 1) return 0;
	outBuf[0] = '\0';
	outBuf[1] = '\0';

	while (i < size) {
		DWORD ls = i;
		DWORD le = i;
		DWORD txtEnd;
		int ll = 0;
		int rr = 0;

		while (le < size && text[le] != '\r' && text[le] != '\n') le++;
		txtEnd = le;
		if (le < size && text[le] == '\r') {
			le++;
			if (le < size && text[le] == '\n') le++;
		}
		else if (le < size && text[le] == '\n') {
			le++;
		}

		if (inir_line_is_section(text + ls, (int)(txtEnd - ls), section)) {
			inTarget = TRUE;
			i = le;
			continue;
		}
		if (inir_line_is_any_section(text + ls, (int)(txtEnd - ls))) {
			inTarget = FALSE;
			i = le;
			continue;
		}
		if (!inTarget) {
			i = le;
			continue;
		}

		inir_trim_lr(text + ls, (int)(txtEnd - ls), &ll, &rr);
		if (rr > ll) {
			const char* line = text + ls + ll;
			int lineLen = rr - ll;
			int eq = -1;
			int j;
			for (j = 0; j < lineLen; ++j) {
				if (line[j] == '=') {
					eq = j;
					break;
				}
			}
			if (line[0] != ';' && line[0] != '#' && eq >= 0) {
				int kl = 0;
				int kr = eq;
				int vl = eq + 1;
				int vr = lineLen;
				int keyLen;
				int valLen;
				int need;

				inir_trim_lr(line, eq, &kl, &kr);
				inir_trim_lr(line + eq + 1, lineLen - (eq + 1), &vl, &vr);
				keyLen = kr - kl;
				valLen = vr - vl;
				if (keyLen > 0) {
					need = keyLen + 1 + (valLen > 0 ? valLen : 0) + 1;
					if (pos + need + 1 >= outBytes) break;
					CopyMemory(outBuf + pos, line + kl, (SIZE_T)keyLen);
					pos += keyLen;
					outBuf[pos++] = '=';
					if (valLen > 0) {
						CopyMemory(outBuf + pos, line + eq + 1 + vl, (SIZE_T)valLen);
						pos += valLen;
					}
					outBuf[pos++] = '\0';
					++count;
				}
			}
		}
		i = le;
	}

	if (pos >= outBytes - 1) pos = outBytes - 2;
	outBuf[pos++] = '\0';
	outBuf[pos] = '\0';
	return count;
}

static BOOL inir_parse_section_name(const char* line, int lineLen, char* outSection, int outSectionBytes)
{
	int ll = 0;
	int rr = 0;
	int nameLen = 0;

	if (!line || !outSection || outSectionBytes <= 1) return FALSE;
	outSection[0] = '\0';
	if (!inir_line_is_any_section(line, lineLen)) return FALSE;
	inir_trim_lr(line + 1, lineLen - 2, &ll, &rr);
	nameLen = rr - ll;
	if (nameLen <= 0) return TRUE;
	if (nameLen >= outSectionBytes) nameLen = outSectionBytes - 1;
	CopyMemory(outSection, line + 1 + ll, (SIZE_T)nameLen);
	outSection[nameLen] = '\0';
	return TRUE;
}

static int inir_inventory_find(const INIR_INV_KEY* items, int count, const char* section, const char* key)
{
	int i;
	const char* sec = section ? section : "";

	if (!items || count <= 0 || !key || !key[0]) return -1;
	for (i = 0; i < count; ++i) {
		if (_stricmp(items[i].section, sec) != 0) continue;
		if (_stricmp(items[i].key, key) != 0) continue;
		return i;
	}
	return -1;
}

static void inir_copy_text(char* dst, int dstBytes, const char* src)
{
	int n = 0;

	if (!dst || dstBytes <= 0) return;
	dst[0] = '\0';
	if (!src) return;
	while (n < dstBytes - 1 && src[n]) {
		dst[n] = src[n];
		n++;
	}
	dst[n] = '\0';
}

static BOOL inir_inventory_add(INIR_INV_KEY** items, int* count, int* cap, const char* section, const char* key)
{
	INIR_INV_KEY* grown = NULL;
	INIR_INV_KEY* entry = NULL;
	const char* sec = section ? section : "";
	int nextCap;

	if (!items || !count || !cap || !key || !key[0]) return FALSE;
	if (inir_inventory_find(*items, *count, sec, key) >= 0) return TRUE;
	if (*count >= *cap) {
		nextCap = (*cap > 0) ? (*cap * 2) : 128;
		if (*items) {
			grown = (INIR_INV_KEY*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *items, (SIZE_T)nextCap * sizeof(INIR_INV_KEY));
		}
		else {
			grown = (INIR_INV_KEY*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)nextCap * sizeof(INIR_INV_KEY));
		}
		if (!grown) return FALSE;
		*items = grown;
		*cap = nextCap;
	}
	entry = &(*items)[(*count)++];
	ZeroMemory(entry, sizeof(*entry));
	inir_copy_text(entry->section, (int)_countof(entry->section), sec);
	inir_copy_text(entry->key, (int)_countof(entry->key), key);
	return TRUE;
}

static BOOL inir_collect_keys_from_text(const char* text, DWORD size, INIR_INV_KEY** items, int* count, int* cap)
{
	DWORD i = 0;
	char currentSection[64];

	if (!text || !items || !count || !cap) return FALSE;
	currentSection[0] = '\0';

	while (i < size) {
		DWORD ls = i;
		DWORD le = i;
		DWORD txtEnd;
		int ll = 0;
		int rr = 0;

		while (le < size && text[le] != '\r' && text[le] != '\n') le++;
		txtEnd = le;
		if (le < size && text[le] == '\r') {
			le++;
			if (le < size && text[le] == '\n') le++;
		}
		else if (le < size && text[le] == '\n') {
			le++;
		}

		inir_trim_lr(text + ls, (int)(txtEnd - ls), &ll, &rr);
		if (rr > ll) {
			const char* line = text + ls + ll;
			int lineLen = rr - ll;
			if (inir_line_is_any_section(line, lineLen)) {
				inir_parse_section_name(line, lineLen, currentSection, (int)_countof(currentSection));
			}
			else if (line[0] != ';' && line[0] != '#') {
				int j;
				for (j = 0; j < lineLen; ++j) {
					if (line[j] == '=') {
						int kl = 0;
						int kr = j;
						char key[128];
						int keyLen;
						inir_trim_lr(line, j, &kl, &kr);
						keyLen = kr - kl;
						if (keyLen > 0) {
							if (keyLen >= (int)_countof(key)) keyLen = (int)_countof(key) - 1;
							CopyMemory(key, line + kl, (SIZE_T)keyLen);
							key[keyLen] = '\0';
							if (!inir_inventory_add(items, count, cap, currentSection, key)) return FALSE;
						}
						break;
					}
				}
			}
		}
		i = le;
	}
	return TRUE;
}

static BOOL inir_collect_keys_from_file(const char* path, INIR_INV_KEY** items, int* count, int* cap)
{
	char* text = NULL;
	DWORD size = 0;
	BOOL hadBom = FALSE;
	BOOL isUtf8 = FALSE;
	BOOL ok = FALSE;

	if (!path || !items || !count || !cap) return FALSE;
	if (!inir_load_text_any(path, &text, &size, &hadBom, &isUtf8)) return FALSE;
	UNREFERENCED_PARAMETER(hadBom);
	UNREFERENCED_PARAMETER(isUtf8);
	ok = inir_collect_keys_from_text(text, size, items, count, cap);
	free(text);
	return ok;
}

static BOOL inir_collect_seed_keys(INIR_INV_KEY** items, int* count, int* cap)
{
	wchar_t tempDir[MAX_PATH];
	wchar_t tempFile[MAX_PATH];
	char originalIni[MAX_PATH];
	char tempIni[MAX_PATH];
	BOOL ok = FALSE;

	if (!items || !count || !cap) return FALSE;
	if (!GetTempPathW((DWORD)_countof(tempDir), tempDir)) return FALSE;
	if (!GetTempFileNameW(tempDir, L"INR", 0, tempFile)) return FALSE;
	DeleteFileW(tempFile);
	inir_copy_text(originalIni, (int)_countof(originalIni), g_inifile);
	CreateDefaultIniFile_Win10(tempFile);
	inir_copy_text(tempIni, (int)_countof(tempIni), g_inifile);
	tc_ini_utf8_clear_cache();
	ok = inir_collect_keys_from_file(tempIni, items, count, cap);
	DeleteFileW(tempFile);
	inir_copy_text(g_inifile, MAX_PATH, originalIni);
	tc_ini_utf8_clear_cache();
	return ok;
}

static void inir_build_legacy_key(char* outKey, int outKeyBytes, const char* prefix, int index)
{
	char digits[16];
	int digitCount = 0;
	int prefixLen = 0;
	int value = index;
	int i;

	if (!outKey || outKeyBytes <= 0) return;
	outKey[0] = '\0';
	if (!prefix) return;
	while (prefix[prefixLen] && prefixLen < outKeyBytes - 1) {
		outKey[prefixLen] = prefix[prefixLen];
		prefixLen++;
	}
	if (value <= 0) {
		if (prefixLen < outKeyBytes - 1) outKey[prefixLen++] = '0';
		outKey[prefixLen] = '\0';
		return;
	}
	while (value > 0 && digitCount < (int)_countof(digits)) {
		digits[digitCount++] = (char)('0' + (value % 10));
		value /= 10;
	}
	for (i = digitCount - 1; i >= 0 && prefixLen < outKeyBytes - 1; --i) {
		outKey[prefixLen++] = digits[i];
	}
	outKey[prefixLen] = '\0';
}

static BOOL inir_key_is_stale_exact(const char* section, const char* key)
{
	int i;
	for (i = 0; i < (int)_countof(k_inirStaleKeys); ++i) {
		if (_stricmp(k_inirStaleKeys[i].section, section ? section : "") != 0) continue;
		if (_stricmp(k_inirStaleKeys[i].key, key) != 0) continue;
		return TRUE;
	}
	return FALSE;
}

static BOOL inir_key_is_utf8hex_fixed(const char* section, const char* key)
{
	int i;
	for (i = 0; i < (int)_countof(k_inirUtf8HexKeys); ++i) {
		if (_stricmp(k_inirUtf8HexKeys[i].section, section ? section : "") != 0) continue;
		if (_stricmp(k_inirUtf8HexKeys[i].key, key) != 0) continue;
		return TRUE;
	}
	return FALSE;
}

static BOOL inir_key_is_legacy_member(const char* section, const char* key)
{
	int i;
	int j;
	char legacyKey[128];
	for (i = 0; i < (int)_countof(k_inirLegacyFamilies); ++i) {
		if (_stricmp(k_inirLegacyFamilies[i].section, section ? section : "") != 0) continue;
		for (j = k_inirLegacyFamilies[i].firstIndex; j <= k_inirLegacyFamilies[i].lastIndex; ++j) {
			inir_build_legacy_key(legacyKey, (int)_countof(legacyKey), k_inirLegacyFamilies[i].legacyPrefix, j);
			if (_stricmp(legacyKey, key) == 0) return TRUE;
		}
	}
	return FALSE;
}

static BOOL inir_key_is_currently_classified(const char* section, const char* key)
{
	if (inir_key_is_stale_exact(section, key)) return TRUE;
	if (inir_key_is_utf8hex_fixed(section, key)) return TRUE;
	if (inir_key_is_legacy_member(section, key)) return TRUE;
	return FALSE;
}

static BOOL inir_seed_meta(const char* section, const char* key, BOOL* emitOnCreate, BOOL* dropEligible)
{
	int i;
	const char* seedSection = NULL;
	const char* seedKey = NULL;
	const char* defaultValue = NULL;
	BOOL emit = FALSE;
	BOOL drop = FALSE;

	for (i = 0; i < seed_count(); ++i) {
		if (!seed_get(i, &seedSection, &seedKey, &defaultValue, &emit, &drop)) continue;
		UNREFERENCED_PARAMETER(defaultValue);
		if (_stricmp(seedSection ? seedSection : "", section ? section : "") != 0) continue;
		if (_stricmp(seedKey ? seedKey : "", key ? key : "") != 0) continue;
		if (emitOnCreate) *emitOnCreate = emit;
		if (dropEligible) *dropEligible = drop;
		return TRUE;
	}
	return FALSE;
}

static BOOL inir_status_audit(const char* key, const char** bucket, const char** confidence)
{
	if (!key || !key[0]) return FALSE;

	if (_stricmp(key, "SafeMode") == 0
		|| _stricmp(key, "CountAutoRestart") == 0
		|| _stricmp(key, "LastExitUser") == 0) {
		if (bucket) *bucket = "explicit-state";
		if (confidence) *confidence = "inherited";
		return TRUE;
	}

	if (_stricmp(key, "PreviousLTEProfNumber") == 0
		|| _stricmp(key, "ModernStandbySupported") == 0
		|| _stricmp(key, "BatteryLifeAvailable") == 0
		|| _stricmp(key, "TimerCountForSec") == 0
		|| _stricmp(key, "Win11TClockMain") == 0
		|| _stricmp(key, "Win11LayoutDegraded") == 0
		|| _stricmp(key, "ClockWidth") == 0
		|| _stricmp(key, "ClockHeight") == 0
		|| _stricmp(key, "Win11IconSize") == 0) {
		if (bucket) *bucket = "runtime-cache";
		if (confidence) *confidence = "inherited";
		return TRUE;
	}

	return FALSE;
}

static void inir_overlay_audit(INIR_INV_KEY* item)
{
	BOOL emitOnCreate = FALSE;
	BOOL dropEligible = FALSE;
	const char* bucket = NULL;
	const char* confidence = NULL;

	if (!item) return;
	item->auditBucket[0] = '\0';
	item->auditConfidence[0] = '\0';

	if (inir_seed_meta(item->section, item->key, &emitOnCreate, &dropEligible)) {
		UNREFERENCED_PARAMETER(emitOnCreate);
		if (dropEligible) {
			inir_copy_text(item->auditBucket, (int)_countof(item->auditBucket), "default-readable");
			inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "proven");
			return;
		}
	}

	if (inir_key_is_utf8hex_fixed(item->section, item->key) || inir_key_is_legacy_member(item->section, item->key)) {
		inir_copy_text(item->auditBucket, (int)_countof(item->auditBucket), "compatibility-sensitive");
		inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "proven");
		return;
	}

	if (inir_key_is_stale_exact(item->section, item->key)) {
		inir_copy_text(item->auditBucket, (int)_countof(item->auditBucket), "obsolete");
		inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "proven");
		return;
	}

	if (_stricmp(item->section, "Status_DoNotEdit") == 0 && inir_status_audit(item->key, &bucket, &confidence)) {
		inir_copy_text(item->auditBucket, (int)_countof(item->auditBucket), bucket);
		inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), confidence);
		return;
	}

	if (item->seeded) {
		inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "needs-recheck");
		return;
	}

	if (item->classified) {
		inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "proven");
		return;
	}

	inir_copy_text(item->auditConfidence, (int)_countof(item->auditConfidence), "uncategorized");
}

static const char* inir_classify_key_source(const char* section, const char* key)
{
	if (inir_key_is_stale_exact(section, key)) return "stale";
	if (inir_key_is_utf8hex_fixed(section, key)) return "utf8hex";
	if (inir_key_is_legacy_member(section, key)) return "legacy";
	return "";
}

static void inir_append_inventory_line(char* report, int cchReport, const INIR_INV_KEY* item)
{
	if (!item) return;
	if (item->auditBucket[0] && item->auditConfidence[0] && item->classSource[0]) {
		inir_append(report, cchReport, "  [%s] %s (%s; %s/%s)\r\n",
			item->section[0] ? item->section : "root",
			item->key,
			item->classSource,
			item->auditBucket,
			item->auditConfidence);
	}
	else if (item->auditBucket[0] && item->auditConfidence[0]) {
		inir_append(report, cchReport, "  [%s] %s (%s/%s)\r\n",
			item->section[0] ? item->section : "root",
			item->key,
			item->auditBucket,
			item->auditConfidence);
	}
	else if (item->auditConfidence[0] && item->classSource[0]) {
		inir_append(report, cchReport, "  [%s] %s (%s; %s)\r\n",
			item->section[0] ? item->section : "root",
			item->key,
			item->classSource,
			item->auditConfidence);
	}
	else if (item->classSource[0]) {
		inir_append(report, cchReport, "  [%s] %s (%s)\r\n",
			item->section[0] ? item->section : "root",
			item->key,
			item->classSource);
	}
	else if (item->auditConfidence[0]) {
		inir_append(report, cchReport, "  [%s] %s (%s)\r\n",
			item->section[0] ? item->section : "root",
			item->key,
			item->auditConfidence);
	}
	else {
		inir_append(report, cchReport, "  [%s] %s\r\n",
			item->section[0] ? item->section : "root",
			item->key);
	}
}

static int inir_scan_inventory(char* report, int cchReport)
{
	INIR_INV_KEY* active = NULL;
	INIR_INV_KEY* seeded = NULL;
	int activeCount = 0;
	int activeCap = 0;
	int seedCount = 0;
	int seedCap = 0;
	int i;
	int seededPresent = 0;
	int presentNotSeeded = 0;
	int seededNotPresent = 0;
	int uncategorized = 0;

	if (!inir_collect_keys_from_file(g_inifile, &active, &activeCount, &activeCap)) {
		inir_append(report, cchReport, "[Inventory: failed]\r\n");
		inir_append(report, cchReport, "  failed: active INI inventory could not be collected.\r\n");
		if (active) HeapFree(GetProcessHeap(), 0, active);
		return 0;
	}
	if (!inir_collect_seed_keys(&seeded, &seedCount, &seedCap)) {
		inir_append(report, cchReport, "[Inventory: failed]\r\n");
		inir_append(report, cchReport, "  failed: seed inventory could not be collected.\r\n");
		if (active) HeapFree(GetProcessHeap(), 0, active);
		if (seeded) HeapFree(GetProcessHeap(), 0, seeded);
		return 0;
	}

	for (i = 0; i < activeCount; ++i) {
		active[i].seeded = (inir_inventory_find(seeded, seedCount, active[i].section, active[i].key) >= 0) ? TRUE : FALSE;
		inir_copy_text(active[i].classSource, (int)_countof(active[i].classSource), active[i].seeded ? "seed" : inir_classify_key_source(active[i].section, active[i].key));
		active[i].classified = active[i].seeded ? TRUE : inir_key_is_currently_classified(active[i].section, active[i].key);
		inir_overlay_audit(&active[i]);
	}

	inir_append(report, cchReport, "[Inventory: seeded-present]\r\n");
	for (i = 0; i < activeCount; ++i) {
		if (!active[i].seeded) continue;
		inir_append_inventory_line(report, cchReport, &active[i]);
		seededPresent++;
	}
	if (!seededPresent) inir_append(report, cchReport, "  none\r\n");

	inir_append(report, cchReport, "[Inventory: present-not-seeded]\r\n");
	for (i = 0; i < activeCount; ++i) {
		if (active[i].seeded) continue;
		inir_append_inventory_line(report, cchReport, &active[i]);
		presentNotSeeded++;
	}
	if (!presentNotSeeded) inir_append(report, cchReport, "  none\r\n");

	inir_append(report, cchReport, "[Inventory: seeded-not-present]\r\n");
	for (i = 0; i < seedCount; ++i) {
		INIR_INV_KEY seedItem;
		if (inir_inventory_find(active, activeCount, seeded[i].section, seeded[i].key) >= 0) continue;
		ZeroMemory(&seedItem, sizeof(seedItem));
		inir_copy_text(seedItem.section, (int)_countof(seedItem.section), seeded[i].section);
		inir_copy_text(seedItem.key, (int)_countof(seedItem.key), seeded[i].key);
		inir_copy_text(seedItem.classSource, (int)_countof(seedItem.classSource), "seed");
		inir_copy_text(seedItem.auditConfidence, (int)_countof(seedItem.auditConfidence), "needs-recheck");
		inir_append_inventory_line(report, cchReport, &seedItem);
		seededNotPresent++;
	}
	if (!seededNotPresent) inir_append(report, cchReport, "  none\r\n");

	inir_append(report, cchReport, "[Inventory: uncategorized]\r\n");
	for (i = 0; i < activeCount; ++i) {
		if (active[i].classified) continue;
		inir_append_inventory_line(report, cchReport, &active[i]);
		uncategorized++;
	}
	if (!uncategorized) inir_append(report, cchReport, "  none\r\n");

	if (active) HeapFree(GetProcessHeap(), 0, active);
	if (seeded) HeapFree(GetProcessHeap(), 0, seeded);
	return activeCount + seedCount;
}

static int inir_read_section_multisz(const char* section, char* outBuf, int outBytes)
{
	char* text = NULL;
	DWORD size = 0;
	BOOL hadBom = FALSE;
	BOOL isUtf8 = FALSE;
	int count;

	if (!section || !outBuf || outBytes <= 1 || !g_inifile[0]) return 0;
	if (inir_load_text_any(g_inifile, &text, &size, &hadBom, &isUtf8)) {
		count = inir_parse_section_multisz(text, size, section, outBuf, outBytes);
		free(text);
		return count;
	}
	return tc_ini_utf8_read_section_multisz(g_inifile, section, outBuf, outBytes);
}

static BOOL inir_find_entry(const char* section, const char* key, char* outVal, int outBytes, BOOL* found)
{
	char entries[32768];
	const char* p;
	int keyLen;

	if (found) *found = FALSE;
	if (outVal && outBytes > 0) outVal[0] = '\0';
	if (!section || !key || !key[0] || !g_inifile[0]) return FALSE;

	if (inir_read_section_multisz(section, entries, (int)sizeof(entries)) <= 0) {
		return TRUE;
	}

	keyLen = lstrlen(key);
	p = entries;
	while (*p) {
		if (_strnicmp(p, key, keyLen) == 0 && p[keyLen] == '=') {
			if (found) *found = TRUE;
			if (outVal && outBytes > 0) {
				lstrcpyn(outVal, p + keyLen + 1, outBytes);
			}
			return TRUE;
		}
		p += lstrlen(p) + 1;
	}
	return TRUE;
}

static int inir_scan_stale(char* report, int cchReport)
{
	int i;
	int foundCount = 0;
	BOOL found;

	for (i = 0; i < (int)_countof(k_inirStaleKeys); i++) {
		found = FALSE;
		inir_find_entry(k_inirStaleKeys[i].section, k_inirStaleKeys[i].key, NULL, 0, &found);
		if (found) {
			inir_append(report, cchReport, "  found: [%s] %s\r\n", k_inirStaleKeys[i].section, k_inirStaleKeys[i].key);
			foundCount++;
		}
	}
	if (!foundCount) inir_append(report, cchReport, "  none\r\n");
	return foundCount;
}

static int inir_scan_legacy_families(char* report, int cchReport)
{
	int i;
	int j;
	int foundCount = 0;
	BOOL foundCombined = FALSE;
	BOOL foundLegacy = FALSE;
	char key[64];
	char value[256];

	for (i = 0; i < (int)_countof(k_inirLegacyFamilies); i++) {
		foundCombined = FALSE;
		inir_find_entry(k_inirLegacyFamilies[i].section, k_inirLegacyFamilies[i].combinedKey, NULL, 0, &foundCombined);
		for (j = k_inirLegacyFamilies[i].firstIndex; j <= k_inirLegacyFamilies[i].lastIndex; j++) {
			wsprintfA(key, "%s%d", k_inirLegacyFamilies[i].legacyPrefix, j);
			foundLegacy = FALSE;
			value[0] = '\0';
			inir_find_entry(k_inirLegacyFamilies[i].section, key, value, (int)sizeof(value), &foundLegacy);
			if (!foundLegacy) continue;
			if (!(foundCombined || value[0] == '\0')) continue;
			inir_append(report, cchReport, "  removable: [%s] %s%s%s\r\n",
				k_inirLegacyFamilies[i].section,
				key,
				foundCombined ? " (shadowed by combined key)" : "",
				(!foundCombined && value[0] == '\0') ? " (empty)" : "");
			foundCount++;
		}
	}
	return foundCount;
}

static int inir_apply_stale(char* report, int cchReport)
{
	int i;
	int removed = 0;
	BOOL found;

	for (i = 0; i < (int)_countof(k_inirStaleKeys); i++) {
		found = FALSE;
		inir_find_entry(k_inirStaleKeys[i].section, k_inirStaleKeys[i].key, NULL, 0, &found);
		if (!found) continue;
		if (tc_ini_utf8_delete_key(g_inifile, k_inirStaleKeys[i].section, k_inirStaleKeys[i].key)) {
			inir_append(report, cchReport, "  removed: [%s] %s\r\n", k_inirStaleKeys[i].section, k_inirStaleKeys[i].key);
			removed++;
		}
		else {
			inir_append(report, cchReport, "  failed: [%s] %s\r\n", k_inirStaleKeys[i].section, k_inirStaleKeys[i].key);
		}
	}
	if (!removed) inir_append(report, cchReport, "  no changes\r\n");
	return removed;
}

static int inir_apply_legacy_families(char* report, int cchReport)
{
	int i;
	int j;
	int removed = 0;
	BOOL foundCombined = FALSE;
	BOOL foundLegacy = FALSE;
	char key[64];
	char value[256];

	for (i = 0; i < (int)_countof(k_inirLegacyFamilies); i++) {
		foundCombined = FALSE;
		inir_find_entry(k_inirLegacyFamilies[i].section, k_inirLegacyFamilies[i].combinedKey, NULL, 0, &foundCombined);
		for (j = k_inirLegacyFamilies[i].firstIndex; j <= k_inirLegacyFamilies[i].lastIndex; j++) {
			wsprintfA(key, "%s%d", k_inirLegacyFamilies[i].legacyPrefix, j);
			foundLegacy = FALSE;
			value[0] = '\0';
			inir_find_entry(k_inirLegacyFamilies[i].section, key, value, (int)sizeof(value), &foundLegacy);
			if (!foundLegacy) continue;
			if (!(foundCombined || value[0] == '\0')) continue;
			if (tc_ini_utf8_delete_key(g_inifile, k_inirLegacyFamilies[i].section, key)) {
				inir_append(report, cchReport, "  removed: [%s] %s\r\n", k_inirLegacyFamilies[i].section, key);
				removed++;
			}
			else {
				inir_append(report, cchReport, "  failed: [%s] %s\r\n", k_inirLegacyFamilies[i].section, key);
			}
		}
	}
	return removed;
}

static BOOL inir_utf8hex_target_exists(const INIR_UTF8HEX_KEY* target)
{
	BOOL found = FALSE;
	if (!target) return FALSE;
	inir_find_entry(target->section, target->baseKey, NULL, 0, &found);
	return found;
}

static int inir_scan_utf8hex_fixed(char* report, int cchReport)
{
	int i;
	int foundCount = 0;
	BOOL found;

	for (i = 0; i < (int)_countof(k_inirUtf8HexKeys); i++) {
		found = FALSE;
		inir_find_entry(k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key, NULL, 0, &found);
		if (found && inir_utf8hex_target_exists(&k_inirUtf8HexKeys[i])) {
			inir_append(report, cchReport, "  removable: [%s] %s\r\n", k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key);
			foundCount++;
		}
	}
	return foundCount;
}

static int inir_apply_utf8hex_fixed(char* report, int cchReport)
{
	int i;
	int removed = 0;
	BOOL found;

	for (i = 0; i < (int)_countof(k_inirUtf8HexKeys); i++) {
		found = FALSE;
		inir_find_entry(k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key, NULL, 0, &found);
		if (!found || !inir_utf8hex_target_exists(&k_inirUtf8HexKeys[i])) continue;
		if (tc_ini_utf8_delete_key(g_inifile, k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key)) {
			inir_append(report, cchReport, "  removed: [%s] %s\r\n", k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key);
			removed++;
		}
		else {
			inir_append(report, cchReport, "  failed: [%s] %s\r\n", k_inirUtf8HexKeys[i].section, k_inirUtf8HexKeys[i].key);
		}
	}
	return removed;
}

static int inir_scan_utf8hex_section(char* report, int cchReport, const char* section)
{
	char entries[32768];
	const char* p;
	int foundCount = 0;

	if (!section) return 0;
	if (inir_read_section_multisz(section, entries, (int)sizeof(entries)) <= 0) return 0;

	p = entries;
	while (*p) {
		const char* eq = p;
		while (*eq && *eq != '=') ++eq;
		if (eq) {
			int keyLen = (int)(eq - p);
			if (*eq == '=' && keyLen > 7 && _stricmp(p + keyLen - 7, "Utf8Hex") == 0) {
				char baseKey[128];
				BOOL found = FALSE;

				if (keyLen < (int)sizeof(baseKey)) {
					memcpy(baseKey, p, (size_t)(keyLen - 7));
					baseKey[keyLen - 7] = '\0';
					inir_find_entry(section, baseKey, NULL, 0, &found);
					if (found) {
						inir_append(report, cchReport, "  removable: [%s] %.*s\r\n", section, keyLen, p);
						foundCount++;
					}
				}
			}
		}
		p += lstrlen(p) + 1;
	}
	return foundCount;
}

static int inir_apply_utf8hex_section(char* report, int cchReport, const char* section)
{
	char entries[32768];
	const char* p;
	int removed = 0;

	if (!section) return 0;
	if (inir_read_section_multisz(section, entries, (int)sizeof(entries)) <= 0) return 0;

	p = entries;
	while (*p) {
		const char* eq = p;
		while (*eq && *eq != '=') ++eq;
		if (eq) {
			int keyLen = (int)(eq - p);
			if (*eq == '=' && keyLen > 7 && _stricmp(p + keyLen - 7, "Utf8Hex") == 0) {
				char keyName[128];
				char baseKey[128];
				BOOL found = FALSE;

				if (keyLen < (int)sizeof(keyName) && keyLen - 7 < (int)sizeof(baseKey)) {
					memcpy(keyName, p, (size_t)keyLen);
					keyName[keyLen] = '\0';
					memcpy(baseKey, p, (size_t)(keyLen - 7));
					baseKey[keyLen - 7] = '\0';
					inir_find_entry(section, baseKey, NULL, 0, &found);
					if (found) {
						if (tc_ini_utf8_delete_key(g_inifile, section, keyName)) {
							inir_append(report, cchReport, "  removed: [%s] %s\r\n", section, keyName);
							removed++;
						}
						else {
							inir_append(report, cchReport, "  failed: [%s] %s\r\n", section, keyName);
						}
					}
				}
			}
		}
		p += lstrlen(p) + 1;
	}
	return removed;
}

static int inir_scan_utf8hex(char* report, int cchReport)
{
	int foundCount = 0;
	BOOL isUtf8 = FALSE;
	BOOL hasBom = FALSE;

	if (!tc_ini_utf8_detect_file(g_inifile, &isUtf8, &hasBom) || !isUtf8) {
		inir_append(report, cchReport, "  utf8hex-skip: file is not detected as UTF-8\r\n");
		return 0;
	}
	UNREFERENCED_PARAMETER(hasBom);
	foundCount += inir_scan_utf8hex_fixed(report, cchReport);
	foundCount += inir_scan_utf8hex_section(report, cchReport, "CustomVars");
	foundCount += inir_scan_utf8hex_section(report, cchReport, "MenuCustom");
	if (!foundCount) inir_append(report, cchReport, "  none\r\n");
	return foundCount;
}

static int inir_apply_utf8hex(char* report, int cchReport)
{
	int removed = 0;
	BOOL isUtf8 = FALSE;
	BOOL hasBom = FALSE;

	if (!tc_ini_utf8_detect_file(g_inifile, &isUtf8, &hasBom) || !isUtf8) {
		inir_append(report, cchReport, "  utf8hex-skip: file is not detected as UTF-8\r\n");
		return 0;
	}
	UNREFERENCED_PARAMETER(hasBom);
	removed += inir_apply_utf8hex_fixed(report, cchReport);
	removed += inir_apply_utf8hex_section(report, cchReport, "CustomVars");
	removed += inir_apply_utf8hex_section(report, cchReport, "MenuCustom");
	if (!removed) inir_append(report, cchReport, "  no changes\r\n");
	return removed;
}

static int inir_scan_obsolete(char* report, int cchReport)
{
	int foundCount = 0;

	inir_append(report, cchReport, "[Obsolete settings]\r\n");
	foundCount += inir_scan_stale(report, cchReport);
	foundCount += inir_scan_legacy_families(report, cchReport);
	foundCount += inir_scan_utf8hex(report, cchReport);
	return foundCount;
}

static int inir_apply_obsolete(char* report, int cchReport)
{
	int removed = 0;

	inir_append(report, cchReport, "[Obsolete settings]\r\n");
	removed += inir_apply_stale(report, cchReport);
	removed += inir_apply_legacy_families(report, cchReport);
	removed += inir_apply_utf8hex(report, cchReport);
	return removed;
}

static int inir_scan_defaults(char* report, int cchReport)
{
	int i;
	int foundCount = 0;
	char value[2048];
	BOOL found;
	const char* section = NULL;
	const char* key = NULL;
	const char* defaultValue = NULL;
	BOOL emitOnCreate = FALSE;
	BOOL dropEligible = FALSE;

	inir_append(report, cchReport, "[Default-valued settings]\r\n");
	for (i = 0; i < seed_count(); i++) {
		if (!seed_get(i, &section, &key, &defaultValue, &emitOnCreate, &dropEligible)) continue;
		UNREFERENCED_PARAMETER(emitOnCreate);
		if (!dropEligible) continue;
		found = FALSE;
		value[0] = '\0';
		inir_find_entry(section, key, value, (int)sizeof(value), &found);
		if (!found) continue;
		if (lstrcmp(value, defaultValue) != 0) continue;
		inir_append(report, cchReport, "  found: [%s] %s=%s\r\n",
			section, key, value[0] ? value : "\"\"");
		foundCount++;
	}
	if (!foundCount) inir_append(report, cchReport, "  no changes\r\n");
	return foundCount;
}

static int inir_apply_defaults(char* report, int cchReport)
{
	int i;
	int removed = 0;
	char value[2048];
	BOOL found;
	const char* section = NULL;
	const char* key = NULL;
	const char* defaultValue = NULL;
	BOOL emitOnCreate = FALSE;
	BOOL dropEligible = FALSE;

	inir_append(report, cchReport, "[Default-valued settings]\r\n");
	for (i = 0; i < seed_count(); i++) {
		if (!seed_get(i, &section, &key, &defaultValue, &emitOnCreate, &dropEligible)) continue;
		UNREFERENCED_PARAMETER(emitOnCreate);
		if (!dropEligible) continue;
		found = FALSE;
		value[0] = '\0';
		inir_find_entry(section, key, value, (int)sizeof(value), &found);
		if (!found) continue;
		if (lstrcmp(value, defaultValue) != 0) continue;
		if (tc_ini_utf8_delete_key(g_inifile, section, key)) {
			inir_append(report, cchReport, "  removed: [%s] %s\r\n",
				section, key);
			removed++;
		}
		else {
			inir_append(report, cchReport, "  failed: [%s] %s\r\n",
				section, key);
		}
	}
	if (!removed) inir_append(report, cchReport, "  no changes\r\n");
	return removed;
}

static void inir_scan_encoding(char* report, int cchReport)
{
	BOOL isUtf8 = FALSE;
	BOOL hasBom = FALSE;
	char* text = NULL;
	DWORD size = 0;
	BOOL hadBom = FALSE;
	BOOL loadIsUtf8 = FALSE;

	inir_append(report, cchReport, "[Encoding]\r\n");
	if (tc_ini_utf8_detect_file(g_inifile, &isUtf8, &hasBom)) {
		inir_append(report, cchReport, "  utf8=%s bom=%s\r\n", isUtf8 ? "yes" : "no", hasBom ? "yes" : "no");
		inir_append(report, cchReport, "  apply-note: Apply can normalize active values and keep UTF-8 cleanup paths available.\r\n");
		return;
	}
	if (!inir_load_text_any(g_inifile, &text, &size, &hadBom, &loadIsUtf8)) {
		inir_append(report, cchReport, "  failed: detection error\r\n");
		return;
	}
	UNREFERENCED_PARAMETER(size);
	UNREFERENCED_PARAMETER(hadBom);
	UNREFERENCED_PARAMETER(loadIsUtf8);
	free(text);
	inir_append(report, cchReport, "  utf8=no bom=no ansi-compat=yes\r\n");
	inir_append(report, cchReport, "  apply-note: Apply can rewrite this file as UTF-8 before cleanup.\r\n");
}

static void inir_scan_eol(char* report, int cchReport)
{
	char* text = NULL;
	DWORD size = 0;
	BOOL hadBom = FALSE;
	BOOL isUtf8 = FALSE;
	DWORD i = 0;
	int crlf = 0;
	int lf = 0;
	int cr = 0;

	inir_append(report, cchReport, "[Line endings]\r\n");
	if (!inir_load_text_any(g_inifile, &text, &size, &hadBom, &isUtf8)) {
		inir_append(report, cchReport, "  skipped: file is not readable as text\r\n");
		return;
	}
	UNREFERENCED_PARAMETER(hadBom);
	UNREFERENCED_PARAMETER(isUtf8);
	while (i < size) {
		if (text[i] == '\r') {
			if (i + 1 < size && text[i + 1] == '\n') {
				crlf++;
				i += 2;
			}
			else {
				cr++;
				i++;
			}
		}
		else if (text[i] == '\n') {
			lf++;
			i++;
		}
		else {
			i++;
		}
	}
	inir_append(report, cchReport, "  CRLF=%d LF=%d CR=%d\r\n", crlf, lf, cr);
	free(text);
}

static BOOL inir_apply_eol(char* report, int cchReport, int* changedCount)
{
	char* text = NULL;
	char* outText = NULL;
	DWORD size = 0;
	DWORD outSize = 0;
	DWORD i = 0;
	DWORD pos = 0;
	BOOL hadBom = FALSE;
	BOOL isUtf8 = FALSE;
	BOOL changed = FALSE;

	if (changedCount) *changedCount = 0;
	inir_append(report, cchReport, "[Line endings]\r\n");
	if (!inir_load_text_any(g_inifile, &text, &size, &hadBom, &isUtf8)) {
		inir_append(report, cchReport, "  skipped: file is not readable as text\r\n");
		return FALSE;
	}
	outText = (char*)malloc((size_t)(size * 2 + 2));
	if (!outText) {
		free(text);
		inir_append(report, cchReport, "  failed: out of memory\r\n");
		return FALSE;
	}

	while (i < size) {
		if (text[i] == '\r') {
			if (i + 1 < size && text[i + 1] == '\n') {
				outText[pos++] = '\r';
				outText[pos++] = '\n';
				i += 2;
			}
			else {
				outText[pos++] = '\r';
				outText[pos++] = '\n';
				i++;
				changed = TRUE;
			}
		}
		else if (text[i] == '\n') {
			outText[pos++] = '\r';
			outText[pos++] = '\n';
			i++;
			changed = TRUE;
		}
		else {
			outText[pos++] = text[i++];
		}
	}
	outSize = pos;

	if (changed || !isUtf8) {
		if (!tc_write_text_file_utf8(g_inifile, outText, outSize, hadBom && isUtf8)) {
			inir_append(report, cchReport, "  failed: write error\r\n");
			free(outText);
			free(text);
			return FALSE;
		}
		if (changedCount) *changedCount = 1;
		if (!isUtf8 && changed) inir_append(report, cchReport, "  converted to UTF-8 and normalized to CRLF\r\n");
		else if (!isUtf8) inir_append(report, cchReport, "  converted to UTF-8\r\n");
		else inir_append(report, cchReport, "  normalized to CRLF\r\n");
	}
	else {
		inir_append(report, cchReport, "  no changes\r\n");
	}

	free(outText);
	free(text);
	return TRUE;
}

static BOOL inir_make_backup(char* report, int cchReport)
{
	WCHAR iniPathW[MAX_PATH];
	WCHAR bakPathW[MAX_PATH + 64];
	char bakPath[1024];
	SYSTEMTIME st;

	if (!g_inifile[0]) {
		inir_append(report, cchReport, "Backup skipped: ini path is empty.\r\n");
		return FALSE;
	}
	if (tc_utf8_to_utf16(g_inifile, iniPathW, (int)_countof(iniPathW)) <= 0) {
		inir_append(report, cchReport, "Backup failed: ini path conversion failed.\r\n");
		return FALSE;
	}
	GetLocalTime(&st);
	wsprintfW(bakPathW, L"%s.%04u%02u%02u-%02u%02u%02u.bak",
		iniPathW, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	if (!CopyFileW(iniPathW, bakPathW, FALSE)) {
		inir_append(report, cchReport, "Backup failed: CopyFileW error=%lu\r\n", GetLastError());
		return FALSE;
	}
	if (tc_utf16_to_utf8(bakPathW, bakPath, (int)sizeof(bakPath)) > 0) {
		inir_append(report, cchReport, "Backup: %s\r\n", bakPath);
	}
	return TRUE;
}

static int inir_apply_runtime_normalize(char* report, int cchReport)
{
	DWORD_PTR refreshRet = 0;
	DWORD_PTR taskbarRet = 0;

	inir_append(report, cchReport, "[Normalize active values]\r\n");
	if (!g_hwndClock || !IsWindow(g_hwndClock)) {
		inir_append(report, cchReport, "  skipped: active clock window is not available.\r\n");
		return 0;
	}
	if (!SendMessageTimeout(g_hwndClock, CLOCKM_REFRESHCLOCK, 0, 0,
		SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 5000, &refreshRet)) {
		inir_append(report, cchReport, "  failed: CLOCKM_REFRESHCLOCK timed out or target exited.\r\n");
		return 0;
	}
	SendMessageTimeout(g_hwndClock, CLOCKM_REFRESHTASKBAR, 0, 0,
		SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 5000, &taskbarRet);
	UNREFERENCED_PARAMETER(taskbarRet);
	inir_append(report, cchReport, "  applied: active values reloaded and normalized.\r\n");
	return 1;
}

static void inir_run_scan(HWND hDlg)
{
	char report[131072];

	report[0] = '\0';
	tc_ini_utf8_clear_cache();
	inir_append(report, (int)sizeof(report), "INI: %s\r\n\r\n", g_inifile[0] ? g_inifile : "(none)");
	if (inir_is_checked(hDlg, IDC_INIR_ENCODING)) inir_scan_encoding(report, (int)sizeof(report));
	if (inir_is_checked(hDlg, IDC_INIR_EOL)) inir_scan_eol(report, (int)sizeof(report));
	if (inir_is_checked(hDlg, IDC_INIR_DEFAULTS)) inir_scan_defaults(report, (int)sizeof(report));
	if (inir_is_checked(hDlg, IDC_INIR_STALE)) inir_scan_obsolete(report, (int)sizeof(report));
	if (inir_is_checked(hDlg, IDC_INIR_DEFAULTS) || inir_is_checked(hDlg, IDC_INIR_STALE)) {
		inir_scan_inventory(report, (int)sizeof(report));
	}
	SetDlgItemTextUTF8Strict(hDlg, IDC_INIR_REPORT, report);
}

static void inir_run_apply(HWND hDlg)
{
	char report[32768];
	BOOL wantsWrite;
	BOOL anySelected;
	int changed = 0;
	int changedEncoding = 0;
	int changedEol = 0;

	report[0] = '\0';
	inir_append(report, (int)sizeof(report), "INI: %s\r\n\r\n", g_inifile[0] ? g_inifile : "(none)");
	if (!g_inifile[0]) {
		inir_append(report, (int)sizeof(report), "Apply aborted: ini path is empty.\r\n");
		SetDlgItemTextUTF8Strict(hDlg, IDC_INIR_REPORT, report);
		return;
	}

	anySelected = inir_is_checked(hDlg, IDC_INIR_ENCODING) || inir_is_checked(hDlg, IDC_INIR_EOL)
		|| inir_is_checked(hDlg, IDC_INIR_DEFAULTS) || inir_is_checked(hDlg, IDC_INIR_STALE);
	wantsWrite = anySelected;
	if (wantsWrite && !inir_make_backup(report, (int)sizeof(report))) {
		SetDlgItemTextUTF8Strict(hDlg, IDC_INIR_REPORT, report);
		return;
	}

	tc_ini_utf8_clear_cache();
	if (anySelected && inir_apply_encoding(report, (int)sizeof(report), &changedEncoding)) changed += changedEncoding;
	if (inir_is_checked(hDlg, IDC_INIR_EOL) && inir_apply_eol(report, (int)sizeof(report), &changedEol)) changed += changedEol;
	if (inir_is_checked(hDlg, IDC_INIR_DEFAULTS)) changed += inir_apply_defaults(report, (int)sizeof(report));
	if (inir_is_checked(hDlg, IDC_INIR_STALE)) changed += inir_apply_obsolete(report, (int)sizeof(report));
	if (anySelected) changed += inir_apply_runtime_normalize(report, (int)sizeof(report));
	inir_append(report, (int)sizeof(report), "\r\nResult: %s\r\n", changed ? "changes applied; restart recommended." : "no changes.");
	SetDlgItemTextUTF8Strict(hDlg, IDC_INIR_REPORT, report);
}

static INT_PTR CALLBACK DlgProcIniRecovery(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message) {
	case WM_INITDIALOG:
		CheckDlgButton(hDlg, IDC_INIR_ENCODING, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_INIR_EOL, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_INIR_DEFAULTS, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_INIR_STALE, BST_CHECKED);
		inir_run_scan(hDlg);
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_INIR_SCAN:
			inir_run_scan(hDlg);
			return TRUE;
		case IDC_INIR_APPLY:
			inir_run_apply(hDlg);
			return TRUE;
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void inir_show(HWND hwndOwner)
{
	DialogBoxParam(GetLangModule(),
		MAKEINTRESOURCE(Language_Offset + IDD_INI_RECOVERY),
		hwndOwner, DlgProcIniRecovery, 0);
}
