#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
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
static PREG_DISCOVERY preg_inventory[PREG_MAX_CUSTOM];
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
static ULONGLONG preg_hash_folder(void) {
    WCHAR pattern[MAX_PATH];WIN32_FIND_DATAW item;HANDLE search;ULONGLONG result=0;
    if(!preg_resolve(L"plugins\\*.dll",pattern))return 0;
    search=FindFirstFileW(pattern,&item);if(search==INVALID_HANDLE_VALUE)return 0;
    do { if(!(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        const WCHAR* ch=item.cFileName;ULONGLONG hash=1469598103934665603ULL;
        while(*ch){hash=(hash^*ch++)*1099511628211ULL;}
        hash^=((ULONGLONG)item.ftLastWriteTime.dwHighDateTime<<32)|item.ftLastWriteTime.dwLowDateTime;
        hash^=(((ULONGLONG)item.nFileSizeHigh<<32)|item.nFileSizeLow)*1099511628211ULL;result^=hash;
    }}while(FindNextFileW(search,&item));
    FindClose(search);return result;
}
static int __cdecl preg_compare_plugins(const void* left,const void* right) {
    return wcscmp(((const PREG_DISCOVERY*)left)->id,((const PREG_DISCOVERY*)right)->id);
}
BOOL preg_discover(void) {
    ULONGLONG stamp=preg_hash_folder();WCHAR pattern[MAX_PATH],directory[MAX_PATH];WIN32_FIND_DATAW item;HANDLE search;
    if(preg_scanned&&stamp==preg_fingerprint)return FALSE;
    preg_scanned=TRUE;preg_fingerprint=stamp;preg_inventory_count=0;
    if(!preg_resolve(L"plugins\\*.dll",pattern))return TRUE;
    wcscpy_s(directory,MAX_PATH,pattern);PathRemoveFileSpecW(directory);
    search=FindFirstFileW(pattern,&item);if(search==INVALID_HANDLE_VALUE)return TRUE;
    do { if(!(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        PREG_DISCOVERY found={0};int i,slot;
        if(wcslen(directory)+wcslen(item.cFileName)+2>MAX_PATH||!PathCombineW(found.path,directory,item.cFileName)||!preg_get_identity(found.path,found.id,32))continue;
        for(i=0;i<preg_inventory_count;++i)if(wcscmp(found.id,preg_inventory[i].id)==0)break;
        if(i<preg_inventory_count){preg_inventory[i].conflict=TRUE;continue;}
        if(!preg_get_resource(found.path,TC_PLUGIN_NAME_ID,TRUE,FALSE,found.name,128))wcscpy_s(found.name,128,item.cFileName);
        if(preg_inventory_count<PREG_MAX_CUSTOM)slot=preg_inventory_count++;
        else {slot=0;for(i=1;i<PREG_MAX_CUSTOM;++i)if(wcscmp(preg_inventory[i].id,preg_inventory[slot].id)>0)slot=i;if(wcscmp(found.id,preg_inventory[slot].id)>=0)continue;}
        preg_inventory[slot]=found;
    }}while(FindNextFileW(search,&item));
    FindClose(search);qsort(preg_inventory,preg_inventory_count,sizeof(preg_inventory[0]),preg_compare_plugins);return TRUE;
}
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
            for(slot=1;slot<=PREG_MAX_CUSTOM;++slot){for(k=PREG_KNOWN;k<count;++k)if(records[k].slot==slot)break;if(k==count)break;}
            if(slot>PREG_MAX_CUSTOM)continue;
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
BOOL preg_get_dll(UINT slot,WCHAR* id,WCHAR* path) {
    PLUGIN_RECORD records[PREG_MAX_RECORDS];int i,count=preg_load(records,PREG_MAX_RECORDS);
    for(i=PREG_KNOWN;i<count;++i)if(records[i].slot==(int)slot&&records[i].kind==PREG_KIND_DLL&&records[i].enabled&&!records[i].conflict&&(!records[i].automatic||records[i].discovered)&&preg_resolve(records[i].path,path)){wcscpy_s(id,64,records[i].id);return TRUE;}
    return FALSE;
}
static BOOL preg_append(char* batch,DWORD* used,const char* key,LPCWSTR text) {
    char encoded[8192]={0};int n;
    if(tc_utf16_to_utf8(text,encoded,sizeof(encoded))<=0 && text[0])return FALSE;
    n=sprintf_s(batch+*used,256*1024-*used,"%s=%s",key,encoded);
    if(n<0)return FALSE;*used+=(DWORD)n+1;return *used+1<256*1024;
}
BOOL preg_save(const PLUGIN_RECORD* records,int count) {
    char* batch;DWORD used=0;WCHAR active[MAX_PATH];int i,j;BOOL ok=FALSE;
    char key[80];
    if(tc_utf8_to_utf16(g_inifile,active,MAX_PATH)<=0)return FALSE;
    batch=(char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,256*1024);if(!batch)return FALSE;
    if(!preg_append(batch,&used,"RegistryVersion",L"1") || !preg_append(batch,&used,"Count",L"32"))goto cleanup;
    for(i=0;i<count;++i) {
        const PLUGIN_RECORD* r=&records[i];if(!r->visible)continue;
        if(r->known>=0) {
            sprintf_s(key,sizeof(key),"%s.Enable",preg_sections[r->known]);if(!preg_append(batch,&used,key,r->enabled?L"1":L"0"))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Path",preg_sections[r->known]);if(!preg_append(batch,&used,key,r->path))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Registered",preg_sections[r->known]);if(!preg_append(batch,&used,key,L"1"))goto cleanup;
            sprintf_s(key,sizeof(key),"%s.Ini",preg_sections[r->known]);if(!preg_append(batch,&used,key,r->ini))goto cleanup;
        } else {
            const char* fields[]={"Id","Name","Type","Enabled","Path","Args","SettingsArgs","Ini"};
            const WCHAR* values[]={r->id,r->name,r->kind==PREG_KIND_DLL?L"dll":L"exe",r->enabled?L"1":L"0",r->path,r->args,r->settings,r->ini};
            for(j=0;j<8;++j){sprintf_s(key,sizeof(key),"Item%d.%s",r->slot,fields[j]);if(!preg_append(batch,&used,key,values[j]))goto cleanup;}
        }
    }
    for(i=1;i<=PREG_MAX_CUSTOM;++i) {
        BOOL found=FALSE;
        for(j=0;j<count;++j)if(records[j].known<0 && records[j].visible && records[j].slot==i){found=TRUE;break;}
        if(!found){sprintf_s(key,sizeof(key),"Item%d.Id",i);if(!preg_append(batch,&used,key,L""))goto cleanup;}
    }
    batch[used]=0;if(!tc_write_batchW(active,"Plugins",batch,used+1))goto cleanup;
    ok=TRUE;
cleanup:
    HeapFree(GetProcessHeap(),0,batch);return ok;
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
/* Session identities are append-only; removing/reordering registrations never
   rebinds a selection already open in another property page. */
static char preg_actions[128][96];static int preg_actions_used;
static char preg_labels[128][512];
static BOOL preg_find_action(const char* key,PLUGIN_RECORD* found) {
    int slot;
    for(slot=1;slot<=PREG_MAX_CUSTOM;++slot){PLUGIN_RECORD record;char id[256],candidate[96];
        if(!preg_read_custom(slot,&record)||record.kind!=PREG_KIND_EXE||!record.enabled||!preg_is_present(&record))continue;
        if(tc_utf16_to_utf8(record.id,id,sizeof(id))<=0 || strlen(id)>70)continue;
        sprintf_s(candidate,sizeof(candidate),"exe:%s/open",id);
        if(strcmp(candidate,key)==0){*found=record;return TRUE;}
    }
    return FALSE;
}
int preg_count_actions(void) {
    int slot;
    if(GetMyRegLong("ETC","MinimalMode",0)||GetMyRegLong("Status_DoNotEdit","SafeMode",0))return 0;
    for(slot=1;slot<=PREG_MAX_CUSTOM;++slot){PLUGIN_RECORD record;char id[256],key[96];int i;
        if(!preg_read_custom(slot,&record)||record.kind!=PREG_KIND_EXE)continue;
        if(tc_utf16_to_utf8(record.id,id,sizeof(id))<=0||strlen(id)>70)continue;
        sprintf_s(key,sizeof(key),"exe:%s/open",id);
        for(i=0;i<preg_actions_used;++i)if(strcmp(key,preg_actions[i])==0)break;
        if(i==preg_actions_used && i<128)strcpy_s(preg_actions[preg_actions_used++],96,key);
    }
    return preg_actions_used;
}
const char* preg_get_action(int index){return index>=0&&index<preg_actions_used?preg_actions[index]:NULL;}
const char* preg_get_label(const char* key) {
    PLUGIN_RECORD record;int i;
    if(!key||strncmp(key,"exe:",4)!=0)return NULL;
    preg_count_actions();
    if(GetMyRegLong("ETC","MinimalMode",0)||GetMyRegLong("Status_DoNotEdit","SafeMode",0)||!preg_find_action(key,&record))return NULL;
    for(i=0;i<preg_actions_used;++i)if(strcmp(key,preg_actions[i])==0){if(tc_utf16_to_utf8(record.name,preg_labels[i],512)>0)return preg_labels[i];break;}
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
    resource=FindResourceExW(module,MAKEINTRESOURCEW(6),MAKEINTRESOURCEW((id>>4)+1),language);
    if(!resource)return FALSE;
    size=SizeofResource(module,resource);if(!size||size>65536||size%sizeof(WCHAR))return FALSE;
    loaded=LoadResource(module,resource);if(!loaded)return FALSE;
    text=(const WCHAR*)LockResource(loaded);if(!text)return FALSE;remaining=size/sizeof(WCHAR);
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
    WCHAR path[MAX_PATH];HMODULE module;PREG_LANGUAGES choices={0};BOOL ok=FALSE;int i,j;
    if(!out||capacity<=0)return FALSE;out[0]=0;
    if(capacity<2||!preg_resolve(file,path))return FALSE;
    module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);if(!module)return FALSE;
    if(!invariant) {
        choices.ids[0]=MAKELANGID(english?LANG_ENGLISH:LANG_JAPANESE,english?SUBLANG_ENGLISH_US:SUBLANG_DEFAULT);
        choices.ids[1]=MAKELANGID(english?LANG_ENGLISH:LANG_JAPANESE,SUBLANG_NEUTRAL);
        choices.ids[2]=MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US);
        choices.ids[3]=MAKELANGID(LANG_ENGLISH,SUBLANG_NEUTRAL);
    }
    choices.ids[4]=MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL);
    EnumResourceLanguagesW(module,MAKEINTRESOURCEW(6),MAKEINTRESOURCEW((id>>4)+1),preg_mark_language,(LONG_PTR)&choices);
    for(i=0;i<5;++i)if(choices.present[i]) {
        for(j=0;j<i;++j)if(choices.ids[j]==choices.ids[i])break;if(j<i)continue;
        if(preg_read_resource(module,id,choices.ids[i],out,capacity)){ok=TRUE;break;}
    }
    FreeLibrary(module);return ok;
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
    if(!id||capacity<=0)return FALSE;id[0]=0;
    if(!preg_get_resource(file,TC_PLUGIN_HEADER_ID,TRUE,TRUE,header,128)||wcsncmp(header,prefix,_countof(prefix)-1))return FALSE;
    ch=header+_countof(prefix)-1;length=wcslen(ch);if(!length||length>=32||length>=(size_t)capacity)return FALSE;
    for(i=0;i<length;++i)if(!((ch[i]>=L'a'&&ch[i]<=L'z')||(ch[i]>=L'0'&&ch[i]<=L'9')||ch[i]==L'.'||ch[i]==L'-'||ch[i]==L'_'))return FALSE;
    wcscpy_s(id,capacity,ch);return TRUE;
}
void preg_get_title(const PLUGIN_RECORD* record,BOOL english,WCHAR* title,int capacity) {
    if(!title||capacity<=0)return;title[0]=0;if(!record)return;
    if(!preg_get_resource(record->path,TC_PLUGIN_NAME_ID,english,FALSE,title,capacity))wcsncpy_s(title,capacity,record->name,_TRUNCATE);
}
