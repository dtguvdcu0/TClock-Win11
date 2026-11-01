#include "ini_io_utf8.h"

#include "text_file_utf8.h"
#include "text_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


typedef struct tc_dynbuf_s {
    char* p;
    DWORD len;
    DWORD cap;
} tc_dynbuf_t;

typedef struct tc_ini_utf8_cache_s {
    BOOL valid;
    char path[MAX_PATH];
    FILETIME ftWrite;
    DWORD fileSizeLow;
    BOOL hadBom;
    char* text;
    DWORD size;
} tc_ini_utf8_cache_t;

static tc_ini_utf8_cache_t g_iniUtf8Cache;


static BOOL tc_bytes_equal(const char* a, const char* b, DWORD n)
{
    DWORD i;
    if (!a || !b) return FALSE;
    for (i = 0; i < n; ++i) {
        if ((unsigned char)a[i] != (unsigned char)b[i]) return FALSE;
    }
    return TRUE;
}

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
    while (a < b && (s[a] == ' ' || s[a] == '	')) a++;
    /* Tolerate accidental UTF-8 BOM prefix at line start to avoid duplicate section creation. */
    if (a + 2 < b &&
        (unsigned char)s[a] == 0xEF &&
        (unsigned char)s[a + 1] == 0xBB &&
        (unsigned char)s[a + 2] == 0xBF) {
        a += 3;
    }
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '	')) b--;
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

static void tc_normalize_ini_name(const char* src, const char* fallback, char* out, int outBytes)
{
    const char* p;
    const char* e;
    int n = 0;

    if (!out || outBytes <= 0) return;
    out[0] = '\0';

    if (!src || !src[0]) {
        if (fallback) tc_copy_str(out, outBytes, fallback);
        return;
    }

    p = src;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    e = p + lstrlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;

    while (p < e && n < outBytes - 1) {
        unsigned char c = (unsigned char)*p++;
        if (c < 0x20) continue;
        out[n++] = (char)c;
    }
    out[n] = '\0';

    if (n == 0 && fallback) tc_copy_str(out, outBytes, fallback);
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
    if (tc_ansi_to_utf16(0, ansi, w, (int)(sizeof(w) / sizeof(w[0]))) <= 0) return FALSE;
    return (tc_utf16_to_utf8(w, outUtf8, outBytes) > 0) ? TRUE : FALSE;
}

static BOOL tc_is_valid_utf8_bytes(const char* s)
{
    WCHAR w[2048];
    if (!s) return FALSE;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, (int)(sizeof(w) / sizeof(w[0]))) > 0;
}

