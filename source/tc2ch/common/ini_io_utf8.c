#include "ini_io_utf8.h"

#include "text_file_utf8.h"
#include "text_codec.h"

typedef struct tc_dynbuf_s {
    char* p;
    DWORD len;
    DWORD cap;
} tc_dynbuf_t;

static const char* tc_section_or_main(const char* section)
{
    return (section && section[0]) ? section : "Main";
}

static int tc_tolower_ascii(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static BOOL tc_ieq_ascii_n(const char* a, int an, const char* b)
{
    int i = 0;
    if (!a || !b) return FALSE;
    while (b[i]) {
        if (i >= an) return FALSE;
        if (tc_tolower_ascii((unsigned char)a[i]) != tc_tolower_ascii((unsigned char)b[i])) return FALSE;
        i++;
    }
    return (i == an) ? TRUE : FALSE;
}

static void tc_trim_lr(const char* s, int n, int* l, int* r)
{
    int a = 0;
    int b = n;
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    *l = a;
    *r = b;
}

static BOOL tc_line_is_section(const char* s, int n, const char* wantSec)
{
    int l;
    int r;
    tc_trim_lr(s, n, &l, &r);
    if (r - l < 3) return FALSE;
    if (s[l] != '[' || s[r - 1] != ']') return FALSE;
    return tc_ieq_ascii_n(s + l + 1, r - l - 2, wantSec);
}

static BOOL tc_line_is_any_section(const char* s, int n)
{
    int l;
    int r;
    tc_trim_lr(s, n, &l, &r);
    if (r - l < 3) return FALSE;
    return (s[l] == '[' && s[r - 1] == ']') ? TRUE : FALSE;
}

static BOOL tc_line_key_match(const char* s, int n, const char* key)
{
    int i;
    int l;
    int r;
    if (!key || !key[0]) return FALSE;
    tc_trim_lr(s, n, &l, &r);
    if (l >= r) return FALSE;
    if (s[l] == ';' || s[l] == '#') return FALSE;
    for (i = l; i < r; i++) {
        if (s[i] == '=') {
            int kl;
            int kr;
            tc_trim_lr(s + l, i - l, &kl, &kr);
            return tc_ieq_ascii_n(s + l + kl, kr - kl, key);
        }
    }
    return FALSE;
}

static BOOL tc_buf_init(tc_dynbuf_t* b)
{
    b->p = (char*)HeapAlloc(GetProcessHeap(), 0, 1);
    if (!b->p) return FALSE;
    b->p[0] = '\0';
    b->len = 0;
    b->cap = 1;
    return TRUE;
}

static BOOL tc_buf_reserve(tc_dynbuf_t* b, DWORD need)
{
    DWORD cap;
    char* p;
    if (need <= b->cap) return TRUE;
    cap = b->cap;
    while (cap < need) {
        if (cap > 0x7FFFFFFF / 2) return FALSE;
        cap *= 2;
    }
    p = (char*)HeapReAlloc(GetProcessHeap(), 0, b->p, cap);
    if (!p) return FALSE;
    b->p = p;
    b->cap = cap;
    return TRUE;
}

static BOOL tc_buf_add(tc_dynbuf_t* b, const char* src, DWORD n)
{
    if (!src || n == 0) return TRUE;
    if (n > 0x7FFFFFFF) return FALSE;
    if (b->len > (DWORD)(0x7FFFFFFF - (int)n - 1)) return FALSE;
    if (!tc_buf_reserve(b, b->len + n + 1)) return FALSE;
    CopyMemory(b->p + b->len, src, n);
    b->len += n;
    b->p[b->len] = '\0';
    return TRUE;
}

static BOOL tc_buf_adds(tc_dynbuf_t* b, const char* s)
{
    return tc_buf_add(b, s, (DWORD)lstrlen(s));
}

static void tc_copy_str(char* dst, int dstSize, const char* src)
{
    int i = 0;
    if (!dst || dstSize <= 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < dstSize - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void tc_buf_free(tc_dynbuf_t* b)
{
    if (b->p) HeapFree(GetProcessHeap(), 0, b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static BOOL tc_ansi_to_utf8_compat(const char* ansi, char* outUtf8, int outBytes)
{
    WCHAR w[2048];
    if (!ansi || !outUtf8 || outBytes <= 0) return FALSE;
    if (tc_ansi_to_utf16(GetACP(), ansi, w, (int)(sizeof(w) / sizeof(w[0]))) <= 0) return FALSE;
    return (tc_utf16_to_utf8(w, outUtf8, outBytes) > 0) ? TRUE : FALSE;
}

static BOOL tc_is_valid_utf8_bytes(const char* s)
{
    WCHAR w[2048];
    if (!s) return FALSE;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, (int)(sizeof(w) / sizeof(w[0]))) > 0;
}

static DWORD tc_hash_path_ci(const char* s)
{
    DWORD h = 2166136261u;
    while (s && *s) {
        unsigned char c = (unsigned char)*s++;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

static HANDLE tc_ini_lock_enter(const char* iniPath)
{
    char name[96];
    HANDLE h;
    DWORD w;

    wsprintf(name, "Local\\TClockIniUtf8Lock_%08X", (unsigned int)tc_hash_path_ci(iniPath));
    h = CreateMutex(NULL, FALSE, name);
    if (!h) return NULL;

    w = WaitForSingleObject(h, 5000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        CloseHandle(h);
        return NULL;
    }
    return h;
}

static void tc_ini_lock_leave(HANDLE h)
{
    if (!h) return;
    ReleaseMutex(h);
    CloseHandle(h);
}

static BOOL tc_ini_utf8_rewrite_key(const char* iniPath, const char* text, DWORD size,
                                    const char* section, const char* key, const char* utf8Value,
                                    char** outText, DWORD* outSize)
{
    DWORD i = 0;
    DWORD iter = 0;
    BOOL inTarget = FALSE;
    BOOL sectionSeen = FALSE;
    BOOL keyDone = FALSE;
    const char* sec = tc_section_or_main(section);
    const char* eol = "\r\n";
    tc_dynbuf_t out;
    UNREFERENCED_PARAMETER(iniPath);

    if (!tc_buf_init(&out)) return FALSE;
    if (!tc_buf_reserve(&out, size + (DWORD)lstrlen(sec) + (DWORD)lstrlen(key) + (DWORD)lstrlen(utf8Value) + 256)) {
        tc_buf_free(&out);
        return FALSE;
    }
    while (i < size) {
        DWORD ls = i;
        DWORD txtEnd;
        DWORD le = i;
        DWORD nlLen = 0;
        BOOL isTargetSection;
        BOOL isAnySection;
        BOOL nextInTarget;

        if (++iter > size + 8) {
            tc_buf_free(&out);
            return FALSE;
        }

        while (le < size && text[le] != '\r' && text[le] != '\n') le++;
        txtEnd = le;

        if (le < size && text[le] == '\r') {
            le++;
            if (le < size && text[le] == '\n') le++;
            nlLen = le - txtEnd;
            eol = "\r\n";
        }
        else if (le < size && text[le] == '\n') {
            le++;
            nlLen = le - txtEnd;
            eol = "\n";
        }

        if (le <= i || le > size) {
            tc_buf_free(&out);
            return FALSE;
        }

        isTargetSection = tc_line_is_section(text + ls, (int)(txtEnd - ls), sec);
        isAnySection = isTargetSection ? TRUE : tc_line_is_any_section(text + ls, (int)(txtEnd - ls));
        nextInTarget = inTarget;

        if (isTargetSection) {
            sectionSeen = TRUE;
            nextInTarget = TRUE;
            if (inTarget && !keyDone) {
                if (!tc_buf_adds(&out, key) || !tc_buf_adds(&out, "=") || !tc_buf_adds(&out, utf8Value) || !tc_buf_adds(&out, eol)) {
                    tc_buf_free(&out);
                    return FALSE;
                }
                keyDone = TRUE;
            }
        }
        else if (isAnySection) {
            if (inTarget && !keyDone) {
                if (!tc_buf_adds(&out, key) || !tc_buf_adds(&out, "=") || !tc_buf_adds(&out, utf8Value) || !tc_buf_adds(&out, eol)) {
                    tc_buf_free(&out);
                    return FALSE;
                }
                keyDone = TRUE;
            }
            nextInTarget = FALSE;
        }

        if (inTarget && !isAnySection && !keyDone && tc_line_key_match(text + ls, (int)(txtEnd - ls), key)) {
            if (!tc_buf_adds(&out, key) || !tc_buf_adds(&out, "=") || !tc_buf_adds(&out, utf8Value)) {
                tc_buf_free(&out);
                return FALSE;
            }
            if (nlLen > 0 && !tc_buf_add(&out, text + txtEnd, nlLen)) {
                tc_buf_free(&out);
                return FALSE;
            }
            keyDone = TRUE;
        }
        else {
            if (!tc_buf_add(&out, text + ls, le - ls)) {
                tc_buf_free(&out);
                return FALSE;
            }
        }

        inTarget = nextInTarget;
        i = le;
    }

    if (sectionSeen && inTarget && !keyDone) {
        if (out.len > 0 && out.p[out.len - 1] != '\n') {
            if (!tc_buf_adds(&out, eol)) {
                tc_buf_free(&out);
                return FALSE;
            }
        }
        if (!tc_buf_adds(&out, key) || !tc_buf_adds(&out, "=") || !tc_buf_adds(&out, utf8Value) || !tc_buf_adds(&out, eol)) {
            tc_buf_free(&out);
            return FALSE;
        }
        keyDone = TRUE;
    }

    if (!sectionSeen) {
        if (out.len > 0 && out.p[out.len - 1] != '\n') {
            if (!tc_buf_adds(&out, eol)) {
                tc_buf_free(&out);
                return FALSE;
            }
        }
        if (!tc_buf_adds(&out, "[") || !tc_buf_adds(&out, sec) || !tc_buf_adds(&out, "]") || !tc_buf_adds(&out, eol) ||
            !tc_buf_adds(&out, key) || !tc_buf_adds(&out, "=") || !tc_buf_adds(&out, utf8Value) || !tc_buf_adds(&out, eol)) {
            tc_buf_free(&out);
            return FALSE;
        }
        keyDone = TRUE;
    }

    if (!keyDone) {
        tc_buf_free(&out);
        return FALSE;
    }

    *outText = out.p;
    *outSize = out.len;
    return TRUE;
}

static BOOL tc_ini_utf8_rewrite_delete(const char* text, DWORD size,
                                       const char* section, const char* key, BOOL deleteSection,
                                       char** outText, DWORD* outSize)
{
    DWORD i = 0;
    BOOL inTarget = FALSE;
    const char* sec = tc_section_or_main(section);
    tc_dynbuf_t out;

    if (!tc_buf_init(&out)) return FALSE;

    while (i < size) {
        DWORD ls = i;
        DWORD le = i;
        DWORD txtEnd;
        BOOL isTargetSection;
        BOOL isAnySection;

        while (le < size && text[le] != '\r' && text[le] != '\n') le++;
        txtEnd = le;
        if (le < size && text[le] == '\r') {
            le++;
            if (le < size && text[le] == '\n') le++;
        }
        else if (le < size && text[le] == '\n') {
            le++;
        }

        isTargetSection = tc_line_is_section(text + ls, (int)(txtEnd - ls), sec);
        isAnySection = tc_line_is_any_section(text + ls, (int)(txtEnd - ls));
        if (isTargetSection) {
            inTarget = TRUE;
            if (deleteSection) {
                i = le;
                continue;
            }
        }
        else if (isAnySection) {
            inTarget = FALSE;
        }

        if (deleteSection && inTarget) {
            i = le;
            continue;
        }
        if (!deleteSection && inTarget && key && key[0] &&
            tc_line_key_match(text + ls, (int)(txtEnd - ls), key)) {
            i = le;
            continue;
        }

        if (!tc_buf_add(&out, text + ls, le - ls)) {
            tc_buf_free(&out);
            return FALSE;
        }
        i = le;
    }

    *outText = out.p;
    *outSize = out.len;
    return TRUE;
}

static int tc_count_literal(const char* s, const char* token)
{
    int n = 0;
    int i = 0;
    int tlen;
    if (!s || !token || !token[0]) return 0;
    tlen = lstrlen(token);
    while (s[i]) {
        int j = 0;
        while (j < tlen && s[i + j] && s[i + j] == token[j]) j++;
        if (j == tlen) {
            n++;
            i += tlen;
            continue;
        }
        i++;
    }
    return n;
}

static BOOL tc_file_has_utf8_bom(const char* path, BOOL* hasBom)
{
    HANDLE h;
    DWORD rd = 0;
    unsigned char bom[3];
    if (hasBom) *hasBom = FALSE;
    if (!path) return FALSE;

    h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!ReadFile(h, bom, 3, &rd, NULL)) {
        CloseHandle(h);
        return FALSE;
    }
    CloseHandle(h);
    if (rd == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        if (hasBom) *hasBom = TRUE;
    }
    return TRUE;
}

BOOL tc_ini_utf8_detect_file(const char* iniPath, BOOL* isUtf8, BOOL* hasBom)
{
    char* text = NULL;
    DWORD size = 0;
    BOOL bom = FALSE;

    if (!iniPath) return FALSE;
    if (isUtf8) *isUtf8 = FALSE;
    if (hasBom) *hasBom = FALSE;

    /* Prefer BOM as UTF-8 intent marker even if legacy bytes polluted the body. */
    if (tc_file_has_utf8_bom(iniPath, &bom) && bom) {
        if (isUtf8) *isUtf8 = TRUE;
        if (hasBom) *hasBom = TRUE;
        return TRUE;
    }

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &bom)) {
        return FALSE;
    }

    tc_free_text_buffer(text);
    if (isUtf8) *isUtf8 = TRUE;
    if (hasBom) *hasBom = bom;
    return TRUE;
}

static BOOL tc_ini_utf8_find_value(const char* text, DWORD size,
                                   const char* section, const char* key,
                                   char* outUtf8, int outUtf8Bytes)
{
    DWORD i = 0;
    BOOL inTarget = FALSE;
    const char* sec = tc_section_or_main(section);

    if (!text || !key || !key[0] || !outUtf8 || outUtf8Bytes <= 0) return FALSE;
    outUtf8[0] = '\0';

    while (i < size) {
        DWORD ls = i;
        DWORD le = i;
        DWORD txtEnd;
        DWORD j;

        while (le < size && text[le] != '\r' && text[le] != '\n') le++;
        txtEnd = le;
        if (le < size && text[le] == '\r') {
            le++;
            if (le < size && text[le] == '\n') le++;
        }
        else if (le < size && text[le] == '\n') {
            le++;
        }

        if (tc_line_is_section(text + ls, (int)(txtEnd - ls), sec)) {
            inTarget = TRUE;
            i = le;
            continue;
        }
        if (tc_line_is_any_section(text + ls, (int)(txtEnd - ls))) {
            inTarget = FALSE;
            i = le;
            continue;
        }

        if (inTarget && tc_line_key_match(text + ls, (int)(txtEnd - ls), key)) {
            int l = (int)ls;
            int r = (int)txtEnd;
            int vl;
            int vr;
            for (j = ls; j < txtEnd; j++) {
                if (text[j] == '=') {
                    l = (int)j + 1;
                    break;
                }
            }
            tc_trim_lr(text + l, r - l, &vl, &vr);
            if (vr - vl <= 0) {
                outUtf8[0] = '\0';
                return TRUE;
            }
            if (vr - vl >= outUtf8Bytes) {
                vr = vl + outUtf8Bytes - 1;
            }
            CopyMemory(outUtf8, text + l + vl, (SIZE_T)(vr - vl));
            outUtf8[vr - vl] = '\0';
            return TRUE;
        }
        i = le;
    }
    return FALSE;
}

int tc_ini_utf8_read_string(const char* iniPath, const char* section, const char* key,
                            const char* defval, char* outVal, int outSize)
{
    HANDLE hLock = NULL;
    char* text = NULL;
    DWORD size = 0;
    BOOL hadBom = FALSE;
    int r = 0;

    if (!iniPath || !key || !outVal || outSize <= 0) return 0;
    outVal[0] = '\0';

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) {
        goto fallback;
    }

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &hadBom)) {
        goto fallback_locked;
    }

    if (tc_ini_utf8_find_value(text, size, section, key, outVal, outSize)) {
        r = lstrlen(outVal);
        goto cleanup;
    }

fallback_locked:
    if (defval && defval[0]) {
        tc_copy_str(outVal, outSize, defval);
        r = lstrlen(outVal);
    }
    goto cleanup;

fallback:
    if (defval && defval[0]) {
        tc_copy_str(outVal, outSize, defval);
        r = lstrlen(outVal);
    }

cleanup:
    if (text) tc_free_text_buffer(text);
    if (hLock) tc_ini_lock_leave(hLock);
    return r;
}

