#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "tclock.h"
#include "plugin_registry.h"
#include "../common/plugin_resources.h"
#include "plugin_runtime.h"
#include <tlhelp32.h>
#include <winver.h>
#include "../common/text_codec.h"
#include "../common/ini_io_utf8.h"
static const char* preg_sections[PREG_KNOWN] = {"TCard","TCycle","TCalendar","TCapture"};
static const WCHAR* preg_files[PREG_KNOWN] = {L"plugins\\TCard.exe",L"plugins\\TCycle.exe",L"plugins\\TCalendar.exe",L"plugins\\TCapture.exe"};
static BOOL preg_migrating;
typedef struct PREG_DISCOVERY { WCHAR id[32],name[128],path[MAX_PATH];BOOL conflict; } PREG_DISCOVERY;
static PREG_DISCOVERY* preg_inventory;
static BOOL preg_scan_failed;
static int preg_inventory_count;
static BOOL preg_scanned;
static ULONGLONG preg_fingerprint;
static BOOL preg_get_resource(LPCWSTR file,UINT id,BOOL english,BOOL invariant,WCHAR* out,int capacity);

/* These legacy binary protocols are not controlled by TClock's accessors. */
static BOOL preg_keep_key(const char* section,const char* key) {
    return (_stricmp(section,"TCycle")==0 && _stricmp(key,"Enabled")==0) ||
        (_stricmp(section,"TCapture")==0 && _strnicmp(key,"Hotkey",6)==0);
}
BOOL preg_map_key(const char* section,const char* key,char* canonical,int size) {
    int i;
    if(preg_migrating||!section||!key||!canonical||size<80)return FALSE;
    for(i=0;i<PREG_KNOWN;++i)if(_stricmp(section,preg_sections[i])==0){
        if(preg_keep_key(section,key)||strlen(preg_sections[i])+strlen(key)+2>(size_t)size)return FALSE;
        sprintf_s(canonical,size,"%s.%s",preg_sections[i],key);return TRUE;
    }
    if(_stricmp(section,"ETC")==0&&_stricmp(key,"ExitExtensionsOnTClockExit")==0){strcpy_s(canonical,size,"ExitExtensionsOnTClockExit");return TRUE;}
    return FALSE;
}
/* Existing UTF-8 INI serialization boundary: decode once; all paths/commands
   in registry/UI/runtime interfaces remain UTF-16 and use explicit W APIs. */