static DWORD tc_hash_wide(const wchar_t* path)
{
    DWORD hash = 2166136261u;
    while (path && *path) {
        unsigned int value = (unsigned int)*path++;
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

static volatile LONG g_iniReadGeneration;

static void ini_clear_data(void)
{
    if (g_iniUtf8Cache.text) {
        tc_free_text_buffer(g_iniUtf8Cache.text);
        g_iniUtf8Cache.text = NULL;
    }
    ZeroMemory(&g_iniUtf8Cache, sizeof(g_iniUtf8Cache));
}

void tc_ini_utf8_clear_cache(void)
{
    InterlockedIncrement(&g_iniReadGeneration);
    ini_clear_data();
}

static int tc_ini_utf8_parse_section_multisz(const char* text, DWORD size, const char* sec,
                                             char* outBuf, int outBytes, BOOL* truncated)
{
    DWORD i = 0;
    int pos = 0;
    int count = 0;
    BOOL inTarget = FALSE;

    if (!text || !sec || !outBuf || outBytes <= 1) return 0;
    if (truncated) *truncated = FALSE;
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
        if (!inTarget) {
            i = le;
            continue;
        }

        tc_trim_lr(text + ls, (int)(txtEnd - ls), &ll, &rr);
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
                tc_trim_lr(line, eq, &kl, &kr);
                tc_trim_lr(line + eq + 1, lineLen - (eq + 1), &vl, &vr);
                keyLen = kr - kl;
                valLen = vr - vl;
                if (keyLen > 0) {
                    need = keyLen + 1 + (valLen > 0 ? valLen : 0) + 1;
                    if (pos + need + 1 >= outBytes) {
                        if (truncated) *truncated = TRUE;
                        break;
                    }
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

static BOOL tc_ini_utf8_get_file_stamp(const char* path, FILETIME* ftWrite, DWORD* fileSizeLow)
{
    HANDLE h;
    DWORD szLow;
    if (!path || !ftWrite || !fileSizeLow) return FALSE;
    {
        wchar_t wPath[MAX_PATH];
        if (tc_utf8_to_utf16(path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) {
            if (tc_ansi_to_utf16_compat(0, path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) return FALSE;
        }
        h = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileTime(h, NULL, NULL, ftWrite)) {
        CloseHandle(h);
        return FALSE;
    }
    szLow = GetFileSize(h, NULL);
    CloseHandle(h);
    if (szLow == INVALID_FILE_SIZE) return FALSE;
    *fileSizeLow = szLow;
    return TRUE;
}

static BOOL tc_ini_utf8_cache_get_locked(const char* iniPath, char** text, DWORD* size, BOOL* hadBom)
{
    FILETIME ftWrite;
    DWORD fileSizeLow = 0;
    char* readText = NULL;
    DWORD readSize = 0;
    BOOL readBom = FALSE;

    if (!iniPath || !text || !size || !hadBom) return FALSE;
    *text = NULL;
    *size = 0;
    *hadBom = FALSE;

    if (!tc_ini_utf8_get_file_stamp(iniPath, &ftWrite, &fileSizeLow)) {
        return FALSE;
    }

    if (g_iniUtf8Cache.valid &&
        g_iniUtf8Cache.text &&
        lstrcmpi(g_iniUtf8Cache.path, iniPath) == 0 &&
        CompareFileTime(&g_iniUtf8Cache.ftWrite, &ftWrite) == 0 &&
        g_iniUtf8Cache.fileSizeLow == fileSizeLow) {
        *text = g_iniUtf8Cache.text;
        *size = g_iniUtf8Cache.size;
        *hadBom = g_iniUtf8Cache.hadBom;
        return TRUE;
    }

    if (!tc_read_text_file_utf8(iniPath, &readText, &readSize, &readBom)) {
        return FALSE;
    }

    ini_clear_data();
    lstrcpyn(g_iniUtf8Cache.path, iniPath, (int)sizeof(g_iniUtf8Cache.path));
    g_iniUtf8Cache.ftWrite = ftWrite;
    g_iniUtf8Cache.fileSizeLow = fileSizeLow;
    g_iniUtf8Cache.hadBom = readBom;
    g_iniUtf8Cache.text = readText;
    g_iniUtf8Cache.size = readSize;
    g_iniUtf8Cache.valid = TRUE;

    *text = g_iniUtf8Cache.text;
    *size = g_iniUtf8Cache.size;
    *hadBom = g_iniUtf8Cache.hadBom;
    return TRUE;
}

static HANDLE tc_lock_wide(const wchar_t* iniPath)
{
    wchar_t name[96];
    HANDLE handle;
    DWORD wait;
    swprintf_s(name, _countof(name), L"Local\\TClockIniUtf8Lock_%08X", (unsigned int)tc_hash_wide(iniPath));
    handle = CreateMutexW(NULL, FALSE, name);
    if (!handle) return NULL;
    wait = WaitForSingleObject(handle, 5000);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        CloseHandle(handle);
        SetLastError(ERROR_TIMEOUT);
        return NULL;
    }
    return handle;
}

static HANDLE tc_ini_lock_enter(const char* iniPath)
{
    wchar_t path[MAX_PATH];
    /* Existing serialized path boundary; all lock names and new file operations are wide. */
    if (!tc_path_utf8_or_ansi_to_utf16(iniPath, path, _countof(path))) return NULL;
    return tc_lock_wide(path);
}

static void tc_ini_lock_leave(HANDLE h)
{
    if (!h) return;
    ReleaseMutex(h);
    CloseHandle(h);
}

typedef struct tc_ini_index_entry_s {
    DWORD secOff;
    DWORD secLen;
    DWORD keyOff;
    DWORD keyLen;
    DWORD valOff;
    DWORD valLen;
} ini_index_entry_t;

typedef struct ini_read_session_s {
    BOOL active;
    BOOL legacy;
    int depth;
    wchar_t path[MAX_PATH];
    char* text;
    DWORD size;
    BOOL hadBom;
    DWORD* slots;
    DWORD slotCount;
    ini_index_entry_t* entries;
    DWORD entryCount;
    LONG generation;
} ini_read_session_t;

static DWORD g_iniReadTlsSlot = TLS_OUT_OF_INDEXES;
static LONG g_iniReadActiveScopes;
static SRWLOCK g_iniReadTlsLock = SRWLOCK_INIT;

static ini_read_session_t* ini_get_session(void)
{
    ini_read_session_t* session=NULL;
    AcquireSRWLockShared(&g_iniReadTlsLock);
    if (g_iniReadTlsSlot!=TLS_OUT_OF_INDEXES) session=(ini_read_session_t*)TlsGetValue(g_iniReadTlsSlot);
    ReleaseSRWLockShared(&g_iniReadTlsLock);
    return session;
}

static BOOL ini_install_session(ini_read_session_t* session)
{
    BOOL allocated=FALSE,ok=FALSE;
    AcquireSRWLockExclusive(&g_iniReadTlsLock);
    if (g_iniReadTlsSlot==TLS_OUT_OF_INDEXES) {
        g_iniReadTlsSlot=TlsAlloc();
        if (g_iniReadTlsSlot!=TLS_OUT_OF_INDEXES) allocated=TRUE;
    }
    if (g_iniReadTlsSlot!=TLS_OUT_OF_INDEXES && TlsSetValue(g_iniReadTlsSlot,session)) {
        ++g_iniReadActiveScopes;
        ok=TRUE;
    }
    else if (allocated) {
        TlsFree(g_iniReadTlsSlot);
        g_iniReadTlsSlot=TLS_OUT_OF_INDEXES;
    }
    ReleaseSRWLockExclusive(&g_iniReadTlsLock);
    return ok;
}

static BOOL ini_equal_span(const char* a,const char* b,DWORD n)
{
    DWORD i;
    for (i=0;i<n;++i) if (tc_tolower_ascii((unsigned char)a[i])!=tc_tolower_ascii((unsigned char)b[i])) return FALSE;
    return TRUE;
}

static DWORD ini_hash_pair(const char* sec, int secLen, const char* key, int keyLen)
{
    DWORD h = 2166136261u;
    int i;
    for (i = 0; i < secLen; ++i) h = (h ^ (DWORD)tc_tolower_ascii((unsigned char)sec[i])) * 16777619u;
    h = (h ^ 0xFFu) * 16777619u;
    for (i = 0; i < keyLen; ++i) h = (h ^ (DWORD)tc_tolower_ascii((unsigned char)key[i])) * 16777619u;
    return h ? h : 1u;
}

static BOOL ini_build_index(ini_read_session_t* s)
{
    DWORD i = 0, lineCount = 1, slots = 8;
    BOOL inSection = FALSE;
    DWORD secOff = 0, secLen = 4;
    if (!s || !s->text) return FALSE;
    for (i = 0; i < s->size; ++i) {
        if (s->text[i] == '\r') { ++lineCount; if (i+1<s->size && s->text[i+1]=='\n') ++i; }
        else if (s->text[i] == '\n') ++lineCount;
    }
    if (lineCount > 0x1FFFFFFFu) return FALSE;
    while (slots < lineCount * 2u) { if (slots > 0x40000000u) return FALSE; slots <<= 1; }
    s->slots=(DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)slots*sizeof(DWORD));
    s->entries=(ini_index_entry_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)lineCount*sizeof(ini_index_entry_t));
    if (!s->slots || !s->entries) return FALSE;
    s->slotCount=slots;
    i=0;
    while (i<s->size) {
        DWORD ls=i, le=i, end, eq;
        int l, r, kl, kr, vl, vr;
        while (le<s->size && s->text[le]!='\r' && s->text[le]!='\n') ++le;
        end=le;
        if (le<s->size && s->text[le]=='\r') { ++le; if (le<s->size && s->text[le]=='\n') ++le; }
        else if (le<s->size && s->text[le]=='\n') ++le;
        if (tc_line_is_any_section(s->text+ls,(int)(end-ls))) {
            tc_trim_lr(s->text+ls,(int)(end-ls),&l,&r);
            inSection=TRUE; secOff=ls+(DWORD)l+1; secLen=(DWORD)(r-l-2);
            i=le; continue;
        }
        if (!inSection) { i=le; continue; }
        tc_trim_lr(s->text+ls,(int)(end-ls),&l,&r);
        if (l>=r || s->text[ls+l]==';' || s->text[ls+l]=='#') { i=le; continue; }
        eq=ls+(DWORD)l; while (eq<ls+(DWORD)r && s->text[eq]!='=') ++eq;
        if (eq>=ls+(DWORD)r) { i=le; continue; }
        tc_trim_lr(s->text+ls+l,(int)(eq-(ls+(DWORD)l)),&kl,&kr);
        if (kr<=kl) { i=le; continue; }
        tc_trim_lr(s->text+eq+1,(int)(ls+(DWORD)r-eq-1),&vl,&vr);
        {
            DWORD hash=ini_hash_pair(s->text+secOff,(int)secLen,s->text+ls+l+kl,kr-kl);
            DWORD slot=hash&(slots-1);
            BOOL found=FALSE;
            while (s->slots[slot]) {
                ini_index_entry_t* e=&s->entries[s->slots[slot]-1];
                if (e->secLen==secLen && ini_equal_span(s->text+e->secOff,s->text+secOff,secLen) &&
                    e->keyLen==(DWORD)(kr-kl) && ini_equal_span(s->text+e->keyOff,s->text+ls+l+kl,e->keyLen)) { found=TRUE; break; }
                slot=(slot+1)&(slots-1);
            }
            if (!found) {
                ini_index_entry_t* e=&s->entries[s->entryCount];
                e->secOff=secOff; e->secLen=secLen; e->keyOff=ls+(DWORD)l+(DWORD)kl; e->keyLen=(DWORD)(kr-kl);
                e->valOff=eq+1+(DWORD)vl; e->valLen=(DWORD)(vr-vl);
                s->slots[slot]=++s->entryCount;
            }
        }
        i=le;
    }
    return TRUE;
}

static void ini_release_session(ini_read_session_t* s)
{
    if (!s) return;
    if (s->text) HeapFree(GetProcessHeap(),0,s->text);
    if (s->slots) HeapFree(GetProcessHeap(),0,s->slots);
    if (s->entries) HeapFree(GetProcessHeap(),0,s->entries);
    ZeroMemory(s,sizeof(*s));
}

static BOOL tc_convert_text(const char* source, DWORD size, char** result, DWORD* bytes);

static BOOL ini_load_session(ini_read_session_t* s)
{
    HANDLE f=INVALID_HANDLE_VALUE;
    DWORD sz=0, rd=0, start=0;
    unsigned char* raw=NULL;
    BOOL ok=FALSE;
    LONG startGeneration=InterlockedCompareExchange(&g_iniReadGeneration,0,0);
    f=CreateFileW(s->path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) return FALSE;
    sz=GetFileSize(f,NULL);
    if (sz==INVALID_FILE_SIZE && GetLastError()!=NO_ERROR) goto done;
    raw=(unsigned char*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)sz+1);
    if (!raw) goto done;
    if (sz && (!ReadFile(f,raw,sz,&rd,NULL) || rd!=sz)) goto done;
    raw[rd]=0;
    if (rd>=3 && raw[0]==0xEF && raw[1]==0xBB && raw[2]==0xBF) { start=3; s->hadBom=TRUE; }
    if (rd>start) {
        if (rd-start>(DWORD)INT_MAX) goto done;
        if (memchr(raw+start,0,rd-start)) goto done;
        if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,(const char*)raw+start,(int)(rd-start),NULL,0)<=0) {
            char* converted = NULL;
            DWORD convertedBytes;
            /* Opt-in legacy reads derive UTF-8 in memory; opening a scope never migrates the file. */
            if (start || !s->legacy || !tc_convert_text((const char*)raw, rd, &converted, &convertedBytes)) goto done;
            HeapFree(GetProcessHeap(),0,raw); raw=(unsigned char*)converted; rd=convertedBytes;
        }
    }
    if (start) { memmove(raw,raw+start,rd-start); rd-=start; raw[rd]=0; }
    s->text=(char*)raw; raw=NULL; s->size=rd;
    if (!ini_build_index(s)) goto done;
    if (InterlockedCompareExchange(&g_iniReadGeneration,0,0)!=startGeneration) goto done;
    s->generation=startGeneration;
    ok=TRUE;
done:
    if (raw) HeapFree(GetProcessHeap(),0,raw);
    if (f!=INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok) {
        if (s->text) HeapFree(GetProcessHeap(),0,s->text); s->text=NULL;
        if (s->slots) HeapFree(GetProcessHeap(),0,s->slots); s->slots=NULL;
        if (s->entries) HeapFree(GetProcessHeap(),0,s->entries); s->entries=NULL;
        s->slotCount=s->entryCount=s->size=0; s->hadBom=FALSE;
    }
    return ok;
}

