#include "text_codec.h"

static int tc_len_no_nul_w(const wchar_t* s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int tc_len_no_nul_a(const char* s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int tc_utf8_to_utf16(const char* utf8, wchar_t* outWide, int outWideCch)
{
    int n;
    if (!utf8 || !outWide || outWideCch <= 0) return 0;
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, outWide, outWideCch);
    if (n <= 0) {
        outWide[0] = L'\0';
        return 0;
    }
    outWide[outWideCch - 1] = L'\0';
    return tc_len_no_nul_w(outWide);
}

int tc_utf16_to_utf8(const wchar_t* wide, char* outUtf8, int outUtf8Bytes)
{
    int n;
    if (!wide || !outUtf8 || outUtf8Bytes <= 0) return 0;
    n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, outUtf8, outUtf8Bytes, NULL, NULL);
    if (n <= 0) {
        outUtf8[0] = '\0';
        return 0;
    }
    outUtf8[outUtf8Bytes - 1] = '\0';
    return tc_len_no_nul_a(outUtf8);
}

int tc_ansi_to_utf16(UINT codepage, const char* ansi, wchar_t* outWide, int outWideCch)
{
    int n;
    if (!codepage) codepage = GetACP();
    if (!ansi || !outWide || outWideCch <= 0) return 0;
    n = MultiByteToWideChar(codepage, 0, ansi, -1, outWide, outWideCch);
    if (n <= 0) {
        outWide[0] = L'\0';
        return 0;
    }
    outWide[outWideCch - 1] = L'\0';
    return tc_len_no_nul_w(outWide);
}

int tc_utf16_to_ansi(UINT codepage, const wchar_t* wide, char* outAnsi, int outAnsiBytes)
{
    int n;
    if (!codepage) codepage = GetACP();
    if (!wide || !outAnsi || outAnsiBytes <= 0) return 0;
    n = WideCharToMultiByte(codepage, 0, wide, -1, outAnsi, outAnsiBytes, NULL, NULL);
    if (n <= 0) {
        outAnsi[0] = '\0';
        return 0;
    }
    outAnsi[outAnsiBytes - 1] = '\0';
    return tc_len_no_nul_a(outAnsi);
}

BOOL tc_text_codec_selfcheck(void)
{
    const char* s = "codec-selfcheck";
    wchar_t wbuf[64];
    char abuf[64];
    if (tc_utf8_to_utf16(s, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) <= 0) return FALSE;
    if (tc_utf16_to_utf8(wbuf, abuf, (int)sizeof(abuf)) <= 0) return FALSE;
    return (lstrcmp(s, abuf) == 0) ? TRUE : FALSE;
}