static BOOL preg_read_text(const char* section,const char* key,LPCWSTR fallback,WCHAR* out,int capacity) {
    char encoded[8192];
    GetMyRegStr(section,key,encoded,sizeof(encoded),"\x1d");
    if (strcmp(encoded,"\x1d")==0) {wcscpy_s(out,capacity,fallback?fallback:L"");return FALSE;}
    if (tc_utf8_to_utf16(encoded,out,capacity)<=0) out[0]=0;
    return TRUE;
}
BOOL preg_resolve(LPCWSTR path,WCHAR* absolute) {
    WCHAR directory[MAX_PATH],combined[MAX_PATH];DWORD n;
    if (!path || !path[0]) return FALSE;
    if (PathIsRelativeW(path)) {
        n=GetModuleFileNameW(NULL,directory,MAX_PATH);
        if (!n || n>=MAX_PATH || !PathRemoveFileSpecW(directory) || wcslen(directory)+wcslen(path)+2>MAX_PATH || !PathCombineW(combined,directory,path))return FALSE;
    } else {if(wcslen(path)>=MAX_PATH)return FALSE;wcscpy_s(combined,MAX_PATH,path);}
    n=GetFullPathNameW(combined,MAX_PATH,absolute,NULL);return n>0 && n<MAX_PATH;
}
BOOL preg_is_present(const PLUGIN_RECORD* record) {
    WCHAR path[MAX_PATH];DWORD attributes;
    if (!preg_resolve(record->path,path))return FALSE;
    attributes=GetFileAttributesW(path);return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
static BOOL preg_legacy_path(WCHAR* path) {
    return preg_resolve(L"plugins\\action-plugins.ini",path);
}
static BOOL preg_read_custom(int slot,PLUGIN_RECORD* record) {
    char key[64];WCHAR type[16];
    ZeroMemory(record,sizeof(*record));record->known=-1;record->slot=slot;
    if (GetMyRegLong("Plugins","RegistryVersion",0)<1) {
        WCHAR registration[MAX_PATH],section[32],relative[MAX_PATH],directory[MAX_PATH];
        if (!preg_legacy_path(registration) || (UINT)slot>min((UINT)PREG_MAX_CUSTOM,GetPrivateProfileIntW(L"Plugins",L"Count",0,registration)))return FALSE;
        swprintf_s(section,_countof(section),L"Plugin%d",slot);
        if (!GetPrivateProfileStringW(section,L"Path",L"",relative,MAX_PATH,registration))return FALSE;
        wcscpy_s(directory,MAX_PATH,registration);PathRemoveFileSpecW(directory);
        if(PathIsRelativeW(relative)) {
            if(wcslen(directory)+wcslen(relative)+2>MAX_PATH || !PathCombineW(record->path,directory,relative))return FALSE;
        } else wcscpy_s(record->path,MAX_PATH,relative);
        swprintf_s(record->id,_countof(record->id),L"legacy.dll.%d",slot);
        wcscpy_s(record->name,_countof(record->name),PathFindFileNameW(record->path));
        record->kind=PREG_KIND_DLL;record->enabled=GetPrivateProfileIntW(section,L"Enabled",0,registration)!=0;record->visible=TRUE;return TRUE;
    }
    sprintf_s(key,sizeof(key),"Item%d.Id",slot);preg_read_text("Plugins",key,L"",record->id,_countof(record->id));if(!record->id[0])return FALSE;
    sprintf_s(key,sizeof(key),"Item%d.Type",slot);preg_read_text("Plugins",key,L"exe",type,_countof(type));
    record->kind=_wcsicmp(type,L"dll")==0?PREG_KIND_DLL:PREG_KIND_EXE;
    sprintf_s(key,sizeof(key),"Item%d.Enabled",slot);record->enabled=GetMyRegLong("Plugins",key,0)!=0;
    sprintf_s(key,sizeof(key),"Item%d.Name",slot);preg_read_text("Plugins",key,record->id,record->name,_countof(record->name));
    sprintf_s(key,sizeof(key),"Item%d.Path",slot);preg_read_text("Plugins",key,L"",record->path,_countof(record->path));
    sprintf_s(key,sizeof(key),"Item%d.Args",slot);preg_read_text("Plugins",key,L"",record->args,_countof(record->args));
    sprintf_s(key,sizeof(key),"Item%d.SettingsArgs",slot);preg_read_text("Plugins",key,L"",record->settings,_countof(record->settings));
    sprintf_s(key,sizeof(key),"Item%d.Ini",slot);preg_read_text("Plugins",key,L"",record->ini,_countof(record->ini));
    record->visible=TRUE;return TRUE;
}
static BOOL preg_hash_folder(ULONGLONG* output) {
    WCHAR pattern[MAX_PATH];WIN32_FIND_DATAW item;HANDLE search;ULONGLONG result=0;DWORD error;
    if(!output||!preg_resolve(L"plugins\\*.dll",pattern))return FALSE;
    search=FindFirstFileW(pattern,&item);
    if(search==INVALID_HANDLE_VALUE){error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){*output=0;return TRUE;}return FALSE;}
    do { if(!(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        const WCHAR* ch=item.cFileName;ULONGLONG hash=1469598103934665603ULL;
        while(*ch){hash=(hash^*ch++)*1099511628211ULL;}
        hash^=((ULONGLONG)item.ftLastWriteTime.dwHighDateTime<<32)|item.ftLastWriteTime.dwLowDateTime;
        hash^=(((ULONGLONG)item.nFileSizeHigh<<32)|item.nFileSizeLow)*1099511628211ULL;result^=hash;
    }}while(FindNextFileW(search,&item));
    error=GetLastError();FindClose(search);if(error!=ERROR_NO_MORE_FILES){SetLastError(error);return FALSE;}*output=result;return TRUE;
}
static int __cdecl preg_compare_plugins(const void* left,const void* right) {
    return wcscmp(((const PREG_DISCOVERY*)left)->id,((const PREG_DISCOVERY*)right)->id);
}
BOOL preg_discover(void) {
    ULONGLONG stamp,after;WCHAR pattern[MAX_PATH],directory[MAX_PATH];WIN32_FIND_DATAW item;HANDLE search=INVALID_HANDLE_VALUE;
    PREG_DISCOVERY* foundItems=NULL;int used=0,capacity=0;BOOL ok=FALSE;DWORD error;
    if(!preg_hash_folder(&stamp)){preg_scan_failed=TRUE;return FALSE;}
    if(preg_scanned&&!preg_scan_failed&&stamp==preg_fingerprint)return FALSE;
    if(!preg_resolve(L"plugins\\*.dll",pattern))goto cleanup;
    wcscpy_s(directory,MAX_PATH,pattern);if(!PathRemoveFileSpecW(directory))goto cleanup;
    search=FindFirstFileW(pattern,&item);
    if(search==INVALID_HANDLE_VALUE){error=GetLastError();if((error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)&&preg_hash_folder(&after)&&after==stamp)ok=TRUE;goto cleanup;}
    do { if(!(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        PREG_DISCOVERY found={0};int i;HANDLE probe;LARGE_INTEGER imageBytes;
        if(wcslen(directory)+wcslen(item.cFileName)+2>MAX_PATH||!PathCombineW(found.path,directory,item.cFileName))goto cleanup;
        probe=CreateFileW(found.path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(probe==INVALID_HANDLE_VALUE)goto cleanup;
        if(!GetFileSizeEx(probe,&imageBytes)){CloseHandle(probe);goto cleanup;}
        if(imageBytes.QuadPart<(LONGLONG)sizeof(IMAGE_DOS_HEADER)){CloseHandle(probe);continue;}
        if(!preg_get_identity(found.path,found.id,32)){
            error=GetLastError();CloseHandle(probe);
            if(error==ERROR_INVALID_DATA||error==ERROR_BAD_EXE_FORMAT||error==ERROR_EXE_MACHINE_TYPE_MISMATCH||error==ERROR_RESOURCE_DATA_NOT_FOUND||error==ERROR_RESOURCE_TYPE_NOT_FOUND||error==ERROR_RESOURCE_NAME_NOT_FOUND||error==ERROR_RESOURCE_LANG_NOT_FOUND)continue;
            goto cleanup;
        }
        CloseHandle(probe);
        for(i=0;i<used;++i)if(wcscmp(found.id,foundItems[i].id)==0)break;
        if(i<used){foundItems[i].conflict=TRUE;continue;}
        if(!preg_get_resource(found.path,TC_PLUGIN_NAME_ID,TRUE,FALSE,found.name,128))wcscpy_s(found.name,128,item.cFileName);
        if(used==capacity){int nextCapacity;PREG_DISCOVERY* next;
            if(capacity>INT_MAX/2)goto cleanup;nextCapacity=capacity?capacity*2:16;
            if((SIZE_T)nextCapacity>((SIZE_T)-1)/sizeof(*next))goto cleanup;
            next=foundItems?(PREG_DISCOVERY*)HeapReAlloc(GetProcessHeap(),0,foundItems,(SIZE_T)nextCapacity*sizeof(*next)):(PREG_DISCOVERY*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)nextCapacity*sizeof(*next));
            if(!next)goto cleanup;foundItems=next;capacity=nextCapacity;
        }
        foundItems[used++]=found;
    }}while(FindNextFileW(search,&item));
    if(GetLastError()!=ERROR_NO_MORE_FILES)goto cleanup;
    if(!preg_hash_folder(&after)||after!=stamp)goto cleanup;
    ok=TRUE;
cleanup:
    if(search!=INVALID_HANDLE_VALUE)FindClose(search);
    if(!ok){if(foundItems)HeapFree(GetProcessHeap(),0,foundItems);preg_scan_failed=TRUE;return FALSE;}
    if(used)qsort(foundItems,used,sizeof(*foundItems),preg_compare_plugins);
    if(preg_inventory)HeapFree(GetProcessHeap(),0,preg_inventory);
    preg_inventory=foundItems;preg_inventory_count=used;preg_scanned=TRUE;preg_scan_failed=FALSE;preg_fingerprint=stamp;return TRUE;
}
BOOL preg_discovery_ok(void){return !preg_scan_failed;}
static int preg_merge_plugins(PLUGIN_RECORD* records,int count,int capacity) {
    int i,j,k;
    for(i=PREG_KNOWN;i<count;++i)records[i].automatic=wcsncmp(records[i].id,L"auto.",5)==0;
    for(i=0;i<preg_inventory_count;++i) {
        PREG_DISCOVERY* found=&preg_inventory[i];WCHAR identity[64];int index=-1;
        swprintf_s(identity,64,L"auto.%ls",found->id);
        for(j=PREG_KNOWN;j<count;++j)if(records[j].kind==PREG_KIND_DLL) {
            WCHAR path[MAX_PATH];
            if(wcscmp(records[j].id,identity)==0||(!records[j].automatic&&preg_resolve(records[j].path,path)&&_wcsicmp(path,found->path)==0)) {
                if(index>=0){records[j].conflict=TRUE;records[index].conflict=TRUE;}else index=j;
            }
        }
        if(index<0) {
            int slot;
            if(count>=capacity)continue;
            for(slot=1;slot<INT_MAX;++slot){for(k=PREG_KNOWN;k<count;++k)if(records[k].slot==slot)break;if(k==count)break;}
            if(slot==INT_MAX)continue;
            index=count++;ZeroMemory(&records[index],sizeof(records[index]));records[index].known=-1;records[index].slot=slot;records[index].kind=PREG_KIND_DLL;records[index].visible=TRUE;records[index].automatic=TRUE;
            wcscpy_s(records[index].id,64,identity);wcscpy_s(records[index].name,128,found->name);wcscpy_s(records[index].path,MAX_PATH,found->path);
        }
        {
            PLUGIN_RECORD* r=&records[index];WCHAR old[MAX_PATH];
            if(r->automatic&&_wcsicmp(r->path,found->path)!=0) {
                /* Keep the old default private INI when a provider binary is renamed. */
                if(!r->ini[0]&&preg_resolve(r->path,old)&&PathRenameExtensionW(old,L".ini"))wcscpy_s(r->ini,MAX_PATH,old);
                wcscpy_s(r->path,MAX_PATH,found->path);
            }
            r->discovered=TRUE;r->conflict=r->conflict||found->conflict;wcscpy_s(r->provider,32,found->id);
        }
    }
    return count;
}
int preg_load(PLUGIN_RECORD* records,int capacity) {
    WCHAR active[MAX_PATH];int i,count=0;
    if(!preg_scanned)preg_discover();
    BOOL scope=tc_utf8_to_utf16(g_inifile,active,MAX_PATH)>0 && tc_ini_beginW(active);
    for(i=0;i<PREG_KNOWN && count<capacity;++i) {
        PLUGIN_RECORD* r=&records[count++];char key[80];
        ZeroMemory(r,sizeof(*r));r->known=i;r->kind=PREG_KIND_KNOWN;r->slot=0;
        tc_utf8_to_utf16(preg_sections[i],r->id,_countof(r->id));wcscpy_s(r->name,_countof(r->name),r->id);
        preg_read_text(preg_sections[i],"Path",preg_files[i],r->path,_countof(r->path));
        if (!r->path[0])wcscpy_s(r->path,_countof(r->path),preg_files[i]);
        if (i==3 && GetMyRegLong("TCapture","Enable",-1)==-1)r->enabled=GetMyRegLong("ETC","TCaptureEnable",0)!=0;
        else r->enabled=GetMyRegLong(preg_sections[i],"Enable",0)!=0;
        if (i==1 || i==3)wcscpy_s(r->settings,_countof(r->settings),L"--settings");
        sprintf_s(key,sizeof(key),"%s.Ini",preg_sections[i]);preg_read_text("Plugins",key,L"",r->ini,_countof(r->ini));
        sprintf_s(key,sizeof(key),"%s.Registered",preg_sections[i]);
        r->visible=preg_is_present(r)||GetMyRegLong("Plugins",key,0)||r->enabled||_wcsicmp(r->path,preg_files[i])!=0;
    }
    for(i=1;i<=PREG_MAX_CUSTOM && count<capacity;++i)if(preg_read_custom(i,&records[count]))++count;
    if(scope)tc_ini_end();return preg_merge_plugins(records,count,capacity);
}
static int preg_cmp_slot(const void* a,const void* b){int x=*(const int*)a,y=*(const int*)b;return (x>y)-(x<y);}
static BOOL preg_legacy_section(LPCWSTR path,HANDLE* retained,char** result){
 HANDLE file=INVALID_HANDLE_VALUE;LARGE_INTEGER length;DWORD read=0,n;char* raw=NULL;WCHAR* wide=NULL;char* utf8=NULL;int chars=4096,needed;BOOL ok=FALSE;
 file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(file==INVALID_HANDLE_VALUE)goto cleanup;
 if(!GetFileSizeEx(file,&length)||length.QuadPart<0||length.QuadPart>INT_MAX-1)goto cleanup;
 raw=(char*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)length.QuadPart+1);if(!raw)goto cleanup;
 if(length.QuadPart && (!ReadFile(file,raw,(DWORD)length.QuadPart,&read,NULL)||read!=(DWORD)length.QuadPart))goto cleanup;
 if(!read || memchr(raw,0,read))goto cleanup;
 if(read>=3&&(BYTE)raw[0]==0xEF&&(BYTE)raw[1]==0xBB&&(BYTE)raw[2]==0xBF)goto cleanup;
 SetLastError(ERROR_SUCCESS);
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw,(int)read,NULL,0)>0 || GetLastError()!=ERROR_NO_UNICODE_TRANSLATION)goto cleanup;
 for(;;){WCHAR* next=wide?(WCHAR*)HeapReAlloc(GetProcessHeap(),0,wide,(SIZE_T)chars*sizeof(WCHAR)):(WCHAR*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)chars*sizeof(WCHAR));
  if(!next)goto cleanup;wide=next;SetLastError(ERROR_SUCCESS);n=GetPrivateProfileSectionW(L"Plugins",wide,(DWORD)chars,path);
  if(!n&&GetLastError()!=ERROR_SUCCESS)goto cleanup;
  if(n!=(DWORD)(chars-2))break;if(chars>INT_MAX/2)goto cleanup;chars*=2;
 }
 if(!n){utf8=(char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,2);if(!utf8)goto cleanup;}
 else {needed=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)n+1,NULL,0,NULL,NULL);if(needed<=0||needed==INT_MAX)goto cleanup;
  utf8=(char*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)needed+1);if(!utf8)goto cleanup;
  if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide,(int)n+1,utf8,needed,NULL,NULL)!=needed)goto cleanup;utf8[needed]=0;
 }
 *retained=file;file=INVALID_HANDLE_VALUE;*result=utf8;utf8=NULL;ok=TRUE;