static BOOL ini_session_valid(const ini_read_session_t* s)
{
    return s && s->active && s->text && s->slots && s->entries;
}

static BOOL ini_ensure_session(ini_read_session_t* s)
{
    LONG now; HANDLE lock; wchar_t path[MAX_PATH]; int depth, attempt; BOOL legacy;
    if (!s || !s->active) return FALSE;
    now=InterlockedCompareExchange(&g_iniReadGeneration,0,0);
    if (now==s->generation && ini_session_valid(s)) return TRUE;
    CopyMemory(path,s->path,sizeof(path)); depth=s->depth; legacy=s->legacy;
    lock=tc_lock_wide(path);
    if (!lock) return FALSE;
    for (attempt=0;attempt<2;++attempt) {
        now=InterlockedCompareExchange(&g_iniReadGeneration,0,0);
        if (now==s->generation && ini_session_valid(s)) { tc_ini_lock_leave(lock); return TRUE; }
        ini_release_session(s); s->active=TRUE; s->depth=depth; s->legacy=legacy; CopyMemory(s->path,path,sizeof(path));
        if (!ini_load_session(s)) {
            LONG after=InterlockedCompareExchange(&g_iniReadGeneration,0,0);
            ini_release_session(s); s->active=TRUE; s->depth=depth; s->legacy=legacy; CopyMemory(s->path,path,sizeof(path)); s->generation=after;
            if (after!=now) continue;
            tc_ini_lock_leave(lock); return FALSE;
        }
        if (InterlockedCompareExchange(&g_iniReadGeneration,0,0)==s->generation && ini_session_valid(s)) {
            tc_ini_lock_leave(lock); return TRUE;
        }
    }
    {
        LONG after=InterlockedCompareExchange(&g_iniReadGeneration,0,0);
        ini_release_session(s); s->active=TRUE; s->depth=depth; s->legacy=legacy; CopyMemory(s->path,path,sizeof(path)); s->generation=after;
    }
    tc_ini_lock_leave(lock); return FALSE;
}

static ini_read_session_t* ini_match_session(const char* iniPath)
{
    wchar_t path[MAX_PATH];
    ini_read_session_t* s=ini_get_session();
    if (!s || !s->active || !iniPath) return NULL;
    if (!tc_path_utf8_or_ansi_to_utf16(iniPath,path,_countof(path))) return NULL;
    if (lstrcmpiW(path,s->path)!=0) return NULL;
    return ini_ensure_session(s) ? s : NULL;
}

static const ini_index_entry_t* ini_find_session(const ini_read_session_t* s,const char* section,const char* key)
{
    DWORD hash,slot; int secLen,keyLen;
    if (!s || !section || !key || !s->slotCount) return NULL;
    secLen=lstrlen(section); keyLen=lstrlen(key);
    hash=ini_hash_pair(section,secLen,key,keyLen); slot=hash&(s->slotCount-1);
    while (s->slots[slot]) {
        const ini_index_entry_t* e=&s->entries[s->slots[slot]-1];
        if (e->secLen==(DWORD)secLen && e->keyLen==(DWORD)keyLen &&
            tc_ieq_ascii_n(s->text+e->secOff,(int)e->secLen,section) &&
            tc_ieq_ascii_n(s->text+e->keyOff,(int)e->keyLen,key)) return e;
        slot=(slot+1)&(s->slotCount-1);
    }
    return NULL;
}

