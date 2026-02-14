#ifndef TC_TEXT_CODEC_H
#define TC_TEXT_CODEC_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int tc_utf8_to_utf16(const char* utf8, wchar_t* outWide, int outWideCch);
int tc_utf16_to_utf8(const wchar_t* wide, char* outUtf8, int outUtf8Bytes);
int tc_ansi_to_utf16(UINT codepage, const char* ansi, wchar_t* outWide, int outWideCch);
int tc_utf16_to_ansi(UINT codepage, const wchar_t* wide, char* outAnsi, int outAnsiBytes);
BOOL tc_text_codec_selfcheck(void);

#ifdef __cplusplus
}
#endif

#endif