cleanup:
 if(raw)HeapFree(GetProcessHeap(),0,raw);if(wide)HeapFree(GetProcessHeap(),0,wide);if(utf8)HeapFree(GetProcessHeap(),0,utf8);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);return ok;
}
static BOOL preg_read_custom_w(LPCWSTR path,int slot,PLUGIN_RECORD* r){
 WCHAR key[64],type[16];ZeroMemory(r,sizeof(*r));r->known=-1;r->slot=slot;
 swprintf_s(key,_countof(key),L"Item%d.Id",slot);if(!GetPrivateProfileStringW(L"Plugins",key,L"",r->id,_countof(r->id),path))return FALSE;
 swprintf_s(key,_countof(key),L"Item%d.Type",slot);GetPrivateProfileStringW(L"Plugins",key,L"exe",type,_countof(type),path);r->kind=_wcsicmp(type,L"dll")==0?PREG_KIND_DLL:PREG_KIND_EXE;
 swprintf_s(key,_countof(key),L"Item%d.Enabled",slot);r->enabled=GetPrivateProfileIntW(L"Plugins",key,0,path)!=0;
 swprintf_s(key,_countof(key),L"Item%d.Name",slot);GetPrivateProfileStringW(L"Plugins",key,r->id,r->name,_countof(r->name),path);
 swprintf_s(key,_countof(key),L"Item%d.Path",slot);GetPrivateProfileStringW(L"Plugins",key,L"",r->path,_countof(r->path),path);
 swprintf_s(key,_countof(key),L"Item%d.Args",slot);GetPrivateProfileStringW(L"Plugins",key,L"",r->args,_countof(r->args),path);
 swprintf_s(key,_countof(key),L"Item%d.SettingsArgs",slot);GetPrivateProfileStringW(L"Plugins",key,L"",r->settings,_countof(r->settings),path);
 swprintf_s(key,_countof(key),L"Item%d.Ini",slot);GetPrivateProfileStringW(L"Plugins",key,L"",r->ini,_countof(r->ini),path);r->visible=TRUE;return TRUE;
}
static void preg_known_text_w(LPCWSTR path,LPCWSTR section,LPCWSTR key,LPCWSTR fallback,WCHAR* out,int capacity){
 WCHAR canonical[80],value[1024];
 if(swprintf_s(canonical,_countof(canonical),L"%ls.%ls",section,key)<0){out[0]=0;return;}
 GetPrivateProfileStringW(L"Plugins",canonical,L"\x1d",value,_countof(value),path);
 if(wcscmp(value,L"\x1d"))wcsncpy_s(out,capacity,value,_TRUNCATE);
 else GetPrivateProfileStringW(section,key,fallback,out,capacity,path);
}
static LONG preg_known_int_w(LPCWSTR path,LPCWSTR section,LPCWSTR key,LONG fallback){
 WCHAR canonical[80],value[32];
 if(swprintf_s(canonical,_countof(canonical),L"%ls.%ls",section,key)<0)return fallback;
 GetPrivateProfileStringW(L"Plugins",canonical,L"\x1d",value,_countof(value),path);
 if(wcscmp(value,L"\x1d"))return (LONG)GetPrivateProfileIntW(L"Plugins",canonical,fallback,path);
 return (LONG)GetPrivateProfileIntW(section,key,fallback,path);
}
static int preg_known_w(LPCWSTR path,PLUGIN_RECORD* records,int capacity){
 int i,count=0;for(i=0;i<PREG_KNOWN&&count<capacity;++i){
  PLUGIN_RECORD* r=&records[count++];WCHAR section[32],key[80];LONG enabled;
  ZeroMemory(r,sizeof(*r));r->known=i;r->kind=PREG_KIND_KNOWN;
  tc_utf8_to_utf16(preg_sections[i],section,_countof(section));wcscpy_s(r->id,_countof(r->id),section);wcscpy_s(r->name,_countof(r->name),section);
  preg_known_text_w(path,section,L"Path",preg_files[i],r->path,_countof(r->path));if(!r->path[0])wcscpy_s(r->path,_countof(r->path),preg_files[i]);
  enabled=preg_known_int_w(path,section,L"Enable",i==3?-1:0);if(i==3&&enabled==-1)enabled=(LONG)GetPrivateProfileIntW(L"ETC",L"TCaptureEnable",0,path);r->enabled=enabled!=0;
  if(i==1||i==3)wcscpy_s(r->settings,_countof(r->settings),L"--settings");
  swprintf_s(key,_countof(key),L"%ls.Ini",section);GetPrivateProfileStringW(L"Plugins",key,L"",r->ini,_countof(r->ini),path);
  swprintf_s(key,_countof(key),L"%ls.Registered",section);
  r->visible=preg_is_present(r)||GetPrivateProfileIntW(L"Plugins",key,0,path)||r->enabled||_wcsicmp(r->path,preg_files[i])!=0;
 }return count;
}
static BOOL preg_read_v0(LPCWSTR registration,int slot,PLUGIN_RECORD* record){
 WCHAR section[32],relative[MAX_PATH],directory[MAX_PATH];
 ZeroMemory(record,sizeof(*record));record->known=-1;record->slot=slot;
 if(swprintf_s(section,_countof(section),L"Plugin%d",slot)<0)return FALSE;
 if(!GetPrivateProfileStringW(section,L"Path",L"",relative,MAX_PATH,registration))return FALSE;
 wcscpy_s(directory,MAX_PATH,registration);if(!PathRemoveFileSpecW(directory))return FALSE;
 if(PathIsRelativeW(relative)){if(wcslen(directory)+wcslen(relative)+2>MAX_PATH||!PathCombineW(record->path,directory,relative))return FALSE;}
 else if(wcscpy_s(record->path,MAX_PATH,relative))return FALSE;
 swprintf_s(record->id,_countof(record->id),L"legacy.dll.%d",slot);wcscpy_s(record->name,_countof(record->name),PathFindFileNameW(record->path));
 record->kind=PREG_KIND_DLL;record->enabled=GetPrivateProfileIntW(section,L"Enabled",0,registration)!=0;record->visible=TRUE;return TRUE;
}
static BOOL preg_load_v0(LPCWSTR active,BOOL scope,PLUGIN_RECORD** result,int* resultCount){
 WCHAR registration[MAX_PATH],*names=NULL,*entry;HANDLE file=INVALID_HANDLE_VALUE;int* slots=NULL;PLUGIN_RECORD* rows=NULL;
 int chars=256,slotCount=0,slotCap=0,capacity,count,i;DWORD n,maximum=0,error;BOOL ok=FALSE;
 if(!preg_legacy_path(registration))return FALSE;
 file=CreateFileW(registration,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE){error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)goto cleanup;}
 else {
  maximum=GetPrivateProfileIntW(L"Plugins",L"Count",0,registration);if(maximum>INT_MAX)goto cleanup;
  for(;;){WCHAR* next=names?(WCHAR*)HeapReAlloc(GetProcessHeap(),0,names,(SIZE_T)chars*sizeof(WCHAR)):(WCHAR*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)chars*sizeof(WCHAR));
   if(!next)goto cleanup;names=next;SetLastError(ERROR_SUCCESS);n=GetPrivateProfileSectionNamesW(names,(DWORD)chars,registration);
   if(!n&&GetLastError()!=ERROR_SUCCESS)goto cleanup;if(n!=(DWORD)(chars-2))break;if(chars>INT_MAX/2)goto cleanup;chars*=2;
  }
  for(entry=names;*entry;entry+=wcslen(entry)+1){const WCHAR* ch=entry+6;int value=0;
   if(_wcsnicmp(entry,L"Plugin",6)||*ch<L'1'||*ch>L'9')continue;
   while(*ch>=L'0'&&*ch<=L'9'){int digit=*ch-L'0';if(value>(INT_MAX-digit)/10){value=0;break;}value=value*10+digit;++ch;}
   if(!value||*ch||(DWORD)value>maximum)continue;
   if(slotCount==slotCap){int nextCap;int* next;if(slotCap>INT_MAX/2)goto cleanup;nextCap=slotCap?slotCap*2:32;
    if(nextCap>INT_MAX/(int)sizeof(int))goto cleanup;
    next=slots?(int*)HeapReAlloc(GetProcessHeap(),0,slots,(SIZE_T)nextCap*sizeof(int)):(int*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)nextCap*sizeof(int));
    if(!next)goto cleanup;slots=next;slotCap=nextCap;}
   slots[slotCount++]=value;
  }
 }
 if(slotCount)qsort(slots,slotCount,sizeof(*slots),preg_cmp_slot);
 if(slotCount>INT_MAX-PREG_KNOWN-preg_inventory_count)goto cleanup;capacity=PREG_KNOWN+slotCount+preg_inventory_count;
 if((SIZE_T)capacity>((SIZE_T)-1)/sizeof(*rows))goto cleanup;
 rows=(PLUGIN_RECORD*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)capacity*sizeof(*rows));if(!rows)goto cleanup;
 count=scope?preg_load(rows,PREG_KNOWN):preg_known_w(active,rows,PREG_KNOWN);
 for(i=0;i<slotCount;++i)if((!i||slots[i]!=slots[i-1])&&preg_read_v0(registration,slots[i],&rows[count]))++count;
 count=preg_merge_plugins(rows,count,capacity);*result=rows;*resultCount=count;rows=NULL;ok=TRUE;
