/* Native hourly-chime editor. Pending values belong to this dialog. */
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include "tclock.h"
#include "../common/text_codec.h"
#include "../common/chime_schedule.h"
#include <commdlg.h>
#include <wchar.h>
#include <stdio.h>

enum {
    CHM_TAB=6100, CHM_REPEAT, CHM_DAY0, CHM_DAY1, CHM_DAY2, CHM_DAY3,
    CHM_DAY4, CHM_DAY5, CHM_DAY6, CHM_DIR, CHM_MIN, CHM_SEC,
    CHM_DIR2, CHM_MIN2, CHM_SEC2, CHM_ONCE, CHM_COUNT_HINT,
    CHM_SECOND_HINT, CHM_NEXT, CHM_SUMMARY, CHM_STATUS
};
typedef struct CHM_CONTROL { HWND hwnd; int group, x, y; } CHM_CONTROL;
typedef struct CHM_PAGE {
    BOOL loading, dirty;
    int days, tab, playing, count, repeatLayout;
    CHM_CONTROL controls[96];
} CHM_PAGE;
extern BOOL b_EnglishMenu;
#define CHM_TEXT(ja,en) (b_EnglishMenu ? L##en : L##ja)
static CHM_PAGE* chm_get(HWND dlg) { return (CHM_PAGE*)GetWindowLongPtrW(dlg,GWLP_USERDATA); }
static HWND chm_add(HWND dlg,int group,int id,LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int w,int h)
{
    RECT r={x,y,x+w,y+h}; HWND control; CHM_PAGE* p=chm_get(dlg);
    MapDialogRect(dlg,&r);
    control=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,
        WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,dlg,(HMENU)(INT_PTR)id,
        (HINSTANCE)GetWindowLongPtrW(dlg,GWLP_HINSTANCE),NULL);
    if(control) {
        SendMessageW(control,WM_SETFONT,SendMessageW(dlg,WM_GETFONT,0,0),FALSE);
        if(p->count<_countof(p->controls)){p->controls[p->count].hwnd=control;p->controls[p->count].group=group;p->controls[p->count].x=x;p->controls[p->count++].y=y;}
    }
    return control;
}
static void chm_label(HWND dlg,int group,LPCWSTR text,int x,int y,int w)
{ chm_add(dlg,group,-1,L"STATIC",text,SS_LEFT,x,y,w,10); }
static void chm_heading(HWND dlg,int group,LPCWSTR text,int y,int width)
{
    chm_label(dlg,group,text,8,y,width);
    chm_add(dlg,group,-1,L"STATIC",L"",SS_ETCHEDHORZ,12+width,y+5,192-width,1);
}
static void chm_text(HWND dlg,int id,LPCWSTR text)
{
    WCHAR old[512]; GetDlgItemTextW(dlg,id,old,_countof(old));
    if(wcscmp(old,text)) SetDlgItemTextW(dlg,id,text);
}
static void chm_item(HWND dlg,int id,LPCWSTR text)
{ SendDlgItemMessageW(dlg,id,CB_ADDSTRING,0,(LPARAM)text); }
static int chm_selection(HWND dlg,int id)
{ return (int)SendDlgItemMessageW(dlg,id,CB_GETCURSEL,0,0); }
static void chm_select(HWND dlg,int id,int value)
{ SendDlgItemMessageW(dlg,id,CB_SETCURSEL,value,0); }
static BOOL chm_number(HWND dlg,int id,int maxValue,int* value)
{
    WCHAR text[16]; int n=0,i;
    GetDlgItemTextW(dlg,id,text,_countof(text));
    if(!text[0]) return FALSE;
    for(i=0;text[i];++i){if(text[i]<L'0'||text[i]>L'9')return FALSE;n=n*10+text[i]-L'0';if(n>maxValue)return FALSE;}
    *value=n;return TRUE;
}
static BOOL chm_offset(HWND dlg,BOOL secondary,int* offset)
{
    int m,s,dir=chm_selection(dlg,secondary?CHM_DIR2:CHM_DIR);
    if(!chm_number(dlg,secondary?CHM_MIN2:CHM_MIN,30,&m)||
       !chm_number(dlg,secondary?CHM_SEC2:CHM_SEC,59,&s)||m*60+s>1800||dir<0)return FALSE;
    *offset=(m*60+s)*(dir==1?-1:1);return TRUE;
}
static void chm_set_offset(HWND dlg,BOOL secondary,int offset)
{
    offset=max(-1800,min(1800,offset));
    chm_select(dlg,secondary?CHM_DIR2:CHM_DIR,offset<0?1:0);
    SetDlgItemInt(dlg,secondary?CHM_MIN2:CHM_MIN,abs(offset)/60,FALSE);
    SetDlgItemInt(dlg,secondary?CHM_SEC2:CHM_SEC,abs(offset)%60,FALSE);
}
/* Existing UTF-8 INI serialization boundary; paths remain UTF-16 everywhere else. */
static void chm_read_audio(HWND dlg,int id,const char* key,LPCWSTR fallback)
{
    char encoded[MAX_PATH*4];WCHAR value[MAX_PATH];
    GetMyRegStr("Chime",key,encoded,sizeof(encoded),"");
    if(!encoded[0]||tc_utf8_to_utf16(encoded,value,_countof(value))<=0)wcscpy_s(value,_countof(value),fallback);
    SetDlgItemTextW(dlg,id,value);
}
static void chm_save_audio(HWND dlg,int id,const char* key)
{
    WCHAR value[MAX_PATH];char encoded[MAX_PATH*4];
    GetDlgItemTextW(dlg,id,value,_countof(value));
    if(value[0]&&tc_utf16_to_utf8(value,encoded,sizeof(encoded))>0)SetMyRegStr("Chime",key,encoded);
}
static LPCWSTR chm_error(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);int value;
    if(!IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME))return NULL;
    if(!p->days)return CHM_TEXT("\u66dc\u65e5\u30921\u65e5\u4ee5\u4e0a\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002","Select at least one day.");
    if(chm_selection(dlg,IDC_EDIT_CHIME_START)>=chm_selection(dlg,IDC_EDIT_CHIME_END)+1)
        return CHM_TEXT("\u7d42\u4e86\u306f\u958b\u59cb\u3088\u308a\u5f8c\u306e\u6642\u523b\u3092\u9078\u3093\u3067\u304f\u3060\u3055\u3044\u3002","End must be later than start.");
    if(!chm_offset(dlg,FALSE,&value)||(IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2)&&!chm_offset(dlg,TRUE,&value)))
        return CHM_TEXT("\u524d\u5f8c30\u5206\u4ee5\u5185\u3067\u6307\u5b9a\u3057\u3066\u304f\u3060\u3055\u3044\u3002","Use an offset within 30 minutes.");
    if(IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME_BLINK)&&!chm_number(dlg,IDC_EDIT_CHIME_BLINK,10,&value))
        return CHM_TEXT("\u70b9\u6ec5\u56de\u6570\u306f0\uff5e10\u56de\u3067\u3059\u3002","Blink count must be 0 to 10.");
    if(!GetWindowTextLengthW(GetDlgItem(dlg,IDC_CHIME_WAV))||
      (IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2)&&!GetWindowTextLengthW(GetDlgItem(dlg,IDC_CHIME_WAV2))))
        return CHM_TEXT("\u300c\u97f3\u30fb\u70b9\u6ec5\u300d\u3067\u97f3\u58f0\u30d5\u30a1\u30a4\u30eb\u3092\u6307\u5b9a\u3057\u3066\u304f\u3060\u3055\u3044\u3002","Choose a file on Sound / blink.");
    return NULL;
}
static void chm_time(WCHAR* text,int capacity,int seconds)
{
    seconds=(seconds%86400+86400)%86400;
    if(seconds%60)swprintf_s(text,capacity,L"%d:%02d:%02d",seconds/3600,seconds/60%60,seconds%60);
    else swprintf_s(text,capacity,L"%d:%02d",seconds/3600,seconds/60%60);
}
static void chm_preview(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);LPCWSTR error=chm_error(dlg);
    int a=0,b=0,start=chm_selection(dlg,IDC_EDIT_CHIME_START),end=chm_selection(dlg,IDC_EDIT_CHIME_END)+1;
    BOOL secondary=IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2)!=0;
    WCHAR next[256]=L"",summary[256]=L"",first[32],last[32];
    SYSTEMTIME now;int delta,hour,slot,found=0;
    if(!IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME)){
        chm_text(dlg,CHM_NEXT,CHM_TEXT("\u505c\u6b62\u4e2d","Paused"));
        chm_text(dlg,CHM_SUMMARY,L"");return;
    }
    if(error){chm_text(dlg,CHM_NEXT,CHM_TEXT("\u8a2d\u5b9a\u3092\u78ba\u8a8d\u3057\u3066\u304f\u3060\u3055\u3044","Check the settings"));chm_text(dlg,CHM_SUMMARY,error);return;}
    chm_offset(dlg,FALSE,&a);chm_offset(dlg,TRUE,&b);GetLocalTime(&now);
    for(delta=0;delta<8&&found<3;++delta){
        if(!chm_allows_day(p->days,(now.wDayOfWeek+delta)%7))continue;
        for(hour=0;hour<24&&found<3;++hour){
            int offsets[2]={a,b};
            /* Within each hour enumerate in actual playback order. */
            if((b+3600)%3600<(a+3600)%3600){offsets[0]=b;offsets[1]=a;}
            for(slot=0;slot<2&&found<3;++slot){
                int off=offsets[slot],sec=(3600+off)%3600,absolute=hour*3600+sec;
                BOOL second=(off==b&&a!=b);
                WCHAR item[64],clock[24];
                if(a==b&&slot==1)continue;
                if(second&&!secondary)continue;
                if(!delta&&absolute<=now.wHour*3600+now.wMinute*60+now.wSecond)continue;
                if(!chm_matches(hour,sec,off,start,end,second))continue;
                chm_time(clock,_countof(clock),absolute);
                if(delta)swprintf_s(item,_countof(item),CHM_TEXT("%d\u65e5\u5f8c %ls%ls","In %dd %ls%ls"),
                    delta,clock,second?CHM_TEXT("\uff08\u7b2c2\uff09"," (2)"):L"");
                else swprintf_s(item,_countof(item),L"%ls%ls",clock,second?CHM_TEXT("\uff08\u7b2c2\uff09"," (2)"):L"");
                if(found)wcscat_s(next,_countof(next),L"  \u2192  ");
                wcscat_s(next,_countof(next),item);++found;
            }
        }
    }
    chm_text(dlg,CHM_NEXT,next);
    if(secondary&&a==b)wcscpy_s(summary,_countof(summary),CHM_TEXT("\u57fa\u672c\u3068\u540c\u6642\u523b\u306e\u305f\u3081\u3001\u7b2c2\u6642\u5831\u306f\u9cf4\u308a\u307e\u305b\u3093\u3002","The second chime is skipped at the same time."));
    else if(end<24){
        chm_time(first,_countof(first),end*3600+a);chm_time(last,_countof(last),end*3600+b);
        if(secondary)swprintf_s(summary,_countof(summary),CHM_TEXT("\u6700\u5f8c\u306f %ls\u3001\u7b2c2\u306f %ls\u3002","Last: %ls; second: %ls."),first,last);
        else swprintf_s(summary,_countof(summary),CHM_TEXT("\u6700\u5f8c\u306f %ls\u3002","Last: %ls."),first);
    } else swprintf_s(summary,_countof(summary),CHM_TEXT("\u5bfe\u8c61 %d\uff5e24\u6642","Hours: %d to 24"),start);
    chm_text(dlg,CHM_SUMMARY,summary);
}
static void chm_arrange(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);
    int repeat=IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK)!=0,i;
    RECT firstGap={0,repeat?113:101,0,repeat?116:109};
    RECT lastGap={0,repeat?158:151,0,repeat?161:159};
    if(p->repeatLayout==repeat)return;
    MapDialogRect(dlg,&firstGap);MapDialogRect(dlg,&lastGap);
    for(i=0;i<p->count;++i){
        CHM_CONTROL* c=&p->controls[i];RECT r;
        if(c->group!=2||c->y<116||c->y>=196)continue;
        r.left=c->x;r.top=c->y+(c->y<158?(repeat?0:-7):(repeat?3:1));
        r.right=r.left;r.bottom=r.top;MapDialogRect(dlg,&r);
        if(c->y>=158)r.top+=(firstGap.bottom-firstGap.top)-(lastGap.bottom-lastGap.top);
        SetWindowPos(c->hwnd,NULL,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    p->repeatLayout=repeat;
}
static void chm_update(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);int i;
    BOOL enabled=IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME)!=0;
    BOOL secondary=enabled&&IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2);
    BOOL counted=IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK)!=0;
    HWND countHint=GetDlgItem(dlg,CHM_COUNT_HINT);
    int secondIds[]={CHM_DIR2,CHM_MIN2,CHM_SEC2,IDC_CHIME_WAV2,IDC_CHIME_WAV_BROWSE2,IDC_PLAY_WAV2};
    chm_arrange(dlg);
    for(i=0;i<p->count;++i){
        CHM_CONTROL* c=&p->controls[i];
        if(c->group){ShowWindow(c->hwnd,c->group==p->tab+1&&(c->hwnd!=countHint||counted)?SW_SHOW:SW_HIDE);EnableWindow(c->hwnd,enabled);}
    }
    for(i=0;i<_countof(secondIds);++i)EnableWindow(GetDlgItem(dlg,secondIds[i]),secondary);
    EnableWindow(GetDlgItem(dlg,IDC_EDIT_CHIME_BLINK),enabled&&IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME_BLINK));
    EnableWindow(GetDlgItem(dlg,IDC_PLAY_WAV_STOP),p->playing==1);
    EnableWindow(GetDlgItem(dlg,IDC_PLAY_WAV_STOP2),p->playing==2);
    chm_text(dlg,CHM_COUNT_HINT,IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK)?
        CHM_TEXT("15\u6642\u306f3\u56de\u30010\u6642\u30fb12\u6642\u306f12\u56de\u3002\u77ed\u3044\u97f3\u5411\u3051\u3002","15:00: 3 times; 0/12:00: 12. Use short sounds."):L"");
    chm_text(dlg,CHM_SECOND_HINT,IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2)?CHM_TEXT("1\u56de","Once"):CHM_TEXT("\u7b2c2\u6642\u5831\u306fOFF","Second chime is off"));
    chm_preview(dlg);
}
static void chm_stop(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);
    if(p->playing)PlaySoundW(NULL,NULL,0);
    p->playing=0;chm_text(dlg,CHM_STATUS,L"");
}
static void chm_changed(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);if(p->loading)return;
    chm_stop(dlg);p->dirty=TRUE;g_bApplyClock=TRUE;
    SendMessageW(GetParent(dlg),PSM_CHANGED,(WPARAM)dlg,0);chm_update(dlg);
}
static void chm_browse(HWND dlg,BOOL secondary)
{
    WCHAR path[MAX_PATH];OPENFILENAMEW ofn={0};int id=secondary?IDC_CHIME_WAV2:IDC_CHIME_WAV;
    GetDlgItemTextW(dlg,id,path,_countof(path));
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=dlg;
    ofn.lpstrFilter=L"WAV (*.wav)\0*.wav\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile=path;ofn.nMaxFile=_countof(path);ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn))SetDlgItemTextW(dlg,id,path);
}
static void chm_play(HWND dlg,BOOL secondary)
{
    WCHAR path[MAX_PATH];DWORD attr;
    chm_stop(dlg);GetDlgItemTextW(dlg,secondary?IDC_CHIME_WAV2:IDC_CHIME_WAV,path,_countof(path));
    attr=GetFileAttributesW(path);
    if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_DIRECTORY)||
       !PlaySoundW(path,NULL,SND_FILENAME|SND_ASYNC|SND_NODEFAULT))
        chm_text(dlg,CHM_STATUS,CHM_TEXT("\u97f3\u58f0\u30d5\u30a1\u30a4\u30eb\u3092\u518d\u751f\u3067\u304d\u307e\u305b\u3093\u3002","Unable to play this file."));
    else chm_get(dlg)->playing=secondary?2:1;
    chm_update(dlg);
}
static void chm_apply(HWND dlg)
{
    CHM_PAGE* p=chm_get(dlg);int value,start,end;
    if(!p->dirty||chm_error(dlg))return;
    SetMyRegLong("Chime","EnableChime",IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME)!=0);
    /* Missing pause key preserves legacy blink-only configurations on load.
       Explicitly switching this master off suspends audio and blink together. */
    SetMyRegLong("Chime","Paused",!IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME));
    SetMyRegLong("Chime","EnableSecondaryChime",IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME2)!=0);
    SetMyRegLong("Chime","EnableBlinkOnChime",IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CHIME_BLINK)!=0);
    SetMyRegLong("Chime","CuckooClock",IsDlgButtonChecked(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK)!=0);
    if(p->days)SetMyRegLong("Chime","Weekdays",p->days);
    if(chm_offset(dlg,FALSE,&value))SetMyRegLong("Chime","OffsetChimeSec",value);
    if(chm_offset(dlg,TRUE,&value))SetMyRegLong("Chime","OffsetSecondaryChimeSec",value);
    if(chm_number(dlg,IDC_EDIT_CHIME_BLINK,10,&value))SetMyRegLong("Chime","BlinksOnChime",value);
    start=chm_selection(dlg,IDC_EDIT_CHIME_START);end=chm_selection(dlg,IDC_EDIT_CHIME_END)+1;
    if(start>=0&&end>start){SetMyRegLong("Chime","ChimeHourStart",start);SetMyRegLong("Chime","ChimeHourEnd",end);}
    chm_save_audio(dlg,IDC_CHIME_WAV,"ChimeWav");chm_save_audio(dlg,IDC_CHIME_WAV2,"SecondaryChimeWav");
    p->dirty=FALSE;chm_stop(dlg);chm_update(dlg);
}
static void chm_init(HWND dlg)
{
    CHM_PAGE* p=(CHM_PAGE*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*p));TCITEMW tab={0};int i,value;
    WCHAR text[24];LPCWSTR days[]={L"\u65e5",L"\u6708",L"\u706b",L"\u6c34",L"\u6728",L"\u91d1",L"\u571f"};
    LPCWSTR englishDays[]={L"Su",L"Mo",L"Tu",L"We",L"Th",L"Fr",L"Sa"};
    if(!p)return;SetWindowLongPtrW(dlg,GWLP_USERDATA,(LONG_PTR)p);p->loading=TRUE;p->repeatLayout=-1;
    chm_add(dlg,0,IDC_CHECK_ENABLE_CHIME,L"BUTTON",CHM_TEXT("\u6642\u5831\u3092\u6709\u52b9\u306b\u3059\u308b","Enable chimes"),BS_AUTOCHECKBOX|WS_TABSTOP,8,8,160,11);
    chm_add(dlg,0,CHM_TAB,WC_TABCONTROLW,L"",WS_TABSTOP,8,24,198,17);
    tab.mask=TCIF_TEXT;tab.pszText=(LPWSTR)CHM_TEXT("\u66dc\u65e5\u30fb\u6642\u523b","Days / time");SendDlgItemMessageW(dlg,CHM_TAB,TCM_INSERTITEMW,0,(LPARAM)&tab);
    tab.pszText=(LPWSTR)CHM_TEXT("\u97f3\u30fb\u70b9\u6ec5","Sound / blink");SendDlgItemMessageW(dlg,CHM_TAB,TCM_INSERTITEMW,1,(LPARAM)&tab);
    chm_heading(dlg,1,CHM_TEXT("\u9cf4\u3089\u3059\u65e5","Days"),44,40);
    chm_label(dlg,1,CHM_TEXT("\u7e70\u308a\u8fd4\u3057","Repeat"),8,59,44);
    chm_add(dlg,1,CHM_REPEAT,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,52,56,100,90);
    chm_item(dlg,CHM_REPEAT,CHM_TEXT("\u6bce\u65e5","Every day"));chm_item(dlg,CHM_REPEAT,CHM_TEXT("\u5e73\u65e5\uff08\u6708\uff5e\u91d1\uff09","Weekdays (Mon-Fri)"));
    chm_item(dlg,CHM_REPEAT,CHM_TEXT("\u571f\u65e5","Weekends"));chm_item(dlg,CHM_REPEAT,CHM_TEXT("\u66dc\u65e5\u3092\u6307\u5b9a","Custom"));
    for(i=0;i<7;++i){int day=(i+1)%7;chm_add(dlg,1,CHM_DAY0+day,L"BUTTON",b_EnglishMenu?englishDays[day]:days[day],BS_AUTOCHECKBOX|BS_PUSHLIKE|WS_TABSTOP,52+i*19,72,17,14);}
    chm_heading(dlg,1,CHM_TEXT("\u9cf4\u3089\u3059\u6642\u9593\u5e2f","Hours"),92,62);
    chm_label(dlg,1,CHM_TEXT("\u5bfe\u8c61\u306e\u6642\u523b","From / to"),8,107,44);
    chm_add(dlg,1,IDC_EDIT_CHIME_START,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,52,104,34,150);
    chm_label(dlg,1,CHM_TEXT("\u6642 \uff5e","to"),89,107,22);
    chm_add(dlg,1,IDC_EDIT_CHIME_END,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,112,104,34,150);
    chm_label(dlg,1,CHM_TEXT("\u6642","h"),149,107,12);
    chm_label(dlg,1,CHM_TEXT("0\uff5e24\u6642\u3067\u7d42\u65e5\u3002","0 to 24: all day."),52,119,145);
    chm_heading(dlg,1,CHM_TEXT("\u6bce\u6642\u306e\u30bf\u30a4\u30df\u30f3\u30b0","Hourly timing"),136,70);
    for(i=0;i<2;++i){
        int y=148+i*16;
        if(i)chm_add(dlg,1,IDC_CHECK_ENABLE_CHIME2,L"BUTTON",CHM_TEXT("\u7b2c2\u6642\u5831","Second"),BS_AUTOCHECKBOX|WS_TABSTOP,8,y+2,44,11);
        else chm_label(dlg,1,CHM_TEXT("\u57fa\u672c\u306e\u6642\u5831","Main"),8,y+3,44);
        chm_label(dlg,1,CHM_TEXT("\u6b63\u6642\u306e","Hour"),52,y+3,23);
        chm_add(dlg,1,i?CHM_DIR2:CHM_DIR,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,75,y,32,50);
        chm_item(dlg,i?CHM_DIR2:CHM_DIR,CHM_TEXT("\u5f8c","After"));
        chm_item(dlg,i?CHM_DIR2:CHM_DIR,CHM_TEXT("\u524d","Before"));
        chm_add(dlg,1,i?CHM_MIN2:CHM_MIN,L"EDIT",L"0",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,110,y,22,13);
        chm_label(dlg,1,CHM_TEXT("\u5206","m"),134,y+3,10);
        chm_add(dlg,1,i?CHM_SEC2:CHM_SEC,L"EDIT",L"0",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,146,y,22,13);
        chm_label(dlg,1,CHM_TEXT("\u79d2","s"),170,y+3,12);
    }
    chm_heading(dlg,2,CHM_TEXT("\u57fa\u672c\u306e\u6642\u5831","Main chime"),44,55);
    chm_add(dlg,2,IDC_CHIME_WAV,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,8,56,163,13);
    chm_add(dlg,2,IDC_CHIME_WAV_BROWSE,L"BUTTON",CHM_TEXT("\u53c2\u7167\u2026","Browse..."),WS_TABSTOP,174,56,32,14);
    chm_label(dlg,2,CHM_TEXT("WAV\u30d5\u30a1\u30a4\u30eb","WAV file"),8,75,70);
    chm_add(dlg,2,IDC_PLAY_WAV,L"BUTTON",CHM_TEXT("\u8a66\u8074","Play"),WS_TABSTOP,137,72,32,14);
    chm_add(dlg,2,IDC_PLAY_WAV_STOP,L"BUTTON",CHM_TEXT("\u505c\u6b62","Stop"),WS_TABSTOP,174,72,32,14);
    chm_label(dlg,2,CHM_TEXT("\u9cf4\u3089\u3057\u65b9","Repeat"),8,91,44);
    chm_add(dlg,2,CHM_ONCE,L"BUTTON",CHM_TEXT("1\u56de","Once"),BS_AUTORADIOBUTTON|WS_GROUP|WS_TABSTOP,58,88,34,12);
    chm_add(dlg,2,IDC_CHECK_ENABLE_CUCKOOCLOCK,L"BUTTON",CHM_TEXT("\u6642\u523b\u306e\u6570\u3060\u3051","Hour count"),BS_AUTORADIOBUTTON|WS_TABSTOP,94,88,100,12);
    chm_add(dlg,2,CHM_COUNT_HINT,L"STATIC",L"",SS_LEFT,8,103,198,10);
    chm_heading(dlg,2,CHM_TEXT("\u7b2c2\u6642\u5831","Second chime"),116,55);
    chm_add(dlg,2,IDC_CHIME_WAV2,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,8,128,163,13);
    chm_add(dlg,2,IDC_CHIME_WAV_BROWSE2,L"BUTTON",CHM_TEXT("\u53c2\u7167\u2026","Browse..."),WS_TABSTOP,174,128,32,14);
    chm_add(dlg,2,CHM_SECOND_HINT,L"STATIC",L"",SS_LEFT,8,147,120,10);
    chm_add(dlg,2,IDC_PLAY_WAV2,L"BUTTON",CHM_TEXT("\u8a66\u8074","Play"),WS_TABSTOP,137,144,32,14);
    chm_add(dlg,2,IDC_PLAY_WAV_STOP2,L"BUTTON",CHM_TEXT("\u505c\u6b62","Stop"),WS_TABSTOP,174,144,32,14);
    chm_heading(dlg,2,CHM_TEXT("\u6642\u8a08\u306e\u70b9\u6ec5","Clock blink"),158,55);
    chm_add(dlg,2,IDC_CHECK_ENABLE_CHIME_BLINK,L"BUTTON",CHM_TEXT("\u57fa\u672c\u6642\u5831\u306e\u30bf\u30a4\u30df\u30f3\u30b0\u3067\u70b9\u6ec5","Blink with the main chime"),BS_AUTOCHECKBOX|WS_TABSTOP,8,170,155,11);
    chm_add(dlg,2,IDC_EDIT_CHIME_BLINK,L"EDIT",L"3",ES_NUMBER|ES_AUTOHSCROLL|WS_TABSTOP,169,168,23,13);
    chm_label(dlg,2,CHM_TEXT("\u56de","x"),196,171,10);
    chm_add(dlg,2,CHM_STATUS,L"STATIC",L"",SS_LEFT,8,183,198,9);
    chm_add(dlg,0,-1,L"STATIC",L"",SS_ETCHEDHORZ,8,196,198,1);
    chm_label(dlg,0,CHM_TEXT("\u3053\u306e\u8a2d\u5b9a\u3067\u9cf4\u308b\u6642\u523b","Upcoming chimes"),8,199,198);
    chm_add(dlg,0,CHM_NEXT,L"STATIC",L"",SS_LEFT,8,210,198,10);
    chm_add(dlg,0,CHM_SUMMARY,L"STATIC",L"",SS_LEFT,8,221,198,9);
    for(i=0;i<24;++i){swprintf_s(text,_countof(text),L"%d",i);chm_item(dlg,IDC_EDIT_CHIME_START,text);swprintf_s(text,_countof(text),L"%d",i+1);chm_item(dlg,IDC_EDIT_CHIME_END,text);}
    chm_select(dlg,IDC_EDIT_CHIME_START,max(0,min(23,GetMyRegLong("Chime","ChimeHourStart",0))));
    value=GetMyRegLong("Chime","ChimeHourEnd",24);chm_select(dlg,IDC_EDIT_CHIME_END,max(1,min(24,value))-1);
    p->days=GetMyRegLong("Chime","Weekdays",127)&127;
    chm_select(dlg,CHM_REPEAT,p->days==127?0:p->days==62?1:p->days==65?2:3);
    for(i=0;i<7;++i)CheckDlgButton(dlg,CHM_DAY0+i,(p->days>>i)&1);
    CheckDlgButton(dlg,IDC_CHECK_ENABLE_CHIME,GetMyRegLong("Chime","EnableChime",0)!=0);
    CheckDlgButton(dlg,IDC_CHECK_ENABLE_CHIME2,GetMyRegLong("Chime","EnableSecondaryChime",0)!=0);
    CheckDlgButton(dlg,IDC_CHECK_ENABLE_CHIME_BLINK,GetMyRegLong("Chime","EnableBlinkOnChime",0)!=0);
    value=GetMyRegLong("Chime","CuckooClock",0);
    CheckRadioButton(dlg,CHM_ONCE,CHM_ONCE,value?IDC_CHECK_ENABLE_CUCKOOCLOCK:CHM_ONCE);
    CheckDlgButton(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK,value!=0);
    chm_set_offset(dlg,FALSE,(short)GetMyRegLong("Chime","OffsetChimeSec",0));
    chm_set_offset(dlg,TRUE,(short)GetMyRegLong("Chime","OffsetSecondaryChimeSec",1800));
    SetDlgItemInt(dlg,IDC_EDIT_CHIME_BLINK,max(0,min(10,GetMyRegLong("Chime","BlinksOnChime",3))),FALSE);
    SendDlgItemMessageW(dlg,IDC_CHIME_WAV,EM_SETLIMITTEXT,MAX_PATH-1,0);
    SendDlgItemMessageW(dlg,IDC_CHIME_WAV2,EM_SETLIMITTEXT,MAX_PATH-1,0);
    chm_read_audio(dlg,IDC_CHIME_WAV,"ChimeWav",L"C:\\Windows\\Media\\notify.wav");
    chm_read_audio(dlg,IDC_CHIME_WAV2,"SecondaryChimeWav",L"C:\\Windows\\Media\\chimes.wav");
    p->loading=FALSE;chm_update(dlg);SetTimer(dlg,1,1000,NULL);
}
INT_PTR CALLBACK PageChimeProc(HWND dlg,UINT message,WPARAM wp,LPARAM lp)
{
    CHM_PAGE* p=chm_get(dlg);
    if(message==WM_INITDIALOG){chm_init(dlg);return TRUE;}
    if(!p)return FALSE;
    switch(message){
    case CHM_VALIDATE:
        if(chm_error(dlg)){chm_preview(dlg);MessageBeep(MB_ICONWARNING);SetWindowLongPtrW(dlg,DWLP_MSGRESULT,FALSE);return TRUE;}
        SetWindowLongPtrW(dlg,DWLP_MSGRESULT,TRUE);return TRUE;
    case WM_COMMAND: {
        int id=LOWORD(wp),code=HIWORD(wp),i;
        if(p->loading)return TRUE;
        if(id==IDC_CHIME_WAV_BROWSE||id==IDC_CHIME_WAV_BROWSE2){chm_browse(dlg,id==IDC_CHIME_WAV_BROWSE2);return TRUE;}
        if(id==IDC_PLAY_WAV||id==IDC_PLAY_WAV2){chm_play(dlg,id==IDC_PLAY_WAV2);return TRUE;}
        if(id==IDC_PLAY_WAV_STOP||id==IDC_PLAY_WAV_STOP2){chm_stop(dlg);chm_update(dlg);return TRUE;}
        if(id==CHM_REPEAT&&code==CBN_SELCHANGE){
            int selected=chm_selection(dlg,id);
            if(selected<3){p->days=selected==0?127:selected==1?62:65;for(i=0;i<7;++i)CheckDlgButton(dlg,CHM_DAY0+i,(p->days>>i)&1);}
        }
        if(id>=CHM_DAY0&&id<=CHM_DAY6&&code==BN_CLICKED){
            p->days=0;for(i=0;i<7;++i)if(IsDlgButtonChecked(dlg,CHM_DAY0+i))p->days|=1<<i;
            chm_select(dlg,CHM_REPEAT,3);
        }
        if((id==CHM_ONCE||id==IDC_CHECK_ENABLE_CUCKOOCLOCK)&&code==BN_CLICKED){
            CheckDlgButton(dlg,CHM_ONCE,id==CHM_ONCE);
            CheckDlgButton(dlg,IDC_CHECK_ENABLE_CUCKOOCLOCK,id==IDC_CHECK_ENABLE_CUCKOOCLOCK);
        }
        if(code==BN_CLICKED||code==EN_CHANGE||code==CBN_SELCHANGE)chm_changed(dlg);
        return TRUE;
    }
    case WM_NOTIFY:
        if(((NMHDR*)lp)->idFrom==CHM_TAB&&((NMHDR*)lp)->code==TCN_SELCHANGE){
            p->tab=(int)SendDlgItemMessageW(dlg,CHM_TAB,TCM_GETCURSEL,0,0);chm_update(dlg);return TRUE;
        }
        if(((NMHDR*)lp)->code==PSN_APPLY){chm_apply(dlg);return TRUE;}break;
    case WM_TIMER: if(wp==1&&IsWindowVisible(dlg))chm_preview(dlg);return TRUE;
    case WM_DESTROY:
        KillTimer(dlg,1);chm_stop(dlg);SetWindowLongPtrW(dlg,GWLP_USERDATA,0);HeapFree(GetProcessHeap(),0,p);return TRUE;
    }
    return FALSE;
}