static BOOL ini_begin_scope(LPCWSTR path, BOOL legacy)
{
    HANDLE lock;
    ini_read_session_t* s;
    if (!path || !path[0]) return FALSE;
    s=ini_get_session();
    if (s && s->active) {
        if (lstrcmpiW(path,s->path)!=0 || !ini_ensure_session(s)) return FALSE;
        ++s->depth; return TRUE;
    }
    s=(ini_read_session_t*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*s));
    if (!s) return FALSE;
    lstrcpynW(s->path,path,MAX_PATH);
    if (lstrcmpiW(s->path,path)!=0) { HeapFree(GetProcessHeap(),0,s); return FALSE; }
    lock=tc_lock_wide(s->path);
    if (!lock) { HeapFree(GetProcessHeap(),0,s); return FALSE; }
    s->active=TRUE; s->depth=1; s->legacy=legacy;
    if (!ini_load_session(s)) { ini_release_session(s); tc_ini_lock_leave(lock); HeapFree(GetProcessHeap(),0,s); return FALSE; }
    tc_ini_lock_leave(lock);
    if (!ini_install_session(s)) { ini_release_session(s); HeapFree(GetProcessHeap(),0,s); return FALSE; }
    return TRUE;
}

/* Wide read boundary over the existing scoped UTF-8/legacy decoder.
   Values are literal, including quotes. Never round-trip a path through ACP.
   Missing keys and every failure preserve the caller's output. */
HRESULT tc_read_valueW(LPCWSTR path, const char* section, const char* key, WCHAR* output, UINT capacity)
{
    ini_read_session_t* session;
    const ini_index_entry_t* entry;
    WCHAR* staged=NULL;
    char secNorm[128],keyNorm[128];
    HRESULT result=E_FAIL;int needed=0,converted;
    if (!path || !path[0] || !key || !key[0] || !output || !capacity || capacity>INT_MAX)
        return E_INVALIDARG;
    if (strnlen(key,sizeof(keyNorm))>=sizeof(keyNorm) ||
        (section && strnlen(section,sizeof(secNorm))>=sizeof(secNorm))) return E_INVALIDARG;
    tc_normalize_ini_name(section,"Main",secNorm,sizeof(secNorm));
    tc_normalize_ini_name(key,NULL,keyNorm,sizeof(keyNorm));
    if (!keyNorm[0] || !tc_begin_legacyW(path)) return E_FAIL;
    session=ini_get_session();
    entry=session ? ini_find_session(session,secNorm,keyNorm) : NULL;
    if (!session) goto done;
    if (!entry) {result=S_FALSE;goto done;}
    if (entry->valLen>INT_MAX) {result=E_INVALIDARG;goto done;}
    if (entry->valLen) {
        needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,session->text+entry->valOff,(int)entry->valLen,NULL,0);
        if (!needed) {result=HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);goto done;}
    }
    if ((UINT)needed>=capacity) {result=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);goto done;}
    staged=(WCHAR*)HeapAlloc(GetProcessHeap(),0,((SIZE_T)needed+1)*sizeof(WCHAR));
    if (!staged) {result=E_OUTOFMEMORY;goto done;}
    if (needed) {
        converted=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,session->text+entry->valOff,(int)entry->valLen,staged,needed);
        if (converted!=needed) {result=HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);goto done;}
    }
    staged[needed]=0;CopyMemory(output,staged,((SIZE_T)needed+1)*sizeof(WCHAR));result=S_OK;
done:
    if (staged) HeapFree(GetProcessHeap(),0,staged);
    tc_ini_end();return result;
}

BOOL tc_ini_beginW(LPCWSTR path)
{
    return ini_begin_scope(path, FALSE);
}

BOOL tc_begin_legacyW(LPCWSTR path)
{
    return ini_begin_scope(path, TRUE);
}