cleanup:
 if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(names)HeapFree(GetProcessHeap(),0,names);if(slots)HeapFree(GetProcessHeap(),0,slots);if(rows)HeapFree(GetProcessHeap(),0,rows);return ok;
}
BOOL preg_load_all(PLUGIN_RECORD** result,int* resultCount){
 WCHAR active[MAX_PATH];char* section=NULL;int* slots=NULL;PLUGIN_RECORD* rows=NULL;
 int bytes=4096,slotCount=0,slotCap=0,count=0,capacity,i;BOOL truncated,scope=FALSE,ok=FALSE;HANDLE legacy=INVALID_HANDLE_VALUE;
 char* entry;
 if(!result||!resultCount)return FALSE;*result=NULL;*resultCount=0;
 if(tc_utf8_to_utf16(g_inifile,active,MAX_PATH)<=0)return FALSE;
 scope=tc_ini_beginW(active);if(!scope&&!preg_legacy_section(active,&legacy,&section))return FALSE;
 if(!preg_scanned)preg_discover();
 if(preg_scan_failed)goto cleanup;
 if((scope?GetMyRegLong("Plugins","RegistryVersion",0):(LONG)GetPrivateProfileIntW(L"Plugins",L"RegistryVersion",0,active))<1){
  if(!preg_load_v0(active,scope,&rows,&count))goto cleanup;ok=TRUE;goto cleanup;
 }
 if(scope)for(;;){
  char* next=section?(char*)HeapReAlloc(GetProcessHeap(),0,section,bytes):(char*)HeapAlloc(GetProcessHeap(),0,bytes);
  if(!next)goto cleanup;section=next;truncated=FALSE;
  tc_ini_utf8_read_section_multisz_ex(g_inifile,"Plugins",section,bytes,&truncated);
  if(!truncated)break;if(bytes>INT_MAX/2)goto cleanup;bytes*=2;
 }
 for(entry=section;*entry;entry+=strlen(entry)+1){
  const char* p=entry+4;int value=0;char canonical[64];
  if(_strnicmp(entry,"Item",4)||*p<'1'||*p>'9')continue;
  while(*p>='0'&&*p<='9'){int digit=*p-'0';if(value>(INT_MAX-digit)/10){value=0;break;}value=value*10+digit;++p;}
  if(!value||_strnicmp(p,".Id=",4))continue;
  sprintf_s(canonical,sizeof(canonical),"Item%d.Id=",value);if(_strnicmp(entry,canonical,strlen(canonical)))continue;
  if(slotCount==slotCap){int newCap;int* next;if(slotCap>INT_MAX/2)goto cleanup;newCap=slotCap?slotCap*2:32;if(newCap>INT_MAX/(int)sizeof(int))goto cleanup;
   next=slots?(int*)HeapReAlloc(GetProcessHeap(),0,slots,(SIZE_T)newCap*sizeof(int)):(int*)HeapAlloc(GetProcessHeap(),0,(SIZE_T)newCap*sizeof(int));
   if(!next)goto cleanup;slots=next;slotCap=newCap;}
  slots[slotCount++]=value;
 }
 if(slotCount)qsort(slots,slotCount,sizeof(*slots),preg_cmp_slot);
 if(slotCount>INT_MAX-PREG_KNOWN-preg_inventory_count)goto cleanup;
 capacity=PREG_KNOWN+slotCount+preg_inventory_count;
 if((SIZE_T)capacity>((SIZE_T)-1)/sizeof(*rows))goto cleanup;
 rows=(PLUGIN_RECORD*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)capacity*sizeof(*rows));if(!rows)goto cleanup;
 count=scope?preg_load(rows,PREG_KNOWN):preg_known_w(active,rows,PREG_KNOWN);
 for(i=0;i<slotCount;++i)if((i==0||slots[i]!=slots[i-1])&&(scope?preg_read_custom(slots[i],&rows[count]):preg_read_custom_w(active,slots[i],&rows[count])))++count;
 count=preg_merge_plugins(rows,count,capacity);ok=TRUE;