BOOL tc_ini_utf8_write_string(const char* iniPath, const char* section, const char* key,
                              const char* val)
{
    HANDLE hLock = NULL;
    char* text = NULL;
    char* outText = NULL;
    DWORD size = 0;
    DWORD outSize = 0;
    BOOL hadBom = TRUE;
    char utf8Val[4096];
    BOOL ok = FALSE;
    if (!iniPath || !key || !key[0]) return FALSE;
    if (!val) val = "";
    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) {
        return FALSE;
    }

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &hadBom)) {
        goto cleanup;
    }

    if (tc_is_valid_utf8_bytes(val)) {
        tc_copy_str(utf8Val, (int)sizeof(utf8Val), val);
    }
    else if (!tc_ansi_to_utf8_compat(val, utf8Val, (int)sizeof(utf8Val))) {
        tc_copy_str(utf8Val, (int)sizeof(utf8Val), val);
    }

    if (!tc_ini_utf8_rewrite_key(iniPath, text, size, section, key, utf8Val, &outText, &outSize)) {
        goto cleanup;
    }

    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) {
        goto cleanup;
    }
    ok = TRUE;

cleanup:
    if (text) tc_free_text_buffer(text);
    if (outText) HeapFree(GetProcessHeap(), 0, outText);
    tc_ini_lock_leave(hLock);
    return ok;
}

