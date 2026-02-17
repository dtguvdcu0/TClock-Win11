#ifndef TC_INI_IO_UTF8_H
#define TC_INI_IO_UTF8_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

BOOL tc_ini_utf8_detect_file(const char* iniPath, BOOL* isUtf8, BOOL* hasBom);
int tc_ini_utf8_read_string(const char* iniPath, const char* section, const char* key,
                            const char* defval, char* outVal, int outSize);
int tc_ini_utf8_read_section_multisz(const char* iniPath, const char* section,
                                     char* outBuf, int outBytes);
BOOL tc_ini_utf8_write_string(const char* iniPath, const char* section, const char* key,
                              const char* val);
BOOL tc_ini_utf8_delete_key(const char* iniPath, const char* section, const char* key);
BOOL tc_ini_utf8_delete_section(const char* iniPath, const char* section);
BOOL tc_ini_utf8_selfcheck(void);

#ifdef __cplusplus
}
#endif

#endif