cleanup:
 if(scope)tc_ini_end();if(legacy!=INVALID_HANDLE_VALUE)CloseHandle(legacy);if(section)HeapFree(GetProcessHeap(),0,section);if(slots)HeapFree(GetProcessHeap(),0,slots);
 if(!ok){if(rows)HeapFree(GetProcessHeap(),0,rows);return FALSE;}
 *result=rows;*resultCount=count;return TRUE;
}
BOOL preg_get_dll(UINT slot,WCHAR* id,WCHAR* path) {
    PLUGIN_RECORD* records=NULL;int i,count;BOOL found=FALSE;
    if(!id||!path||!slot||slot>INT_MAX||!preg_load_all(&records,&count))return FALSE;
    for(i=PREG_KNOWN;i<count;++i)if(records[i].slot==(int)slot&&records[i].kind==PREG_KIND_DLL&&records[i].enabled&&!records[i].conflict&&(!records[i].automatic||records[i].discovered)&&preg_resolve(records[i].path,path)){wcscpy_s(id,64,records[i].id);found=TRUE;break;}
    HeapFree(GetProcessHeap(),0,records);return found;
}
typedef struct PREG_BATCH { char* bytes; DWORD used,capacity; } PREG_BATCH;
static BOOL preg_append(PREG_BATCH* batch,const char* key,LPCWSTR text) {
    char encoded[8192]={0};SIZE_T keylen,valuelen,needed;DWORD capacity;char* grown;
    if(tc_utf16_to_utf8(text,encoded,sizeof(encoded))<=0 && text[0])return FALSE;
    keylen=strlen(key);valuelen=strlen(encoded);
    if(keylen>(SIZE_T)MAXDWORD-3 || valuelen>(SIZE_T)MAXDWORD-3-keylen ||
       batch->used>(SIZE_T)MAXDWORD-3-keylen-valuelen)return FALSE;
    needed=(SIZE_T)batch->used+keylen+valuelen+3;
    if(needed>batch->capacity) {
        capacity=batch->capacity?batch->capacity:4096;
        while(capacity<needed) {
            if(capacity>MAXDWORD/2){capacity=(DWORD)needed;break;}
            capacity*=2;
        }
        grown=batch->bytes?(char*)HeapReAlloc(GetProcessHeap(),0,batch->bytes,capacity):
            (char*)HeapAlloc(GetProcessHeap(),0,capacity);
        if(!grown)return FALSE;
        batch->bytes=grown;batch->capacity=capacity;
    }
    memcpy(batch->bytes+batch->used,key,keylen);batch->used+=(DWORD)keylen;
    batch->bytes[batch->used++]='=';
    memcpy(batch->bytes+batch->used,encoded,valuelen);batch->used+=(DWORD)valuelen;
    batch->bytes[batch->used++]=0;batch->bytes[batch->used]=0;return TRUE;
}
BOOL preg_save(const PLUGIN_RECORD* records,int count) {
    PREG_BATCH batch={0};WCHAR active[MAX_PATH],slotCount[16];int i,j;BOOL ok=FALSE;
    LONG highest=GetMyRegLong("Plugins","Count",PREG_MAX_CUSTOM);char key[80];
    if(tc_utf8_to_utf16(g_inifile,active,MAX_PATH)<=0)return FALSE;
    if(highest<PREG_MAX_CUSTOM)highest=PREG_MAX_CUSTOM;
    for(i=0;i<count;++i)if(records[i].known<0 && records[i].visible && records[i].slot>highest)highest=records[i].slot;
    if(swprintf_s(slotCount,_countof(slotCount),L"%ld",highest)<0)return FALSE;
    if(!preg_append(&batch,"RegistryVersion",L"1") || !preg_append(&batch,"Count",slotCount))goto cleanup;
    for(i=0;i<count;++i) {
        const PLUGIN_RECORD* r=&records[i];if(!r->visible)continue;
        if(r->known>=0) {
            sprintf_s(key,sizeof(key),"%s.Enable",preg_sections[r->known]);if(!preg_append(&batch,key,r->enabled?L"1":L"0"))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Path",preg_sections[r->known]);if(!preg_append(&batch,key,r->path))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Registered",preg_sections[r->known]);if(!preg_append(&batch,key,L"1"))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Ini",preg_sections[r->known]);if(!preg_append(&batch,key,r->ini))goto cleanup;
        } else {
            const char* fields[]={"Id","Name","Type","Enabled","Path","Args","SettingsArgs","Ini"};
            const WCHAR* values[]={r->id,r->name,r->kind==PREG_KIND_DLL?L"dll":L"exe",r->enabled?L"1":L"0",r->path,r->args,r->settings,r->ini};
            for(j=0;j<8;++j){sprintf_s(key,sizeof(key),"Item%d.%s",r->slot,fields[j]);if(!preg_append(&batch,key,values[j]))goto cleanup;}
        }
    }
    for(i=1;i<=PREG_MAX_CUSTOM;++i) {
        BOOL found=FALSE;
        for(j=0;j<count;++j)if(records[j].known<0 && records[j].visible && records[j].slot==i){found=TRUE;break;}
        if(!found){sprintf_s(key,sizeof(key),"Item%d.Id",i);if(!preg_append(&batch,key,L""))goto cleanup;}
    }
    /* Clear explicitly supplied invisible slots beyond the legacy range without touching omitted entries. */
    for(i=0;i<count;++i)if(records[i].known<0 && !records[i].visible && records[i].slot>PREG_MAX_CUSTOM) {
        BOOL found=FALSE;
        for(j=0;j<count;++j)if(records[j].known<0 && records[j].visible && records[j].slot==records[i].slot){found=TRUE;break;}
        if(!found){sprintf_s(key,sizeof(key),"Item%d.Id",records[i].slot);if(!preg_append(&batch,key,L""))goto cleanup;}
    }
    if(!tc_write_batchW(active,"Plugins",batch.bytes,batch.used+1))goto cleanup;
    ok=TRUE;