BOOL tc_ini_utf8_delete_key(const char* iniPath, const char* section, const char* key)
{
    HANDLE hLock = NULL;
    char* text = NULL;
    char* outText = NULL;
    DWORD size = 0;
    DWORD outSize = 0;
    BOOL hadBom = TRUE;
    BOOL ok = FALSE;

    if (!iniPath || !key || !key[0]) return FALSE;

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) return FALSE;

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &hadBom)) goto cleanup;
    if (!tc_ini_utf8_rewrite_delete(text, size, section, key, FALSE, &outText, &outSize)) goto cleanup;
    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) goto cleanup;
    ok = TRUE;

cleanup:
    if (text) tc_free_text_buffer(text);
    if (outText) HeapFree(GetProcessHeap(), 0, outText);
    tc_ini_lock_leave(hLock);
    return ok;
}

BOOL tc_ini_utf8_delete_section(const char* iniPath, const char* section)
{
    HANDLE hLock = NULL;
    char* text = NULL;
    char* outText = NULL;
    DWORD size = 0;
    DWORD outSize = 0;
    BOOL hadBom = TRUE;
    BOOL ok = FALSE;

    if (!iniPath) return FALSE;

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) return FALSE;

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &hadBom)) goto cleanup;
    if (!tc_ini_utf8_rewrite_delete(text, size, section, NULL, TRUE, &outText, &outSize)) goto cleanup;
    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) goto cleanup;
    ok = TRUE;

