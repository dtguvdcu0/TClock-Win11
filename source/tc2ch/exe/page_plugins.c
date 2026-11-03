#include <wchar.h>
#include "tclock.h"
#include "plugin_registry.h"
#include "plugin_runtime.h"
#include "plugin_format_bridge.h"
#include "plugin_chime.h"
#include "plugin_render.h"
#include "../common/page_api.h"
#include "../common/plugin_resources.h"
#include <commctrl.h>
extern BOOL b_EnglishMenu;
#define PPG_POLL_TIMER 1

typedef struct PPG_STATE {
    PLUGIN_RECORD* rows;
    BOOL* changed;
    int count,selected;
    BOOL loading,dirty,settings,retry,refreshPending,pageChanged;
    /* Provider whose tclock.page/1 page replaces the Chime entry; empty = built-in. */
    WCHAR pageOwner[TC_PLUGIN_ID_CHARS];
} PPG_STATE;
static LPCWSTR ppg_text(LPCWSTR english,LPCWSTR japanese){return b_EnglishMenu?english:japanese;}
static PPG_STATE* ppg_state(HWND dialog){return (PPG_STATE*)GetWindowLongPtrW(dialog,GWLP_USERDATA);}
/* Descriptions of the known companion tools are owned by TClock, indexed like preg_files (TCard, TCycle, TCalendar, TCapture). NULL falls back to the plugin resource. */
static const WCHAR* const ppg_known_ja[]={L"TClock\u306e\u66f8\u5f0f\u304c\u4f7f\u3048\u308b\u4ed8\u7b8b\u7d19\u30c4\u30fc\u30eb\u3067\u3059\u3002\u5358\u4f53\u3067\u52d5\u4f5c\u3059\u308b\u307b\u304b\u3001LAN\u5185\u306e\u4ed6\u306ePC\u3068\u4ed8\u7b8b\u3092\u540c\u671f\u3059\u308b\u3053\u3068\u3082\u3067\u304d\u307e\u3059\u3002",L"Windows\u306e\u30bf\u30b9\u30af\u30b9\u30b1\u30b8\u30e5\u30fc\u30e9\u30fc\u306e\u3088\u3046\u306a\u30a2\u30d7\u30ea\u30b1\u30fc\u30b7\u30e7\u30f3\u30e9\u30f3\u30c1\u30e3\u30fc\u3067\u3059\u3002\u30a2\u30d7\u30ea\u306e\u8d77\u52d5\u76e3\u8996\uff08\u7d42\u4e86\u6642\u306e\u518d\u8d77\u52d5\uff09\u3001\u5b9a\u671f\u5b9f\u884c\u3001\u30ad\u30fc\u30dc\u30fc\u30c9\u30b7\u30e7\u30fc\u30c8\u30ab\u30c3\u30c8\u304b\u3089\u306e\u8d77\u52d5\u306a\u3069\u306b\u5bfe\u5fdc\u3057\u3066\u3044\u307e\u3059\u3002",L"\u6539\u4fee\u4e2d\u3067\u3059\u3002",L"\u30de\u30eb\u30c1\u30c7\u30a3\u30b9\u30d7\u30ec\u30a4\u5bfe\u5fdc\u306e\u30b9\u30af\u30ea\u30fc\u30f3\u30b7\u30e7\u30c3\u30c8\u30c4\u30fc\u30eb\u3067\u3059\u3002\u30db\u30c3\u30c8\u30ad\u30fc\u3067\u306e\u64ae\u5f71\u306e\u307b\u304b\u3001\u9023\u7d9a\u64ae\u5f71\u3084\u5b9a\u671f\u64ae\u5f71\u3082\u3067\u304d\u307e\u3059\u3002"};
static const WCHAR* const ppg_known_en[]={L"A sticky-note tool that supports TClock format strings. It works standalone and can also sync notes with other PCs on the LAN.",L"An application launcher in the style of Windows Task Scheduler. It supports launch monitoring (restarting apps when they exit), scheduled runs, keyboard shortcuts, and more.",L"Currently being reworked.",L"A multi-display screenshot tool. Besides capturing with a hotkey, it supports burst capture and scheduled capture."};
#define PPG_DOWNLOAD_URL L"https://github.com/dtguvdcu0/TClock-Plugin"
static void ppg_get_about(const PLUGIN_RECORD* r,WCHAR* description,int capacity){
    const WCHAR* text;
    if(r->known<0||r->known>=(int)_countof(ppg_known_ja)){preg_get_description(r,b_EnglishMenu,description,capacity);return;}
    text=b_EnglishMenu?ppg_known_en[r->known]:ppg_known_ja[r->known];
    if(text)wcscpy_s(description,capacity,text);else preg_get_description(r,b_EnglishMenu,description,capacity);
    if(description[0])wcscat_s(description,capacity,L"\r\n");
    wcscat_s(description,capacity,ppg_text(L"Download: ",L"\u914d\u5e03\u5143: "));
    wcscat_s(description,capacity,PPG_DOWNLOAD_URL);
}
static BOOL ppg_is_listed(const PLUGIN_RECORD* r){return r->visible&&(!r->automatic||r->discovered);}
static void ppg_mark(HWND dialog){PPG_STATE* s=ppg_state(dialog);if(s&&!s->loading){s->dirty=TRUE;SendMessageW(GetParent(dialog),PSM_CHANGED,(WPARAM)dialog,0);}}
static void ppg_capture(HWND dialog){
    PPG_STATE* s=ppg_state(dialog);PLUGIN_RECORD* r;
    if(!s||s->loading||s->selected<0||s->selected>=s->count)return;r=&s->rows[s->selected];
    r->enabled=IsDlgButtonChecked(dialog,IDC_PL_ENABLED)==BST_CHECKED;
    if(r->known>=0){const int controls[]={IDC_ETC_TCARD_INTEGRATION,IDC_ETC_TCYCLE_INTEGRATION,IDC_ETC_TCALENDAR_INTEGRATION,IDC_ETC_TCAPTURE_INTEGRATION};CheckDlgButton(dialog,controls[r->known],r->enabled?BST_CHECKED:BST_UNCHECKED);}
}
static void ppg_show(HWND dialog){
    PPG_STATE* s=ppg_state(dialog);PLUGIN_RECORD* r=s->selected>=0?&s->rows[s->selected]:NULL;WCHAR description[TC_PLUGIN_DESCRIPTION_CHARS]=L"";
    s->loading=TRUE;
    if(r){
        if(r->conflict)wcscpy_s(description,_countof(description),ppg_text(L"More than one registration or DLL uses this plugin ID. Keep one matching DLL in the plugins folder.",L"\u540c\u3058\u30d7\u30e9\u30b0\u30a4\u30f3ID\u306eDLL\u307e\u305f\u306f\u767b\u9332\u304c\u91cd\u8907\u3057\u3066\u3044\u307e\u3059\u3002plugins\u30d5\u30a9\u30eb\u30c0\u5185\u306e\u5bfe\u8c61DLL\u30921\u3064\u306b\u3057\u3066\u304f\u3060\u3055\u3044\u3002"));
        else ppg_get_about(r,description,_countof(description));
    }
    SetDlgItemTextW(dialog,IDC_PL_DESCRIPTION,description);
    EnableWindow(GetDlgItem(dialog,IDC_PL_ENABLED),r!=NULL);CheckDlgButton(dialog,IDC_PL_ENABLED,r&&r->enabled?BST_CHECKED:BST_UNCHECKED);
    EnableWindow(GetDlgItem(dialog,IDC_PL_SETTINGS),preg_can_configure(r));
    /* The current selection stays visible and removable while its plugin is unavailable. */
    {BOOL owns=r&&r->provider[0]&&!wcscmp(s->pageOwner,r->provider);
    EnableWindow(GetDlgItem(dialog,IDC_PL_PAGE),r&&r->provider[0]&&(owns||prt_find_substitute(r->provider,TC_PAGE_TARGET_CHIME)>=0));
    CheckDlgButton(dialog,IDC_PL_PAGE,owns?BST_CHECKED:BST_UNCHECKED);}
    s->loading=FALSE;
}
static void ppg_render(HWND dialog){
    PPG_STATE* s=ppg_state(dialog);HWND list=GetDlgItem(dialog,IDC_PL_LIST);int i,row=0,selected=-1;
    s->loading=TRUE;SendMessageW(list,LVM_DELETEALLITEMS,0,0);
    for(i=0;i<s->count;++i)if(ppg_is_listed(&s->rows[i])){
        PLUGIN_RECORD* r=&s->rows[i];LVITEMW item={0};LPCWSTR status;WCHAR name[128],version[64];
        preg_get_title(r,b_EnglishMenu,name,_countof(name));item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=row;item.pszText=name;item.lParam=i;SendMessageW(list,LVM_INSERTITEMW,0,(LPARAM)&item);
        item.mask=LVIF_TEXT;item.iSubItem=1;item.pszText=(LPWSTR)(r->kind==PREG_KIND_DLL?L"DLL":L"EXE");SendMessageW(list,LVM_SETITEMTEXTW,row,(LPARAM)&item);
        preg_get_version(r,version,_countof(version));item.iSubItem=2;item.pszText=version;SendMessageW(list,LVM_SETITEMTEXTW,row,(LPARAM)&item);
        status=r->conflict?ppg_text(L"ID conflict",L"ID\u91cd\u8907"):!preg_is_present(r)?ppg_text(L"Not found",L"\u672a\u691c\u51fa"):(r->enabled?ppg_text(L"Enabled",L"\u6709\u52b9"):ppg_text(L"Disabled",L"\u7121\u52b9"));
        item.iSubItem=3;item.pszText=(LPWSTR)status;SendMessageW(list,LVM_SETITEMTEXTW,row,(LPARAM)&item);
        if(i==s->selected)selected=row;++row;
    }
    if(selected<0&&row){LVITEMW item={0};item.mask=LVIF_PARAM;item.iItem=0;SendMessageW(list,LVM_GETITEMW,0,(LPARAM)&item);s->selected=(int)item.lParam;selected=0;}
    if(selected>=0){LVITEMW item={0};item.stateMask=LVIS_SELECTED|LVIS_FOCUSED;item.state=item.stateMask;SendMessageW(list,LVM_SETITEMSTATE,selected,(LPARAM)&item);}else s->selected=-1;
    s->loading=FALSE;ppg_show(dialog);
}
static BOOL ppg_sync(HWND dialog,BOOL force){
    PPG_STATE* s=ppg_state(dialog);PLUGIN_RECORD* fresh=NULL;BOOL* changed=NULL;WCHAR selected[64]=L"";int count,i,j;BOOL inventoryChanged;
    if(!s||s->settings)return FALSE;
    inventoryChanged=preg_discover();if(inventoryChanged)s->refreshPending=TRUE;if(!force&&!inventoryChanged&&!s->retry&&preg_discovery_ok())return TRUE;
    ppg_capture(dialog);if(s->selected>=0)wcscpy_s(selected,64,s->rows[s->selected].id);
    if(!preg_load_all(&fresh,&count))goto failed;
    changed=(BOOL*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)count*sizeof(*changed));if(!changed)goto failed;
    for(j=0;j<s->count;++j)if(s->changed[j]){
        int match=-1;for(i=0;i<count;++i)if(wcscmp(fresh[i].id,s->rows[j].id)==0){if(match>=0)goto failed;match=i;}
        if(match<0)goto failed;fresh[match].enabled=s->rows[j].enabled;changed[match]=TRUE;
    }
    if(s->rows)HeapFree(GetProcessHeap(),0,s->rows);if(s->changed)HeapFree(GetProcessHeap(),0,s->changed);
    s->rows=fresh;s->changed=changed;s->count=count;s->selected=-1;s->retry=FALSE;
    for(i=0;i<count;++i)if(wcscmp(s->rows[i].id,selected)==0&&ppg_is_listed(&s->rows[i])){s->selected=i;break;}
    {
        const int controls[]={IDC_ETC_TCARD_INTEGRATION,IDC_ETC_TCYCLE_INTEGRATION,IDC_ETC_TCALENDAR_INTEGRATION,IDC_ETC_TCAPTURE_INTEGRATION};
        for(i=0;i<PREG_KNOWN&&i<count;++i)CheckDlgButton(dialog,controls[i],s->rows[i].enabled?BST_CHECKED:BST_UNCHECKED);
    }
    ppg_render(dialog);if(s->refreshPending){s->refreshPending=FALSE;prt_refresh();}return TRUE;