cleanup:
    if(batch.bytes)HeapFree(GetProcessHeap(),0,batch.bytes);
    if(ok)prt_note_setting("Plugins",NULL);return ok;
}
BOOL preg_open(const PLUGIN_RECORD* record,HWND owner,BOOL settings,BOOL english) {
    WCHAR path[MAX_PATH],parameters[1024];LPCWSTR args=record->args;
    if(settings && record->kind==PREG_KIND_DLL) {
        if(record->conflict||(record->automatic&&!record->discovered))return FALSE;
        HRESULT result=prt_show_settings(record->id,record->path,record->ini,owner,english);
        if(result!=E_NOINTERFACE)return SUCCEEDED(result);
    }
    if(settings && record->ini[0]) {
        if(!preg_resolve(record->ini,path))return FALSE;
        return (INT_PTR)ShellExecuteW(owner,L"open",path,NULL,NULL,SW_SHOWNORMAL)>32;
    }
    if(record->kind==PREG_KIND_DLL || !preg_resolve(record->path,path))return FALSE;
    if(settings) {
        args=record->settings;
        if(record->known==1 || record->known==3){swprintf_s(parameters,_countof(parameters),L"--settings --lang %ls",english?L"en":L"ja");args=parameters;}
    }
    return (INT_PTR)ShellExecuteW(owner,L"open",path,args[0]?args:NULL,NULL,SW_SHOWNORMAL)>32;
}
#define PREG_ACTION_BLOCK_SIZE 32
typedef struct PREG_ACTION_ENTRY {char key[96],label[512];ULONGLONG generation;} PREG_ACTION_ENTRY;
typedef struct PREG_ACTION_BLOCK {struct PREG_ACTION_BLOCK* next;PREG_ACTION_ENTRY entries[PREG_ACTION_BLOCK_SIZE];} PREG_ACTION_BLOCK;
static PREG_ACTION_BLOCK *preg_action_first,*preg_action_last;static int preg_actions_used;
static ULONGLONG preg_action_generation,preg_action_snapshot;
static PREG_ACTION_ENTRY* preg_action_at(int index){
 PREG_ACTION_BLOCK* block=preg_action_first;int n;
 if(index<0||index>=preg_actions_used)return NULL;
 for(n=index/PREG_ACTION_BLOCK_SIZE;n>0&&block;--n)block=block->next;
 return block?&block->entries[index%PREG_ACTION_BLOCK_SIZE]:NULL;
}
static PREG_ACTION_ENTRY* preg_action_find(const char* key){
 PREG_ACTION_BLOCK* block=preg_action_first;int offset=0;
 while(block){int n;for(n=0;n<PREG_ACTION_BLOCK_SIZE&&offset<preg_actions_used;++n,++offset)if(strcmp(key,block->entries[n].key)==0)return &block->entries[n];block=block->next;}
 return NULL;
}
static PREG_ACTION_ENTRY* preg_action_add(const char* key){
 PREG_ACTION_ENTRY* entry=preg_action_find(key);PREG_ACTION_BLOCK* block;if(entry)return entry;
 if(preg_actions_used==INT_MAX)return NULL;
 if(preg_actions_used%PREG_ACTION_BLOCK_SIZE==0){
  block=(PREG_ACTION_BLOCK*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*block));if(!block)return NULL;
  if(preg_action_last)preg_action_last->next=block;else preg_action_first=block;preg_action_last=block;
 }
 entry=&preg_action_last->entries[preg_actions_used%PREG_ACTION_BLOCK_SIZE];
 if(strcpy_s(entry->key,sizeof(entry->key),key))return NULL;++preg_actions_used;return entry;
}
static BOOL preg_find_action(const char* key,PLUGIN_RECORD* found) {
 PLUGIN_RECORD* records=NULL;int count,i;BOOL matched=FALSE;
 if(!preg_load_all(&records,&count))return FALSE;
 for(i=PREG_KNOWN;i<count;++i){PLUGIN_RECORD* record=&records[i];char id[256],candidate[96];
  if(record->kind!=PREG_KIND_EXE||!record->enabled||!preg_is_present(record))continue;
  if(tc_utf16_to_utf8(record->id,id,sizeof(id))<=0||strlen(id)>70)continue;
  sprintf_s(candidate,sizeof(candidate),"exe:%s/open",id);
  if(strcmp(candidate,key)==0){*found=*record;matched=TRUE;break;}
 }
 HeapFree(GetProcessHeap(),0,records);return matched;
}
int preg_count_actions(void){
 PLUGIN_RECORD* records=NULL;int count,i;BOOL complete=TRUE;ULONGLONG generation;
 if(GetMyRegLong("ETC","MinimalMode",0)||GetMyRegLong("Status_DoNotEdit","SafeMode",0)){preg_action_snapshot=0;return 0;}
 if(!preg_load_all(&records,&count)){preg_action_snapshot=0;return 0;}
 if(++preg_action_generation==0){PREG_ACTION_BLOCK* block;for(block=preg_action_first;block;block=block->next){int n;for(n=0;n<PREG_ACTION_BLOCK_SIZE;++n)block->entries[n].generation=0;}preg_action_generation=1;}
 generation=preg_action_generation;
 for(i=PREG_KNOWN;i<count;++i){PLUGIN_RECORD* record=&records[i];PREG_ACTION_ENTRY* entry;char id[256],key[96];
  if(record->kind!=PREG_KIND_EXE)continue;
  if(tc_utf16_to_utf8(record->id,id,sizeof(id))<=0||strlen(id)>70)continue;
  sprintf_s(key,sizeof(key),"exe:%s/open",id);entry=preg_action_add(key);if(!entry){complete=FALSE;break;}
  if(entry->generation!=generation&&record->enabled&&preg_is_present(record)){
   entry->generation=generation;entry->label[0]=0;tc_utf16_to_utf8(record->name,entry->label,sizeof(entry->label));
  }
 }
 HeapFree(GetProcessHeap(),0,records);preg_action_snapshot=complete?generation:0;return complete?preg_actions_used:0;
}
const char* preg_get_action(int index){PREG_ACTION_ENTRY* entry=preg_action_at(index);return entry?entry->key:NULL;}
const char* preg_get_cached_label(int index){PREG_ACTION_ENTRY* entry=preg_action_at(index);return entry&&preg_action_snapshot&&entry->generation==preg_action_snapshot&&entry->label[0]?entry->label:NULL;}
const char* preg_get_label(const char* key){
 PLUGIN_RECORD record;int i;
 if(!key||strncmp(key,"exe:",4)!=0)return NULL;preg_count_actions();
 if(GetMyRegLong("ETC","MinimalMode",0)||GetMyRegLong("Status_DoNotEdit","SafeMode",0)||!preg_find_action(key,&record))return NULL;
 for(i=0;i<preg_actions_used;++i){PREG_ACTION_ENTRY* entry=preg_action_at(i);if(entry&&strcmp(key,entry->key)==0){if(tc_utf16_to_utf8(record.name,entry->label,sizeof(entry->label))>0)return entry->label;break;}}
 return NULL;
}
BOOL preg_run_action(const char* key,HWND owner) {
    PLUGIN_RECORD record;
    return preg_get_label(key)&&preg_find_action(key,&record)&&preg_open(&record,owner,FALSE,FALSE);
}