cleanup:
    if (text) tc_free_text_buffer(text);
    if (outText) HeapFree(GetProcessHeap(), 0, outText);
    tc_ini_lock_leave(hLock);
    return ok;
}

BOOL tc_ini_utf8_selfcheck(void)
{
    char tmpPath[MAX_PATH];
    char tmpFile[MAX_PATH];
    char* outText = NULL;
    DWORD outSize = 0;
    BOOL hadBom = FALSE;
    const char* seed = "[Main]\r\nDebugLog=0\r\n[Misc]\r\nA=1\r\n";
    const char* seed2 = "[Color_Font]\r\nFont=\"MS Gothic\"\r\n[Misc]\r\nA=1\r\n[Main]\r\nDebugLog=0\r\n";
    BOOL ok = FALSE;
    int pass;

    if (!tc_text_file_utf8_selfcheck()) return FALSE;
    if (!GetTempPath((DWORD)sizeof(tmpPath), tmpPath)) return FALSE;
    if (!GetTempFileName(tmpPath, "tiu", 0, tmpFile)) return FALSE;

    for (pass = 0; pass < 2; pass++) {
        BOOL writeBom = (pass == 0) ? TRUE : FALSE;

        if (!tc_write_text_file_utf8(tmpFile, seed, (DWORD)lstrlen(seed), writeBom)) goto cleanup;
        if (!tc_ini_utf8_write_string(tmpFile, NULL, "DebugLog", "1")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if ((writeBom ? TRUE : FALSE) != (hadBom ? TRUE : FALSE)) goto cleanup;
        if (tc_count_literal(outText, "[Main]") != 1) goto cleanup;
        if (tc_count_literal(outText, "DebugLog=1") != 1) goto cleanup;
        if (tc_count_literal(outText, "DebugLog=0") != 0) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;

        if (!tc_ini_utf8_delete_key(tmpFile, NULL, "DebugLog")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if (tc_count_literal(outText, "[Main]") != 1) goto cleanup;
        if (tc_count_literal(outText, "DebugLog=") != 0) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;

        /* section is not always first: verify rewrite/delete on non-Main sections as well */
        if (!tc_write_text_file_utf8(tmpFile, seed2, (DWORD)lstrlen(seed2), writeBom)) goto cleanup;
        if (!tc_ini_utf8_write_string(tmpFile, "Color_Font", "Font", "\"Meiryo UI\"")) goto cleanup;
        if (!tc_ini_utf8_write_string(tmpFile, "Misc", "A", "2")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if (tc_count_literal(outText, "[Color_Font]") != 1) goto cleanup;
        if (tc_count_literal(outText, "[Misc]") != 1) goto cleanup;
        if (tc_count_literal(outText, "[Main]") != 1) goto cleanup;
        if (tc_count_literal(outText, "Font=\"Meiryo UI\"") != 1) goto cleanup;
        if (tc_count_literal(outText, "A=2") != 1) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;

        if (!tc_ini_utf8_delete_section(tmpFile, "Misc")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if (tc_count_literal(outText, "[Misc]") != 0) goto cleanup;
        if (tc_count_literal(outText, "[Color_Font]") != 1) goto cleanup;
        if (tc_count_literal(outText, "[Main]") != 1) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;
    }

    ok = TRUE;

cleanup:
    if (outText) tc_free_text_buffer(outText);
    DeleteFile(tmpFile);
    return ok;
}