void tc_ini_end(void)
{
    ini_read_session_t* s;
    DWORD slot;
    AcquireSRWLockExclusive(&g_iniReadTlsLock);
    slot=g_iniReadTlsSlot;
    s=slot==TLS_OUT_OF_INDEXES ? NULL : (ini_read_session_t*)TlsGetValue(slot);
    if (!s || !s->active) { ReleaseSRWLockExclusive(&g_iniReadTlsLock); return; }
    if (s->depth>1) { --s->depth; ReleaseSRWLockExclusive(&g_iniReadTlsLock); return; }
    if (!TlsSetValue(slot,NULL)) { ReleaseSRWLockExclusive(&g_iniReadTlsLock); return; }
    ini_release_session(s); HeapFree(GetProcessHeap(),0,s);
    if (g_iniReadActiveScopes>0) --g_iniReadActiveScopes;
    if (g_iniReadActiveScopes==0 && TlsFree(slot)) g_iniReadTlsSlot=TLS_OUT_OF_INDEXES;
    ReleaseSRWLockExclusive(&g_iniReadTlsLock);
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
    char secNorm[128];
    const char* sec;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    sec = secNorm;
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
    char secNorm[128];
    const char* sec;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    sec = secNorm;
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

    {
        wchar_t wPath[MAX_PATH];
        if (tc_utf8_to_utf16(path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) {
            if (tc_ansi_to_utf16_compat(0, path, wPath, (int)(sizeof(wPath) / sizeof(wPath[0]))) <= 0) return FALSE;
        }
        h = CreateFileW(wPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    }
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
    HANDLE hLock = NULL;
    ini_read_session_t* session;

    if (!iniPath) return FALSE;
    if (isUtf8) *isUtf8 = FALSE;
    if (hasBom) *hasBom = FALSE;

    session=ini_match_session(iniPath);
    if (session) {
        if (isUtf8) *isUtf8 = TRUE;
        if (hasBom) *hasBom = session->hadBom;
        return TRUE;
    }

    /* Prefer BOM as UTF-8 intent marker even if legacy bytes polluted the body. */
    if (tc_file_has_utf8_bom(iniPath, &bom) && bom) {
        if (isUtf8) *isUtf8 = TRUE;
        if (hasBom) *hasBom = TRUE;
        return TRUE;
    }

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) return FALSE;

    if (!tc_ini_utf8_cache_get_locked(iniPath, &text, &size, &bom)) {
        tc_ini_lock_leave(hLock);
        return FALSE;
    }
    tc_ini_lock_leave(hLock);
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
    char secNorm[128];
    const char* sec;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    sec = secNorm;

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
    char secNorm[128];
    char keyNorm[128];
    ini_read_session_t* session;

    if (!iniPath || !key || !outVal || outSize <= 0) return 0;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    tc_normalize_ini_name(key, NULL, keyNorm, (int)sizeof(keyNorm));
    if (!keyNorm[0]) return 0;
    outVal[0] = '\0';

    session=ini_match_session(iniPath);
    if (session) {
        const ini_index_entry_t* entry=ini_find_session(session,secNorm,keyNorm);
        if (entry) {
            int n=(int)entry->valLen;
            if (n>=outSize) n=outSize-1;
            if (n>0) CopyMemory(outVal,session->text+entry->valOff,(SIZE_T)n);
            outVal[n]='\0';
            return n;
        }
        if (defval && defval[0]) { tc_copy_str(outVal,outSize,defval); return lstrlen(outVal); }
        return 0;
    }

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) {
        goto fallback;
    }

    if (!tc_ini_utf8_cache_get_locked(iniPath, &text, &size, &hadBom)) {
        goto fallback_locked;
    }

    if (tc_ini_utf8_find_value(text, size, secNorm, keyNorm, outVal, outSize)) {
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
    if (hLock) tc_ini_lock_leave(hLock);
    return r;
}

int tc_ini_utf8_read_section_multisz_ex(const char* iniPath, const char* section,
                                        char* outBuf, int outBytes, BOOL* truncated)
{
    HANDLE hLock = NULL;
    char* text = NULL;
    char* readText = NULL;
    DWORD size = 0;
    BOOL hadBom = FALSE;
    BOOL isUtf8 = FALSE;
    ini_read_session_t* session;
    char secNorm[128];
    const char* sec;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    sec = secNorm;
    int count = 0;

    if (truncated) *truncated = FALSE;
    if (!outBuf || outBytes <= 1) return 0;
    outBuf[0] = '\0';
    outBuf[1] = '\0';
    if (!iniPath || !iniPath[0]) return 0;

    session=ini_match_session(iniPath);
    if (session) {
        return tc_ini_utf8_parse_section_multisz(session->text,session->size,sec,outBuf,outBytes,truncated);
    }

    hLock = tc_ini_lock_enter(iniPath);
    if (hLock && tc_ini_utf8_cache_get_locked(iniPath, &text, &size, &hadBom)) {
        UNREFERENCED_PARAMETER(hadBom);
    }
    else {
        if (tc_ini_utf8_detect_file(iniPath, &isUtf8, NULL) && isUtf8 &&
            tc_read_text_file_utf8(iniPath, &readText, &size, &hadBom)) {
            text = readText;
        }
        else {
            DWORD r = GetPrivateProfileSection(sec, outBuf, (DWORD)outBytes, iniPath);
            const char* p;
            if (hLock) tc_ini_lock_leave(hLock);
            if (r == 0) {
                outBuf[0] = '\0';
                outBuf[1] = '\0';
                return 0;
            }
            outBuf[outBytes - 1] = '\0';
            outBuf[outBytes - 2] = '\0';
            if (truncated && r == (DWORD)(outBytes - 2)) *truncated = TRUE;
            p = outBuf;
            while (*p) {
                ++count;
                p += lstrlen(p) + 1;
            }
            return count;
        }
    }

    count = tc_ini_utf8_parse_section_multisz(text, size, sec, outBuf, outBytes, truncated);
    if (readText) tc_free_text_buffer(readText);
    tc_ini_lock_leave(hLock);
    return count;
}

int tc_ini_utf8_read_section_multisz(const char* iniPath, const char* section,
                                     char* outBuf, int outBytes)
{
    return tc_ini_utf8_read_section_multisz_ex(iniPath, section, outBuf, outBytes, NULL);
}

/* Each bounded UTF-8 entry is key=value, or key alone to delete it. Publish only a complete batch. */
typedef struct tc_ini_entry_t {
    const char* line;
    DWORD length;
    DWORD ordinal;
    char key[128];
} tc_ini_entry_t;

static int tc_compare_keys(const char* a, const char* b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= '0' && ca <= '9' && cb >= '0' && cb <= '9') {
            const char *ea = a, *eb = b, *sa, *sb;
            while (*ea >= '0' && *ea <= '9') ++ea;
            while (*eb >= '0' && *eb <= '9') ++eb;
            sa = a; sb = b;
            while (sa < ea && *sa == '0') ++sa;
            while (sb < eb && *sb == '0') ++sb;
            if (ea - sa != eb - sb) return ea - sa < eb - sb ? -1 : 1;
            while (sa < ea) {
                if (*sa != *sb) return *sa < *sb ? -1 : 1;
                ++sa; ++sb;
            }
            if (ea - a != eb - b) return ea - a < eb - b ? -1 : 1;
            a = ea; b = eb; continue;
        }
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return ca < cb ? -1 : 1;
        ++a; ++b;
    }
    return *a ? 1 : (*b ? -1 : 0);
}

static int __cdecl tc_compare_entries(const void* left, const void* right)
{
    const tc_ini_entry_t *a = (const tc_ini_entry_t*)left, *b = (const tc_ini_entry_t*)right;
    int result = tc_compare_keys(a->key, b->key);
    if (result) return result;
    return a->ordinal < b->ordinal ? -1 : (a->ordinal > b->ordinal ? 1 : 0);
}

/* Canonicalize only the requested section. The first duplicate wins, matching reads. */
static BOOL tc_sort_section(const char* text, DWORD size, const char* section, char** result, DWORD* resultSize)
{
    DWORD offset = 0, start = size, stop = size, count = 0, index;
    BOOL active = FALSE, seen = FALSE, ok = FALSE;
    tc_ini_entry_t* entries = NULL;
    tc_dynbuf_t out;
    if (!tc_buf_init(&out)) return FALSE;
    while (offset < size) {
        DWORD line = offset, end;
        while (offset < size && text[offset] != '\r' && text[offset] != '\n') ++offset;
        end = offset;
        if (offset < size && text[offset] == '\r') ++offset;
        if (offset < size && text[offset] == '\n') ++offset;
        if (tc_line_is_section(text + line, (int)(end - line), section)) {
            /* Ambiguous repeated sections must never silently discard user data. */
            if (seen) goto cleanup;
            seen = active = TRUE; start = offset;
        } else if (active && tc_line_is_any_section(text + line, (int)(end - line))) {
            stop = line; active = FALSE;
        }
    }
    if (!seen) goto cleanup;
    entries = (tc_ini_entry_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        ((SIZE_T)(stop - start) / 2 + 1) * sizeof(*entries));
    if (!entries || !tc_buf_add(&out, text, start)) goto cleanup;
    offset = start;
    while (offset < stop) {
        DWORD line = offset, end, equals;
        int left, right, keyLeft, keyRight;
        while (offset < stop && text[offset] != '\r' && text[offset] != '\n') ++offset;
        end = offset;
        if (offset < stop && text[offset] == '\r') ++offset;
        if (offset < stop && text[offset] == '\n') ++offset;
        tc_trim_lr(text + line, (int)(end - line), &left, &right);
        equals = line + (DWORD)left;
        if (left < right && text[equals] != ';' && text[equals] != '#') {
            while (equals < line + (DWORD)right && text[equals] != '=') ++equals;
        } else equals = end;
        if (equals < line + (DWORD)right) {
            tc_trim_lr(text + line + left, (int)(equals - line - left), &keyLeft, &keyRight);
            if (keyRight <= keyLeft || keyRight - keyLeft >= (int)sizeof(entries[count].key)) goto cleanup;
            memcpy(entries[count].key, text + line + left + keyLeft, (size_t)(keyRight - keyLeft));
            entries[count].line = text + line; entries[count].length = end - line;
            entries[count].ordinal = count; ++count;
        } else if (!tc_buf_add(&out, text + line, offset - line)) goto cleanup;
    }
    /* Separate retained comments from keys even when the input has no final newline. */
    if (out.len && out.p[out.len - 1] != '\n' && !tc_buf_adds(&out, "\r\n")) goto cleanup;
    qsort(entries, count, sizeof(*entries), tc_compare_entries);
    for (index = 0; index < count; ++index) {
        if (index && tc_ieq_ascii_n(entries[index - 1].key, (int)strlen(entries[index - 1].key), entries[index].key)) continue;
        if (!tc_buf_add(&out, entries[index].line, entries[index].length) || !tc_buf_adds(&out, "\r\n")) goto cleanup;
    }
    if (!tc_buf_add(&out, text + stop, size - stop)) goto cleanup;
    *result = out.p; *resultSize = out.len; ok = TRUE;
cleanup:
    if (entries) HeapFree(GetProcessHeap(), 0, entries);
    if (!ok) tc_buf_free(&out);
    return ok;
}

