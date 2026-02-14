#include "ini_io_utf8.h"

#include "text_file_utf8.h"

static const char* tc_section_or_main(const char* section)
{
    return (section && section[0]) ? section : "Main";
}

BOOL tc_ini_utf8_detect_file(const char* iniPath, BOOL* isUtf8, BOOL* hasBom)
{
    char* text = NULL;
    DWORD size = 0;
    BOOL bom = FALSE;

    if (!iniPath) return FALSE;
    if (isUtf8) *isUtf8 = FALSE;
    if (hasBom) *hasBom = FALSE;

    if (!tc_read_text_file_utf8(iniPath, &text, &size, &bom)) {
        return FALSE;
    }

    tc_free_text_buffer(text);
    if (isUtf8) *isUtf8 = TRUE;
    if (hasBom) *hasBom = bom;
    return TRUE;
}

int tc_ini_utf8_read_string(const char* iniPath, const char* section, const char* key,
                            const char* defval, char* outVal, int outSize)
{
    if (!iniPath || !key || !outVal || outSize <= 0) return 0;
    return (int)GetPrivateProfileStringA(tc_section_or_main(section), key, defval ? defval : "",
                                         outVal, (DWORD)outSize, iniPath);
}

BOOL tc_ini_utf8_write_string(const char* iniPath, const char* section, const char* key,
                              const char* val)
{
    if (!iniPath || !key || !key[0]) return FALSE;
    return WritePrivateProfileStringA(tc_section_or_main(section), key, val, iniPath) ? TRUE : FALSE;
}

BOOL tc_ini_utf8_delete_key(const char* iniPath, const char* section, const char* key)
{
    if (!iniPath || !key || !key[0]) return FALSE;
    return WritePrivateProfileStringA(tc_section_or_main(section), key, NULL, iniPath) ? TRUE : FALSE;
}

BOOL tc_ini_utf8_delete_section(const char* iniPath, const char* section)
{
    if (!iniPath) return FALSE;
    return WritePrivateProfileSectionA(tc_section_or_main(section), NULL, iniPath) ? TRUE : FALSE;
}

BOOL tc_ini_utf8_selfcheck(void)
{
    return tc_text_file_utf8_selfcheck();
}
