#ifndef TC_INI_IO_UTF8_H
#define TC_INI_IO_UTF8_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scoped patch limits: 1 MiB of operations, 4096 keys, 16 MiB source/output. */
#define TC_PATCH_MAX_BYTES (1024u * 1024u)

BOOL tc_ini_utf8_detect_file(const char* iniPath, BOOL* isUtf8, BOOL* hasBom);
void tc_ini_utf8_clear_cache(void);
BOOL tc_ini_beginW(LPCWSTR path);
/* Opt-in strict legacy ingress for readers; the source file remains unchanged. */
BOOL tc_begin_legacyW(LPCWSTR path);
/* Literal value, strict wide output; missing/failure leaves output unchanged. */
HRESULT tc_read_valueW(LPCWSTR path, const char* section, const char* key, WCHAR* output, UINT capacity);
void tc_ini_end(void);
/* Reads in a scope refresh after successful writers linked in this module; external or other-module edits appear in a fresh scope. */
int tc_ini_utf8_read_string(const char* iniPath, const char* section, const char* key,
                            const char* defval, char* outVal, int outSize);
int tc_ini_utf8_read_section_multisz(const char* iniPath, const char* section,
                                     char* outBuf, int outBytes);
int tc_ini_utf8_read_section_multisz_ex(const char* iniPath, const char* section,
                                        char* outBuf, int outBytes, BOOL* truncated);
BOOL tc_ini_utf8_write_string(const char* iniPath, const char* section, const char* key,
                              const char* val);
BOOL tc_ini_utf8_delete_key(const char* iniPath, const char* section, const char* key);
BOOL tc_ini_utf8_delete_section(const char* iniPath, const char* section);
BOOL tc_ini_utf8_selfcheck(void);
/* Atomic UTF-8 batch; each bounded multisz entry sets key=value or deletes key without an equals sign. */
BOOL tc_write_batchW(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes);
/* Scoped atomic patch: preserve unrelated bytes/order and reject ambiguous targets.
   Legacy files allow ASCII changes and unchanged values through the existing read boundary. */
BOOL tc_patch_batchW(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes);
/* Explicitly authorized legacy-to-UTF8 patch; returns a verified original backup path if converted.
   The caller must obtain migration consent. Retain the backup on both success and failure. */
BOOL tc_patch_utf8W(const wchar_t* iniPath, const char* section, const char* entries, DWORD entryBytes, WCHAR* backup, DWORD backupChars);

#ifdef __cplusplus
}
#endif

#endif