typedef struct { char key[128]; UINT matches; } TC_PATCH_KEY;

static int tc_compare_patch(const void* left, const void* right)
{
    return _stricmp(((const TC_PATCH_KEY*)left)->key, ((const TC_PATCH_KEY*)right)->key);
}

static BOOL tc_read_patch(const char* entries, DWORD bytes, DWORD* offset, char* key)
{
    const char* start = entries + *offset;
    const char* end = (const char*)memchr(start, 0, bytes - *offset);
    const char* equals;
    size_t len, i;
    if (!end || start == end) return FALSE;
    equals = (const char*)memchr(start, '=', (size_t)(end - start));
    len = (size_t)((equals ? equals : end) - start);
    if (!len || len >= 128 || memchr(start, '\r', (size_t)(end - start)) || memchr(start, '\n', (size_t)(end - start))) return FALSE;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)start[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return FALSE;
    }
    if (equals && !tc_is_valid_utf8_bytes(equals + 1)) return FALSE;
    memcpy(key, start, len); key[len] = 0;
    *offset = (DWORD)(end - entries) + 1;
    return TRUE;
}

static BOOL tc_check_patch(const char* text, DWORD size, const char* section, const char* entries, DWORD bytes)
{
    TC_PATCH_KEY* keys = NULL;
    DWORD offset = 0, count = 0, i, pos = 0, sections = 0;
    char key[128];
    BOOL inTarget = FALSE, ok = FALSE;
    if (bytes > TC_PATCH_MAX_BYTES || strlen(section) >= sizeof(key)) return FALSE;
    for (i = 0; section[i]; ++i) {
        unsigned char c = (unsigned char)section[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return FALSE;
    }
    while (offset < bytes && entries[offset]) {
        if (!tc_read_patch(entries, bytes, &offset, key) || ++count > 4096) return FALSE;
    }
    if ((!count && (bytes != 2 || entries[1])) || (count && offset != bytes - 1)) return FALSE;
    if (!count) return TRUE;
    keys = (TC_PATCH_KEY*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*keys));
    if (!keys) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    offset = 0;
    for (i = 0; i < count; ++i) if (!tc_read_patch(entries, bytes, &offset, keys[i].key)) goto cleanup;
    qsort(keys, count, sizeof(*keys), tc_compare_patch);
    for (i = 1; i < count; ++i) if (!_stricmp(keys[i-1].key, keys[i].key)) goto cleanup;
    while (pos < size) {
        DWORD start = pos, end, j;
        int left, right, kl, kr;
        TC_PATCH_KEY sought = {{0}, 0}, *found;
        while (pos < size && text[pos] != '\r' && text[pos] != '\n') ++pos;
        end = pos;
        if (pos < size && text[pos] == '\r') ++pos;
        if (pos < size && text[pos] == '\n') ++pos;
        if (tc_line_is_section(text + start, (int)(end-start), section)) {
            if (++sections > 1) goto cleanup;
            inTarget = TRUE; continue;
        }
        if (tc_line_is_any_section(text + start, (int)(end-start))) { inTarget = FALSE; continue; }
        if (!inTarget) continue;
        tc_trim_lr(text + start, (int)(end-start), &left, &right);
        if (left == right || text[start+left] == ';' || text[start+left] == '#') continue;
        for (j = start + left; j < start + right && text[j] != '='; ++j) {}
        if (j == start + right) continue;
        tc_trim_lr(text + start + left, (int)(j-start-left), &kl, &kr);
        if (kr-kl <= 0 || kr-kl >= (int)sizeof(sought.key)) continue;
        memcpy(sought.key, text + start + left + kl, (size_t)(kr-kl));
        found = (TC_PATCH_KEY*)bsearch(&sought, keys, count, sizeof(*keys), tc_compare_patch);
        if (found && ++found->matches > 1) goto cleanup;
    }
    ok = TRUE;
cleanup:
    HeapFree(GetProcessHeap(), 0, keys);
    return ok;
}

static BOOL tc_match_patch(const char* text, DWORD size, const char* section, const char* key, const char* value, BOOL utf8)
{
    DWORD pos = 0;
    BOOL inTarget = FALSE;
    while (pos < size) {
        DWORD start = pos, end, equals;
        int left, right;
        char raw[2048], converted[8192];
        while (pos < size && text[pos] != '\r' && text[pos] != '\n') ++pos;
        end = pos;
        if (pos < size && text[pos] == '\r') ++pos;
        if (pos < size && text[pos] == '\n') ++pos;
        if (tc_line_is_section(text+start, (int)(end-start), section)) { inTarget = TRUE; continue; }
        if (tc_line_is_any_section(text+start, (int)(end-start))) { inTarget = FALSE; continue; }
        if (!inTarget || !tc_line_key_match(text+start, (int)(end-start), key)) continue;
        for (equals = start; equals < end && text[equals] != '='; ++equals) {}
        ++equals;
        tc_trim_lr(text+equals, (int)(end-equals), &left, &right);
        if (utf8) return strlen(value) == (size_t)(right-left) && !memcmp(text+equals+left, value, (size_t)(right-left));
        if (right-left >= (int)sizeof(raw)) return FALSE;
        memcpy(raw, text+equals+left, (size_t)(right-left)); raw[right-left] = 0;
        /* Reuse legacy read conversion only; never encode new values to ACP. */
        return tc_ansi_to_utf8_compat(raw, converted, sizeof(converted)) && !strcmp(converted, value);
    }
    return FALSE;
}