static BOOL preg_compare_files(LPCWSTR left,LPCWSTR right) {
    HANDLE a=CreateFileW(left,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    HANDLE b=CreateFileW(right,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    BYTE x[4096],y[4096];DWORD nx,ny;BOOL equal=FALSE;
    if(a==INVALID_HANDLE_VALUE||b==INVALID_HANDLE_VALUE)goto done;
    for(;;){if(!ReadFile(a,x,sizeof(x),&nx,NULL)||!ReadFile(b,y,sizeof(y),&ny,NULL)||nx!=ny||memcmp(x,y,nx))goto done;if(!nx){equal=TRUE;break;}}
done:
    if(a!=INVALID_HANDLE_VALUE)CloseHandle(a);if(b!=INVALID_HANDLE_VALUE)CloseHandle(b);return equal;
}
BOOL preg_migrate(void) {
    WCHAR active[MAX_PATH],backup[MAX_PATH+80];char* section=NULL;BOOL ok=FALSE,backed=FALSE;int i;
    if(preg_migrating)return TRUE;
    if(tc_utf8_to_utf16(g_inifile,active,MAX_PATH)<=0)return FALSE;
    section=(char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,65536);if(!section)return FALSE;
    preg_migrating=TRUE;
    for(i=0;i<PREG_KNOWN;++i){
        BOOL truncated=FALSE;char* entry;
        /* Existing active-INI UTF-8 persistence boundary; public path handling is W. */
        tc_ini_utf8_read_section_multisz_ex(g_inifile,preg_sections[i],section,65536,&truncated);
        if(truncated)goto cleanup;
        for(entry=section;*entry;entry+=strlen(entry)+1){
            char* equal=strchr(entry,'=');char key[128],oldKey[128],current[8192];char* batch;DWORD used;BOOL keep,missing;
            size_t length;if(!equal)continue;length=(size_t)(equal-entry);if(!length||length>=sizeof(oldKey))goto cleanup;
            memcpy(oldKey,entry,length);oldKey[length]=0;
            if(strlen(preg_sections[i])+length+2>sizeof(key))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.%s",preg_sections[i],oldKey);keep=preg_keep_key(preg_sections[i],oldKey);
            tc_ini_utf8_read_string(g_inifile,"Plugins",key,"\x1d",current,sizeof(current));missing=strcmp(current,"\x1d")==0;
            if(keep&&!missing&&strcmp(current,equal+1)==0)continue;
            if(!backed){
                int attempt;for(attempt=0;attempt<16;++attempt){
                    if(swprintf_s(backup,_countof(backup),L"%ls.plugins-migration-%lu-%lu-%d.bak",active,GetCurrentProcessId(),GetTickCount(),attempt)<0)goto cleanup;
                    if(CopyFileW(active,backup,TRUE)){if(!preg_compare_files(active,backup))goto cleanup;backed=TRUE;break;}
                    if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)goto cleanup;
                }
                if(!backed)goto cleanup;
            }
            if(missing||keep){
                used=(DWORD)(strlen(key)+strlen(equal+1)+2);batch=(char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)used+1);if(!batch)goto cleanup;
                sprintf_s(batch,(SIZE_T)used+1,"%s=%s",key,equal+1);
                if(!tc_write_batchW(active,"Plugins",batch,used+1)){HeapFree(GetProcessHeap(),0,batch);goto cleanup;}HeapFree(GetProcessHeap(),0,batch);
                tc_ini_utf8_read_string(g_inifile,"Plugins",key,"\x1d",current,sizeof(current));if(strcmp(current,equal+1)!=0)goto cleanup;
            }
            if(!keep){
                char latest[8192];tc_ini_utf8_read_string(g_inifile,preg_sections[i],oldKey,"\x1d",latest,sizeof(latest));
                if(strcmp(latest,equal+1)!=0)goto cleanup;
                if(!DelMyReg(preg_sections[i],oldKey))goto cleanup;
            }
        }
        section[0]=section[1]=0;tc_ini_utf8_read_section_multisz_ex(g_inifile,preg_sections[i],section,65536,&truncated);
        if(!section[0]&&!DelMyRegKey(preg_sections[i]))goto cleanup;
    }
    {
        char legacy[64],current[64],batch[160];
        const char* key="ExitExtensionsOnTClockExit";
        tc_ini_utf8_read_string(g_inifile,"ETC",key,"\x1d",legacy,sizeof(legacy));
        if(strcmp(legacy,"\x1d")!=0){
            if(!backed){
                int attempt;
                for(attempt=0;attempt<16;++attempt){
                    if(swprintf_s(backup,_countof(backup),L"%ls.plugins-migration-%lu-%lu-%d.bak",active,GetCurrentProcessId(),GetTickCount(),attempt)<0)goto cleanup;
                    if(CopyFileW(active,backup,TRUE)){if(!preg_compare_files(active,backup))goto cleanup;backed=TRUE;break;}
                    if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)goto cleanup;
                }
                if(!backed)goto cleanup;
            }
            tc_ini_utf8_read_string(g_inifile,"Plugins",key,"\x1d",current,sizeof(current));
            if(strcmp(current,"\x1d")==0){
                int used=sprintf_s(batch,sizeof(batch),"%s=%s",key,legacy);
                if(used<0)goto cleanup;batch[used+1]=0;
                if(!tc_write_batchW(active,"Plugins",batch,(DWORD)used+2))goto cleanup;
                tc_ini_utf8_read_string(g_inifile,"Plugins",key,"\x1d",current,sizeof(current));
                if(strcmp(current,legacy)!=0)goto cleanup;
            }
            if(!DelMyReg("ETC",key))goto cleanup;
        }
    }
    ok=TRUE;
cleanup:
    preg_migrating=FALSE;HeapFree(GetProcessHeap(),0,section);return ok;
}

BOOL preg_get_version(const PLUGIN_RECORD* record,WCHAR* version,int capacity) {
    WCHAR path[MAX_PATH];DWORD size;void* data;VS_FIXEDFILEINFO* info=NULL;UINT length=0;BOOL ok=FALSE;
    if(!version||capacity<=0)return FALSE;version[0]=0;
    if(!preg_resolve(record->path,path))return FALSE;
    size=GetFileVersionInfoSizeW(path,NULL);if(!size)return FALSE;
    data=HeapAlloc(GetProcessHeap(),0,size);if(!data)return FALSE;
    if(GetFileVersionInfoW(path,0,size,data)&&VerQueryValueW(data,L"\\",(void**)&info,&length)&&
       length>=sizeof(*info)&&info->dwSignature==0xfeef04bd) {
        ok=swprintf_s(version,capacity,L"%u.%u.%u.%u",HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS))>=0;
    }
    HeapFree(GetProcessHeap(),0,data);return ok;
}
int preg_stop_tools(void) {
    PLUGIN_RECORD records[PREG_KNOWN];int count=preg_load(records,PREG_KNOWN),i,stopped=0;
    for(i=1;i<count;++i) {
        WCHAR target[MAX_PATH];HANDLE snapshot;PROCESSENTRY32W entry={sizeof(entry)};
        if(!records[i].enabled||!preg_resolve(records[i].path,target))continue;
        snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)continue;
        if(Process32FirstW(snapshot,&entry))do {
            HANDLE process;WCHAR image[MAX_PATH];DWORD length=_countof(image);
            if(_wcsicmp(entry.szExeFile,PathFindFileNameW(target))!=0||entry.th32ProcessID==GetCurrentProcessId())continue;
            process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE,FALSE,entry.th32ProcessID);
            if(!process)continue;
            /* Preserve the existing explicit-exit semantics, scoped to each enabled
               integration's full path and held process identity instead of /IM. */
            if(QueryFullProcessImageNameW(process,0,image,&length)&&_wcsicmp(image,target)==0&&TerminateProcess(process,0))++stopped;
            CloseHandle(process);
        }while(Process32NextW(snapshot,&entry));
        CloseHandle(snapshot);
    }
    return stopped;
}