failed:
    if(fresh)HeapFree(GetProcessHeap(),0,fresh);if(changed)HeapFree(GetProcessHeap(),0,changed);s->retry=TRUE;return FALSE;
}
static BOOL ppg_commit(HWND dialog){
    PPG_STATE* s=ppg_state(dialog);if(!s||!s->dirty)return TRUE;
    if(!ppg_sync(dialog,TRUE))return FALSE;
    if(!preg_save(s->rows,s->count))return FALSE;
    if(s->pageChanged){char owner[TC_PLUGIN_ID_CHARS*3];
        if(!WideCharToMultiByte(CP_UTF8,0,s->pageOwner,-1,owner,sizeof(owner),NULL,NULL)||!SetMyRegStr("Plugins","Page.chime",owner))return FALSE;
        s->pageChanged=FALSE;}
    if(s->changed)ZeroMemory(s->changed,(SIZE_T)s->count*sizeof(*s->changed));s->dirty=FALSE;prt_refresh();
    /* Hand the clock to a newly selected formatter, or back to the built-in one, right away. */
    pfb_refresh(g_hwndMain,TRUE);pch_refresh(g_hwndMain,TRUE);prn_refresh(g_hwndMain,TRUE);ppg_sync(dialog,TRUE);return TRUE;
}
INT_PTR CALLBACK PagePluginsProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam){
    PPG_STATE* s=ppg_state(dialog);
    switch(message){
    case WM_INITDIALOG:{
        const int controls[]={IDC_ETC_TCARD_INTEGRATION,IDC_ETC_TCYCLE_INTEGRATION,IDC_ETC_TCALENDAR_INTEGRATION,IDC_ETC_TCAPTURE_INTEGRATION};
        HWND list=GetDlgItem(dialog,IDC_PL_LIST);RECT rc;LVCOLUMNW column={0};int i;BOOL migrated;
        s=(PPG_STATE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*s));if(!s)return FALSE;SetWindowLongPtrW(dialog,GWLP_USERDATA,(LONG_PTR)s);s->selected=-1;s->loading=TRUE;
        migrated=preg_migrate();s->dirty=GetMyRegLong("Plugins","RegistryVersion",0)<1;
        {char owner[TC_PLUGIN_ID_CHARS*3];GetMyRegStr("Plugins","Page.chime",owner,sizeof(owner),"");
        if(!MultiByteToWideChar(CP_UTF8,0,owner,-1,s->pageOwner,_countof(s->pageOwner)))s->pageOwner[0]=0;}
        for(i=0;i<PREG_KNOWN;++i)CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|BS_AUTOCHECKBOX,0,0,1,1,dialog,(HMENU)(INT_PTR)controls[i],g_hInst,NULL);
        SendMessageW(list,LVM_SETEXTENDEDLISTVIEWSTYLE,0,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);GetClientRect(list,&rc);
        column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=(rc.right-GetSystemMetrics(SM_CXVSCROLL))*40/100;column.pszText=(LPWSTR)ppg_text(L"Plugin",L"\u30d7\u30e9\u30b0\u30a4\u30f3");SendMessageW(list,LVM_INSERTCOLUMNW,0,(LPARAM)&column);
        column.cx=(rc.right-GetSystemMetrics(SM_CXVSCROLL))*12/100;column.pszText=(LPWSTR)ppg_text(L"Type",L"\u5f62\u5f0f");SendMessageW(list,LVM_INSERTCOLUMNW,1,(LPARAM)&column);
        column.cx=(rc.right-GetSystemMetrics(SM_CXVSCROLL))*23/100;column.pszText=(LPWSTR)ppg_text(L"Version",L"\u30d0\u30fc\u30b8\u30e7\u30f3");SendMessageW(list,LVM_INSERTCOLUMNW,2,(LPARAM)&column);
        column.cx=(rc.right-GetSystemMetrics(SM_CXVSCROLL))*25/100;column.pszText=(LPWSTR)ppg_text(L"Status",L"\u72b6\u614b");SendMessageW(list,LVM_INSERTCOLUMNW,3,(LPARAM)&column);
        SetDlgItemTextW(dialog,IDC_PL_LABEL_DESCRIPTION,ppg_text(L"Description",L"\u8aac\u660e"));SetDlgItemTextW(dialog,IDC_PL_SETTINGS,ppg_text(L"Settings",L"\u8a2d\u5b9a"));SetDlgItemTextW(dialog,IDC_PL_ENABLED,ppg_text(L"Enable plugin",L"\u30d7\u30e9\u30b0\u30a4\u30f3\u3092\u6709\u52b9\u306b\u3059\u308b"));
        SetDlgItemTextW(dialog,IDC_PL_PAGE,ppg_text(L"Use as Chime settings page",L"\u6642\u5831\u306e\u8a2d\u5b9a\u30da\u30fc\u30b8\u3068\u3057\u3066\u4f7f\u3046"));
        s->loading=FALSE;ppg_sync(dialog,TRUE);
        if(!migrated)MessageBoxW(dialog,ppg_text(L"Old plugin settings could not be migrated. Original settings were retained.",L"\u65e7\u30d7\u30e9\u30b0\u30a4\u30f3\u8a2d\u5b9a\u3092\u79fb\u884c\u3067\u304d\u307e\u305b\u3093\u3067\u3057\u305f\u3002\u5143\u306e\u8a2d\u5b9a\u306f\u4fdd\u6301\u3057\u3066\u3044\u307e\u3059\u3002"),L"TClock",MB_OK|MB_ICONINFORMATION);
        SetTimer(dialog,PPG_POLL_TIMER,2000,NULL);return TRUE;
    }
    case WM_SHOWWINDOW:if(wParam&&s)ppg_sync(dialog,TRUE);return TRUE;
    case WM_TIMER:if(wParam==PPG_POLL_TIMER&&s&&!s->settings&&IsWindowVisible(dialog))ppg_sync(dialog,FALSE);return TRUE;
    case WM_NOTIFY:
        if(s&&((NMHDR*)lParam)->idFrom==IDC_PL_LIST&&((NMHDR*)lParam)->code==LVN_ITEMCHANGED&&!s->loading){NMLISTVIEW* change=(NMLISTVIEW*)lParam;if((change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED)){ppg_capture(dialog);s->selected=(int)change->lParam;ppg_show(dialog);}}return TRUE;
    case WM_COMMAND:
        if(!s)return FALSE;
        if(LOWORD(wParam)==IDC_PL_PAGE&&s->selected>=0&&!s->loading){PLUGIN_RECORD* r=&s->rows[s->selected];
            if(IsDlgButtonChecked(dialog,IDC_PL_PAGE)==BST_CHECKED)wcscpy_s(s->pageOwner,_countof(s->pageOwner),r->provider);
            else if(!wcscmp(s->pageOwner,r->provider))s->pageOwner[0]=0;
            s->pageChanged=TRUE;ppg_mark(dialog);return TRUE;}
        if(LOWORD(wParam)==IDC_PL_ENABLED&&s->selected>=0){ppg_capture(dialog);s->changed[s->selected]=TRUE;ppg_mark(dialog);ppg_render(dialog);return TRUE;}
        if(LOWORD(wParam)==IDC_PL_SETTINGS&&s->selected>=0&&preg_can_configure(&s->rows[s->selected])){
            BOOL ok;ppg_capture(dialog);s->settings=TRUE;ok=preg_open(&s->rows[s->selected],dialog,TRUE,b_EnglishMenu);s->settings=FALSE;
            if(!ok)MessageBoxW(dialog,ppg_text(L"Could not open plugin settings.",L"\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u8a2d\u5b9a\u3092\u958b\u3051\u307e\u305b\u3093\u3067\u3057\u305f\u3002"),L"TClock",MB_OK|MB_ICONERROR);
            ppg_sync(dialog,TRUE);return TRUE;
        }break;
    case PPG_COMMIT:
        if(ppg_commit(dialog)){SetWindowLongPtrW(dialog,DWLP_MSGRESULT,TRUE);return TRUE;}
        MessageBoxW(dialog,ppg_text(L"Could not save plugin enablement. Check INI write access.",L"\u30d7\u30e9\u30b0\u30a4\u30f3\u306e\u6709\u52b9\u72b6\u614b\u3092\u4fdd\u5b58\u3067\u304d\u307e\u305b\u3093\u3002INI\u3078\u306e\u66f8\u304d\u8fbc\u307f\u3092\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044\u3002"),L"TClock",MB_OK|MB_ICONERROR);SetWindowLongPtrW(dialog,DWLP_MSGRESULT,FALSE);return TRUE;
    case WM_DESTROY:KillTimer(dialog,PPG_POLL_TIMER);if(s){if(s->rows)HeapFree(GetProcessHeap(),0,s->rows);if(s->changed)HeapFree(GetProcessHeap(),0,s->changed);HeapFree(GetProcessHeap(),0,s);SetWindowLongPtrW(dialog,GWLP_USERDATA,0);}return TRUE;
    }
    return FALSE;
}