static int tc_decode_legacy(const char* source, DWORD size, WCHAR* output, int capacity)
{
    /* Compatibility boundary: strict legacy INI ingress uses the existing Windows code page; conversion writes require consent and output UTF-8 only. */
    return MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, source, (int)size, output, capacity);
}

static BOOL tc_convert_text(const char* source, DWORD size, char** result, DWORD* bytes)
{
    WCHAR* wide = NULL;
    char* utf8 = NULL;
    int count, length;
    BOOL ok = FALSE;
    *result = NULL;
    /* Same source code page as legacy profile reads; never convert output back to ACP. */
    count = tc_decode_legacy(source, size, NULL, 0);
    if (!count) return FALSE;
    wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)count * sizeof(WCHAR));
    if (!wide) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    if (tc_decode_legacy(source, size, wide, count) != count) goto cleanup;
    length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, count, NULL, 0, NULL, NULL);
    if (!length || length > 16 * 1024 * 1024) goto cleanup;
    utf8 = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length + 1);
    if (!utf8) goto cleanup;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, count, utf8, length, NULL, NULL) != length) goto cleanup;
    utf8[length] = 0;
    *result = utf8; *bytes = (DWORD)length; utf8 = NULL; ok = TRUE;
cleanup:
    if (utf8) HeapFree(GetProcessHeap(), 0, utf8);
    HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    return ok;
}

static BOOL tc_verify_copy(LPCWSTR path, const char* expected, DWORD bytes)
{
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER length;
    char buffer[4096];
    DWORD offset = 0, got, error = ERROR_CRC;
    BOOL ok = FALSE;
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &length) || length.QuadPart != bytes) goto cleanup;
    while (offset < bytes) {
        DWORD amount = bytes - offset;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (!ReadFile(file, buffer, amount, &got, NULL) || got != amount || memcmp(buffer, expected + offset, amount)) goto cleanup;
        offset += amount;
    }
    if (!FlushFileBuffers(file)) { error = GetLastError(); goto cleanup; }
    ok = TRUE;
cleanup:
    CloseHandle(file);
    if (!ok) SetLastError(error);
    return ok;
}

static BOOL tc_commit_batchW(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes, BOOL scoped, WCHAR* backup, DWORD backupChars)
{
    HANDLE lock = NULL, source = INVALID_HANDLE_VALUE, temporary = INVALID_HANDLE_VALUE;
    wchar_t tempPath[MAX_PATH + 80] = {0};
    char* text = NULL;
    char* replacement = NULL;
    char* original = NULL;
    DWORD originalSize = 0;
    LARGE_INTEGER length;
    DWORD size = 0, read = 0, written = 0, offset = 0, error = ERROR_INVALID_DATA;
    BOOL hadBom = FALSE, ok = FALSE, tempCreated = FALSE, utf8 = FALSE, changed = FALSE;
    int attempt;
    if (!iniPath || !iniPath[0] || !section || !section[0] || !entries || entryBytes < 2) return FALSE;
    lock = tc_lock_wide(iniPath);
    if (!lock) return FALSE;
    source = CreateFileW(iniPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (source == INVALID_HANDLE_VALUE) { error = GetLastError(); goto cleanup; }
    if (!GetFileSizeEx(source, &length)) { error = GetLastError(); goto cleanup; }
    if (length.QuadPart < 0 || length.QuadPart > 16 * 1024 * 1024) { error = ERROR_FILE_TOO_LARGE; goto cleanup; }
    size = (DWORD)length.QuadPart;
    text = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 1);
    if (!text) { error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
    if (!ReadFile(source, text, size, &read, NULL) || read != size) { error = ERROR_READ_FAULT; goto cleanup; }
    text[size] = '\0';
    if (size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        hadBom = TRUE;
        size -= 3;
        memmove(text, text + 3, (SIZE_T)size + 1);
    }
    utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) != 0;
    if (memchr(text, 0, size) || (!utf8 && (!scoped || hadBom))) { error = scoped ? ERROR_INVALID_DATA : ERROR_NO_UNICODE_TRANSLATION; goto cleanup; }
    if (backup && !utf8) {
        DWORD convertedSize;
        if (!tc_convert_text(text, size, &replacement, &convertedSize)) { error = GetLastError(); goto cleanup; }
        original = text; originalSize = size;
        text = replacement; replacement = NULL; size = convertedSize;
        utf8 = TRUE; hadBom = TRUE;
    }
    if (scoped && !tc_check_patch(text, size, section, entries, entryBytes)) goto cleanup;
    while (offset < entryBytes && entries[offset]) {
        const char* entry = entries + offset;
        const char* end = (const char*)memchr(entry, 0, entryBytes - offset);
        const char* equals;
        char key[128];
        DWORD outSize = 0;
        size_t keyLength;
        if (!end) goto cleanup;
        equals = (const char*)memchr(entry, '=', (size_t)(end - entry));
        keyLength = (size_t)((equals ? equals : end) - entry);
        if (!keyLength || keyLength >= sizeof(key) || memchr(entry, '\r', (size_t)(end - entry)) || memchr(entry, '\n', (size_t)(end - entry))) goto cleanup;
        memcpy(key, entry, keyLength); key[keyLength] = '\0';
        if (equals) {
            if (scoped && tc_match_patch(text, size, section, key, equals + 1, utf8)) {
                offset = (DWORD)(end - entries) + 1;
                continue;
            }
            if (scoped && !utf8) {
                const unsigned char* value = (const unsigned char*)equals + 1;
                for (; *value; ++value) if (*value >= 0x80) {
                    error = tc_decode_legacy(text, size, NULL, 0) ? ERROR_NO_UNICODE_TRANSLATION : ERROR_INVALID_DATA;
                    goto cleanup;
                }
            }
            if (!tc_is_valid_utf8_bytes(equals + 1)) { error = ERROR_NO_UNICODE_TRANSLATION; goto cleanup; }
            if (!tc_ini_utf8_rewrite_key(NULL, text, size, section, key, equals + 1, &replacement, &outSize)) { error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup; }
        } else if (!tc_ini_utf8_rewrite_delete(text, size, section, key, FALSE, &replacement, &outSize)) {
            error = ERROR_NOT_ENOUGH_MEMORY; goto cleanup;
        }
        if (scoped && outSize > 16 * 1024 * 1024) { error = ERROR_FILE_TOO_LARGE; goto cleanup; }
        if (outSize != size || !tc_bytes_equal(text, replacement, size)) changed = TRUE;
        HeapFree(GetProcessHeap(), 0, text);
        text = replacement; replacement = NULL; size = outSize;
        offset = (DWORD)(end - entries) + 1;
    }
    if (offset >= entryBytes || entries[offset] != '\0') goto cleanup;
    if (scoped) {
        if (!changed && !original) { ok = TRUE; goto cleanup; }
    } else {
        if (!tc_sort_section(text, size, section, &replacement, &read)) goto cleanup;
        HeapFree(GetProcessHeap(), 0, text);
        text = replacement; replacement = NULL; size = read;
    }
    if (original) {
        WCHAR copyPath[MAX_PATH + 80];
        BOOL copied = FALSE;
        for (attempt = 0; attempt < 16; ++attempt) {
            if (swprintf_s(copyPath, _countof(copyPath), L"%ls.pre-utf8-%lu-%lu-%d.bak", iniPath, GetCurrentProcessId(), GetTickCount(), attempt) < 0) {
                error = ERROR_FILENAME_EXCED_RANGE; goto cleanup;
            }
            if (wcslen(copyPath) >= backupChars) { error = ERROR_INSUFFICIENT_BUFFER; goto cleanup; }
            if (CopyFileW(iniPath, copyPath, TRUE)) { copied = TRUE; break; }
            error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) goto cleanup;
        }
        if (!copied || !tc_verify_copy(copyPath, original, originalSize)) { error = GetLastError(); goto cleanup; }
        wcscpy_s(backup, backupChars, copyPath);
        /* Retain the verified original even if the later replacement fails. */
    }
    for (attempt = 0; attempt < 16; ++attempt) {
        if (swprintf_s(tempPath, _countof(tempPath), L"%ls.tclock-%lu-%lu-%d.tmp", iniPath, GetCurrentProcessId(), GetTickCount(), attempt) < 0) {
            error = ERROR_FILENAME_EXCED_RANGE; goto cleanup;
        }
        temporary = CreateFileW(tempPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if (temporary != INVALID_HANDLE_VALUE) { tempCreated = TRUE; break; }
        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) goto cleanup;
    }
    if (temporary == INVALID_HANDLE_VALUE) goto cleanup;
    if (hadBom && (!WriteFile(temporary, "\xEF\xBB\xBF", 3, &written, NULL) || written != 3)) { error = ERROR_WRITE_FAULT; goto cleanup; }
    if (!WriteFile(temporary, text, size, &written, NULL) || written != size) { error = ERROR_WRITE_FAULT; goto cleanup; }
    if (!FlushFileBuffers(temporary)) { error = GetLastError(); goto cleanup; }
    CloseHandle(temporary); temporary = INVALID_HANDLE_VALUE;
    CloseHandle(source); source = INVALID_HANDLE_VALUE;
    if (!ReplaceFileW(iniPath, tempPath, NULL, 0, NULL, NULL)) { error = GetLastError(); goto cleanup; }
    tempCreated = FALSE;
    tc_ini_utf8_clear_cache();
    ok = TRUE;