/* Resource-only metadata never queries interfaces or initializes a DLL. */
typedef struct PREG_LANGUAGES { WORD ids[5];BOOL present[5]; } PREG_LANGUAGES;
static BOOL CALLBACK preg_mark_language(HMODULE module,LPCWSTR type,LPCWSTR name,WORD language,LONG_PTR data) {
    PREG_LANGUAGES* choices=(PREG_LANGUAGES*)data;int i;
    (void)module;(void)type;(void)name;
    for(i=0;i<5;++i)if(choices->ids[i]==language)choices->present[i]=TRUE;
    return TRUE;
}
static BOOL preg_read_resource(HMODULE module,UINT id,WORD language,WCHAR* out,int capacity) {
    HRSRC resource;HGLOBAL loaded;DWORD size;const WCHAR* text;size_t remaining,length,i,take;UINT entry;
    SetLastError(ERROR_SUCCESS);resource=FindResourceExW(module,MAKEINTRESOURCEW(6),MAKEINTRESOURCEW((id>>4)+1),language);
    if(!resource){if(GetLastError()==ERROR_SUCCESS)SetLastError(ERROR_READ_FAULT);return FALSE;}
    size=SizeofResource(module,resource);if(!size||size>65536||size%sizeof(WCHAR)){SetLastError(ERROR_INVALID_DATA);return FALSE;}
    SetLastError(ERROR_SUCCESS);loaded=LoadResource(module,resource);if(!loaded){if(GetLastError()==ERROR_SUCCESS)SetLastError(ERROR_READ_FAULT);return FALSE;}
    text=(const WCHAR*)LockResource(loaded);if(!text){SetLastError(ERROR_READ_FAULT);return FALSE;}remaining=size/sizeof(WCHAR);SetLastError(ERROR_INVALID_DATA);
    for(entry=0;entry<=(id&15);++entry) {
        if(!remaining)return FALSE;length=*text++;--remaining;
        if(length>remaining)return FALSE;
        if(entry==(id&15)) {
            if(!length)return FALSE;
            for(i=0;i<length;++i) {
                WCHAR ch=text[i];if(!ch)return FALSE;
                if(ch>=0xd800&&ch<=0xdbff) {
                    if(i+1>=length||text[i+1]<0xdc00||text[i+1]>0xdfff)return FALSE;++i;
                } else if(ch>=0xdc00&&ch<=0xdfff)return FALSE;
            }
            take=min(length,(size_t)capacity-1);
            if(take&&text[take-1]>=0xd800&&text[take-1]<=0xdbff)--take;
            if(!take)return FALSE;memcpy(out,text,take*sizeof(WCHAR));out[take]=0;return TRUE;
        }
        text+=length;remaining-=length;
    }
    return FALSE;
}
static BOOL preg_get_resource(LPCWSTR file,UINT id,BOOL english,BOOL invariant,WCHAR* out,int capacity) {
    WCHAR path[MAX_PATH];HMODULE module;PREG_LANGUAGES choices={0};BOOL ok=FALSE;int i,j;DWORD error=ERROR_RESOURCE_DATA_NOT_FOUND;
    if(!out||capacity<=0){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}out[0]=0;
    if(capacity<2||!preg_resolve(file,path)){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);if(!module)return FALSE;
    if(!invariant) {
        choices.ids[0]=MAKELANGID(english?LANG_ENGLISH:LANG_JAPANESE,english?SUBLANG_ENGLISH_US:SUBLANG_DEFAULT);
        choices.ids[1]=MAKELANGID(english?LANG_ENGLISH:LANG_JAPANESE,SUBLANG_NEUTRAL);
        choices.ids[2]=MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
        choices.ids[3]=MAKELANGID(LANG_ENGLISH,SUBLANG_NEUTRAL);
    }
    choices.ids[4]=MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL);
    SetLastError(ERROR_SUCCESS);
    if(!EnumResourceLanguagesW(module,MAKEINTRESOURCEW(6),MAKEINTRESOURCEW((id>>4)+1),preg_mark_language,(LONG_PTR)&choices)){
        error=GetLastError();if(error==ERROR_SUCCESS)error=ERROR_READ_FAULT;goto done;
    }
    for(i=0;i<5;++i)if(choices.present[i]) {
        for(j=0;j<i;++j)if(choices.ids[j]==choices.ids[i])break;if(j<i)continue;
        if(preg_read_resource(module,id,choices.ids[i],out,capacity)){ok=TRUE;break;}
        error=GetLastError();if(error!=ERROR_INVALID_DATA&&error!=ERROR_RESOURCE_DATA_NOT_FOUND&&error!=ERROR_RESOURCE_NAME_NOT_FOUND&&error!=ERROR_RESOURCE_LANG_NOT_FOUND)goto done;
    }
done:
    FreeLibrary(module);SetLastError(ok?ERROR_SUCCESS:error);return ok;
}
BOOL preg_get_description(const PLUGIN_RECORD* record,BOOL english,WCHAR* description,int capacity) {
    if(!record){if(description&&capacity>0)description[0]=0;return FALSE;}
    return preg_get_resource(record->path,TC_PLUGIN_DESCRIPTION_ID,english,FALSE,description,capacity);
}
BOOL preg_can_configure(const PLUGIN_RECORD* record) {
    WCHAR path[MAX_PATH],hint[32];
    if(!record||record->conflict||(!preg_is_present(record)&&!record->ini[0]))return FALSE;
    if(record->kind==PREG_KIND_DLL&&preg_resolve(record->path,path)&&
        preg_get_resource(path,TC_PLUGIN_SETTINGS_HINT_ID,TRUE,TRUE,hint,_countof(hint))&&wcscmp(hint,L"none")==0)return FALSE;
    return TRUE;
}

BOOL preg_get_identity(LPCWSTR file,WCHAR* id,int capacity) {
    static const WCHAR prefix[]=L"tclock.plugin/1:";WCHAR header[128];const WCHAR* ch;size_t length,i;
    if(!id||capacity<=0){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}id[0]=0;
    if(!preg_get_resource(file,TC_PLUGIN_HEADER_ID,TRUE,TRUE,header,128))return FALSE;
    if(wcsncmp(header,prefix,_countof(prefix)-1)){SetLastError(ERROR_INVALID_DATA);return FALSE;}
    ch=header+_countof(prefix)-1;length=wcslen(ch);if(!length||length>=32||length>=(size_t)capacity){SetLastError(ERROR_INVALID_DATA);return FALSE;}
    for(i=0;i<length;++i)if(!((ch[i]>=L'a'&&ch[i]<=L'z')||(ch[i]>=L'0'&&ch[i]<=L'9')||ch[i]==L'.'||ch[i]==L'-'||ch[i]==L'_')){SetLastError(ERROR_INVALID_DATA);return FALSE;}
    wcscpy_s(id,capacity,ch);SetLastError(ERROR_SUCCESS);return TRUE;
}
void preg_get_title(const PLUGIN_RECORD* record,BOOL english,WCHAR* title,int capacity) {
    if(!title||capacity<=0)return;title[0]=0;if(!record)return;
    if(!preg_get_resource(record->path,TC_PLUGIN_NAME_ID,english,FALSE,title,capacity))wcsncpy_s(title,capacity,record->name,_TRUNCATE);
}