cleanup:
    if (source != INVALID_HANDLE_VALUE) CloseHandle(source);
    if (temporary != INVALID_HANDLE_VALUE) CloseHandle(temporary);
    if (tempCreated) DeleteFileW(tempPath);
    if (text) HeapFree(GetProcessHeap(), 0, text);
    if (replacement) HeapFree(GetProcessHeap(), 0, replacement);
    if (original) HeapFree(GetProcessHeap(), 0, original);
    tc_ini_lock_leave(lock);
    if (!ok) SetLastError(error);
    return ok;
}

BOOL tc_write_batchW(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes)
{
    return tc_commit_batchW(iniPath, section, entries, entryBytes, FALSE, NULL, 0);
}

BOOL tc_patch_batchW(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes)
{
    return tc_commit_batchW(iniPath, section, entries, entryBytes, TRUE, NULL, 0);
}

BOOL tc_patch_utf8W(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes, WCHAR* backup, DWORD backupChars)
{
    if (!backup || !backupChars) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    backup[0] = 0;
    return tc_commit_batchW(iniPath, section, entries, entryBytes, TRUE, backup, backupChars);
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
    char secNorm[128];
    char keyNorm[128];
    if (!iniPath || !key || !key[0]) return FALSE;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    tc_normalize_ini_name(key, NULL, keyNorm, (int)sizeof(keyNorm));
    if (!keyNorm[0]) return FALSE;
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

    if (!tc_ini_utf8_rewrite_key(iniPath, text, size, secNorm, keyNorm, utf8Val, &outText, &outSize)) {
        goto cleanup;
    }

    if (outSize == size && text && outText && tc_bytes_equal(text, outText, size)) {
        ok = TRUE;
        goto cleanup;
    }

    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) {
        goto cleanup;
    }
    ok = TRUE;
    tc_ini_utf8_clear_cache();

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
    char secNorm[128];
    char keyNorm[128];

    if (!iniPath || !key || !key[0]) return FALSE;
    tc_normalize_ini_name(section, "Main", secNorm, (int)sizeof(secNorm));
    tc_normalize_ini_name(key, NULL, keyNorm, (int)sizeof(keyNorm));
    if (!keyNorm[0]) return FALSE;

    hLock = tc_ini_lock_enter(iniPath);
    if (!hLock) return FALSE;

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &hadBom)) goto cleanup;
    if (!tc_ini_utf8_rewrite_delete(text, size, secNorm, keyNorm, FALSE, &outText, &outSize)) goto cleanup;
    if (outSize == size && text && outText && tc_bytes_equal(text, outText, size)) {
        ok = TRUE;
        goto cleanup;
    }
    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) goto cleanup;
    ok = TRUE;
    tc_ini_utf8_clear_cache();

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
    if (outSize == size && text && outText && tc_bytes_equal(text, outText, size)) {
        ok = TRUE;
        goto cleanup;
    }
    if (!tc_write_text_file_utf8(iniPath, outText, outSize, hadBom ? TRUE : FALSE)) goto cleanup;
    ok = TRUE;
    tc_ini_utf8_clear_cache();

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
        if (!tc_ini_utf8_write_string(tmpFile, " \tMain\r\n", "\tDebugLog\r\n", "1")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if (tc_count_literal(outText, "[Main]") != 1) goto cleanup;
        if (tc_count_literal(outText, "DebugLog=1") != 1) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;
        if (!tc_ini_utf8_write_string(tmpFile, "\tMisc\r\n", "\tA\r\n", "3")) goto cleanup;
        if (!tc_read_text_file_utf8(tmpFile, &outText, &outSize, &hadBom)) goto cleanup;
        if (tc_count_literal(outText, "[Misc]") != 1) goto cleanup;
        if (tc_count_literal(outText, "A=3") != 1) goto cleanup;
        tc_free_text_buffer(outText);
        outText = NULL;
        outSize = 0;
    }

    ok = TRUE;

cleanup:
    if (outText) tc_free_text_buffer(outText);
    {
        wchar_t wTmpFile[MAX_PATH];
        /* Migrate cleanup to W API; ignore cleanup failure in selfcheck path. */
        if (tc_utf8_to_utf16(tmpFile, wTmpFile, (int)(sizeof(wTmpFile) / sizeof(wTmpFile[0]))) > 0 ||
            tc_ansi_to_utf16_compat(0, tmpFile, wTmpFile, (int)(sizeof(wTmpFile) / sizeof(wTmpFile[0]))) > 0) {
            DeleteFileW(wTmpFile);
        }
    }
    return ok;
}
