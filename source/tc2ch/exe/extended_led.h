// Native LED sections share a fixed preview and retain all message fields.
static HWND ext_get_control(HWND dialog, int id)
{
    HWND control=GetDlgItem(dialog,id);
    if(!control){HWND panel=GetDlgItem(dialog,IDC_EXT_LED_PANEL);if(panel)control=GetDlgItem(panel,id);}
    return control;
}
static LRESULT ext_send_control(HWND d,int id,UINT m,WPARAM w,LPARAM l){return SendMessageW(ext_get_control(d,id),m,w,l);}
static int ext_get_text(HWND d,int id,WCHAR* text,int count){return GetWindowTextW(ext_get_control(d,id),text,count);}
static BOOL ext_set_text(HWND d,int id,const WCHAR* text){return SetWindowTextW(ext_get_control(d,id),text);}
static void ext_check_control(HWND d,int id,UINT checked){SendMessageW(ext_get_control(d,id),BM_SETCHECK,checked,0);}
static UINT ext_get_checked(HWND d,int id){return (UINT)SendMessageW(ext_get_control(d,id),BM_GETCHECK,0,0);}
static BOOL ext_set_number(HWND d,int id,UINT value,BOOL sign){WCHAR text[32];if(sign)swprintf_s(text,_countof(text),L"%d",(int)value);else swprintf_s(text,_countof(text),L"%u",value);return ext_set_text(d,id,text);}
static void ext_layout_led(HWND dialog,EXT_PAGE* page,BOOL show);
static void ext_scroll_led(HWND dialog,EXT_PAGE* page,int target);
static void ext_reveal_led(HWND dialog,EXT_PAGE* page,HWND control)
{
    RECT rect,client;if(!page||!control||GetParent(control)!=page->ledPanel)return;
    GetWindowRect(control,&rect);MapWindowPoints(NULL,page->ledPanel,(POINT*)&rect,2);GetClientRect(page->ledPanel,&client);
    if(rect.top<0)ext_scroll_led(dialog,page,page->ledScroll+rect.top);
    else if(rect.bottom>client.bottom)ext_scroll_led(dialog,page,page->ledScroll+rect.bottom-client.bottom);
}
static LRESULT CALLBACK ext_panel_proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR ref)
{
    HWND dialog=(HWND)ref;EXT_PAGE* page=(EXT_PAGE*)GetWindowLongPtrW(dialog,DWLP_USER);
    if(message==WM_COMMAND){
        if(HIWORD(wParam)==EN_SETFOCUS||HIWORD(wParam)==CBN_SETFOCUS||HIWORD(wParam)==BN_SETFOCUS)ext_reveal_led(dialog,page,(HWND)lParam);
        return SendMessageW(dialog,message,wParam,lParam);
    }
    if(message==WM_VSCROLL&&page){
        SCROLLINFO si={sizeof(si),SIF_ALL};GetScrollInfo(dialog,SB_VERT,&si);int target=si.nPos;
        RECT unit={0,0,0,16};MapDialogRect(dialog,&unit);
        switch(LOWORD(wParam)){case SB_LINEUP:target-=unit.bottom;break;case SB_LINEDOWN:target+=unit.bottom;break;
        case SB_PAGEUP:target-=(int)si.nPage;break;case SB_PAGEDOWN:target+=(int)si.nPage;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:target=si.nTrackPos;break;case SB_TOP:target=0;break;case SB_BOTTOM:target=si.nMax;break;}
        ext_scroll_led(dialog,page,target);return 0;
    }
    if(message==WM_MOUSEWHEEL&&page){
        UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);RECT unit={0,0,0,16};MapDialogRect(dialog,&unit);
        int step=lines==WHEEL_PAGESCROLL?page->ledView:unit.bottom*(int)lines;
        page->ledWheel+=GET_WHEEL_DELTA_WPARAM(wParam);int ticks=page->ledWheel/WHEEL_DELTA;page->ledWheel%=WHEEL_DELTA;
        ext_scroll_led(dialog,page,page->ledScroll-ticks*step);return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ext_panel_proc,id);
    return DefSubclassProc(window,message,wParam,lParam);
}
static LRESULT CALLBACK ext_wheel_proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR ref)
{
    if(message==WM_MOUSEWHEEL && GetParent(window)==(HWND)ref){WCHAR name[24];GetClassNameW(window,name,_countof(name));
        if(lstrcmpiW(name,L"ComboBox")||!SendMessageW(window,CB_GETDROPPEDSTATE,0,0))return SendMessageW((HWND)ref,message,wParam,lParam);
    }
    if(message==WM_SETFOCUS){HWND panel=(HWND)ref,dialog=GetParent(panel);EXT_PAGE* page=(EXT_PAGE*)GetWindowLongPtrW(dialog,DWLP_USER);ext_reveal_led(dialog,page,window);}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ext_wheel_proc,id);
    return DefSubclassProc(window,message,wParam,lParam);
}
// Keep native tab behavior while separating adjacent headers by one physical pixel.
static LRESULT CALLBACK ext_handle_tabs(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR ref)
{
    (void)ref;
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ext_handle_tabs,id);
    LRESULT result=DefSubclassProc(window,message,wParam,lParam);
    if(message==WM_PAINT||message==WM_PRINTCLIENT||message==WM_PRINT){
        BOOL painting=message==WM_PAINT;HDC dc=painting?GetDC(window):(HDC)wParam;
        if(dc){RECT client;GetClientRect(window,&client);
            for(int i=1;i<TabCtrl_GetItemCount(window);++i){RECT item;
                if(TabCtrl_GetItemRect(window,i,&item)){RECT gap={item.left-1,1,item.left,client.bottom-2};FillRect(dc,&gap,GetSysColorBrush(COLOR_BTNFACE));}
            }
            if(painting)ReleaseDC(window,dc);
        }
    }
    return result;
}
static void ext_add_led(EXT_PAGE* page,HWND control,int row,int x,int width,int minimum,BOOL details)
{
    EXT_LED_CONTROL* item=&page->ledControls[page->ledControlCount++];
    item->window=control;item->row=row;item->x=x;item->width=width;item->minimum=minimum;item->details=details;
    SendMessageW(control,WM_SETFONT,SendMessageW(GetParent(page->ledPanel),WM_GETFONT,0,0),TRUE);
    SetWindowSubclass(control,ext_wheel_proc,1,(DWORD_PTR)page->ledPanel);
}
static HWND ext_create_led(EXT_PAGE* page,const WCHAR* cls,const WCHAR* text,int id,DWORD style,int row,int x,int width,int minimum,BOOL details)
{
    HWND control=CreateWindowExW(!lstrcmpW(cls,L"Edit")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|style,0,0,1,1,page->ledPanel,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    if(control)ext_add_led(page,control,row,x,width,minimum,details);
    return control;
}
static void ext_label_led(EXT_PAGE* p,const WCHAR* en,const WCHAR* ja,int row,int x,int width,int minimum,BOOL details)
{ext_create_led(p,L"Static",b_EnglishMenu?en:ja,0,SS_LEFT,row,x,width,minimum,details);}
static void ext_number_led(EXT_PAGE* p,int id,int row,const WCHAR* en,const WCHAR* ja,int minimum,BOOL details)
{
    ext_label_led(p,en,ja,row,4,43,minimum,details);
    HWND edit=ext_create_led(p,L"Edit",L"",id,WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,row,49,28,minimum,details);
    SendMessageW(edit,EM_SETLIMITTEXT,3,0);
}
static void ext_color_led(EXT_PAGE* p,int id,int row,int minimum)
{
    ext_label_led(p,L"Color",L"\u767a\u5149\u8272",row,4,43,minimum,FALSE);
    HWND combo=ext_create_led(p,L"ComboBox",L"",id,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,row,49,64,minimum,FALSE);
    const WCHAR* en[]={L"Red",L"Amber",L"Green",L"White",L"Blue",L"Ice blue",L"Pink"};
    const WCHAR* ja[]={L"\u8d64",L"\u9ec4",L"\u7dd1",L"\u767d",L"\u9752",L"\u6c34\u8272",L"\u30d4\u30f3\u30af"};
    for(int i=0;i<7;++i)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)(b_EnglishMenu?en[i]:ja[i]));
}
static void ext_move_led(HWND dialog,HWND control,int x,int y,int width,int height)
{
    RECT rect={x,y,x+width,y+height};MapDialogRect(dialog,&rect);
    SetWindowPos(control,NULL,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
}
static void ext_enable_led(HWND dialog,EXT_PAGE* page,BOOL enabled)
{
    BOOL clock=enabled&&ext_get_checked(dialog,IDC_LED_CLOCK)==BST_CHECKED;
    BOOL offsets=enabled&&ext_get_checked(dialog,IDC_LED_DETAILS)==BST_CHECKED;
    const int ids[]={IDC_LED_DATE,IDC_LED_SECONDS,IDC_LED_COLON,IDC_LED_CLOCK_TIME,IDC_LED_CLOCK_COLOR};
    for(int i=0;i<(int)_countof(ids);++i)EnableWindow(ext_get_control(dialog,ids[i]),clock);
    EnableWindow(ext_get_control(dialog,IDC_LED_DATE_FORMAT),clock&&ext_get_checked(dialog,IDC_LED_DATE)==BST_CHECKED);
    EnableWindow(ext_get_control(dialog,IDC_EXT_X),offsets);EnableWindow(ext_get_control(dialog,IDC_EXT_Y),offsets);
    int count=(int)ext_send_control(dialog,IDC_LED_COUNT,CB_GETCURSEL,0,0);
    for(int i=0;i<3;++i)EnableWindow(ext_get_control(dialog,IDC_LED_MESSAGE_FIRST+i),enabled&&i<count);
    int effect=(int)ext_send_control(dialog,IDC_LED_TEXT_FIRST+page->ledMessage*10+2,CB_GETCURSEL,0,0);
    EnableWindow(ext_get_control(dialog,IDC_LED_SPEED),enabled&&count>0&&effect!=0);
    EnableWindow(page->ledTabs,enabled);EnableWindow(page->ledPlay,enabled&&(page->ledSection!=2?clock:count>0));
}
static void ext_place_led(EXT_PAGE* page,HWND window,int x,int width)
{
    for(int i=0;i<page->ledControlCount;++i)if(page->ledControls[i].window==window){page->ledControls[i].x=x;page->ledControls[i].width=width;return;}
}
static void ext_init_led(HWND dialog,EXT_PAGE* page)
{
    HWND child;int i;
    for(child=GetWindow(dialog,GW_CHILD);child&&page->originalCount<32;child=GetWindow(child,GW_HWNDNEXT)){
        EXT_ORIGINAL* original=&page->originals[page->originalCount++];original->window=child;
        GetWindowRect(child,&original->rect);MapWindowPoints(NULL,dialog,(POINT*)&original->rect,2);
    }
    page->ledPanel=CreateWindowExW(WS_EX_CONTROLPARENT,L"Static",L"",WS_CHILD|WS_CLIPCHILDREN,
        0,0,1,1,dialog,(HMENU)IDC_EXT_LED_PANEL,GetModuleHandleW(NULL),NULL);
    ext_move_led(dialog,page->ledPanel,7,76,222,102);
    SetWindowSubclass(page->ledPanel,ext_panel_proc,1,(DWORD_PTR)dialog);
    page->ledTabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_TABSTOP|TCS_FIXEDWIDTH,
        0,0,1,1,dialog,(HMENU)IDC_LED_TABS,GetModuleHandleW(NULL),NULL);
    SendMessageW(page->ledTabs,WM_SETFONT,SendMessageW(dialog,WM_GETFONT,0,0),TRUE);
    SetWindowSubclass(page->ledTabs,ext_handle_tabs,1,0);
    const WCHAR* tabsEn[]={L"Frame",L"Time",L"Messages"};
    const WCHAR* tabsJa[]={L"\u8868\u793a\u67a0",L"\u6642\u523b",L"\u8ffd\u52a0\u6587\u5b57\u5217"};
    for(i=0;i<3;++i){TCITEMW item={};item.mask=TCIF_TEXT;item.pszText=(WCHAR*)(b_EnglishMenu?tabsEn[i]:tabsJa[i]);SendMessageW(page->ledTabs,TCM_INSERTITEMW,i,(LPARAM)&item);}
    ext_move_led(dialog,page->ledTabs,7,57,174,17);
    RECT tabSize={0,0,54,16};MapDialogRect(dialog,&tabSize);SendMessageW(page->ledTabs,TCM_SETITEMSIZE,0,MAKELPARAM(tabSize.right,tabSize.bottom));
    page->ledPlay=CreateWindowExW(0,L"Button",b_EnglishMenu?L"Play":L"\u518d\u751f",WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
        0,0,1,1,dialog,(HMENU)IDC_LED_PLAY,GetModuleHandleW(NULL),NULL);
    SendMessageW(page->ledPlay,WM_SETFONT,SendMessageW(dialog,WM_GETFONT,0,0),TRUE);
    ext_move_led(dialog,page->ledPlay,201,181,24,12);
    // Row / 10 selects the section; message minimum selects the retained editor.
    ext_label_led(page,L"Height (DIP)",L"\u9ad8\u3055 (DIP)",0,4,43,0,FALSE);
    ext_add_led(page,GetDlgItem(dialog,IDC_EXT_SIZE),0,49,28,0,FALSE);
    ext_number_led(page,IDC_LED_COLUMNS,0,L"Max characters",L"\u6700\u5927\u8868\u793a\u6587\u5b57\u6570",0,FALSE);
    page->ledControls[page->ledControlCount-2].x=114;page->ledControls[page->ledControlCount-2].width=64;
    ext_place_led(page,ext_get_control(dialog,IDC_LED_COLUMNS),179,28);
    ext_label_led(page,L"Height 0 = taskbar / characters 0 = longest item",L"\u9ad8\u3055 0 = \u30bf\u30b9\u30af\u30d0\u30fc / \u6587\u5b57\u6570 0 = \u6700\u9577\u306e\u5185\u5bb9",1,4,214,0,FALSE);
    ext_label_led(page,L"Placement",L"\u914d\u7f6e",2,4,43,0,FALSE);
    ext_add_led(page,GetDlgItem(dialog,IDC_EXT_PLACE),2,49,64,0,FALSE);
    ext_number_led(page,IDC_LED_BRIGHTNESS,2,L"Light (%)",L"\u660e\u308b\u3055 (%)",0,FALSE);
    page->ledControls[page->ledControlCount-2].x=121;
    ext_place_led(page,ext_get_control(dialog,IDC_LED_BRIGHTNESS),179,28);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Offsets":L"\u4f4d\u7f6e\u88dc\u6b63",IDC_LED_DETAILS,WS_TABSTOP|BS_AUTOCHECKBOX,3,4,50,0,FALSE);
    ext_label_led(page,L"X (DIP)",L"\u6a2a (DIP)",3,59,28,0,FALSE);ext_add_led(page,GetDlgItem(dialog,IDC_EXT_X),3,89,28,0,FALSE);
    ext_label_led(page,L"Y (DIP)",L"\u7e26 (DIP)",3,141,28,0,FALSE);ext_add_led(page,GetDlgItem(dialog,IDC_EXT_Y),3,171,28,0,FALSE);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Frame":L"\u30d5\u30ec\u30fc\u30e0",IDC_LED_FRAME,WS_TABSTOP|BS_AUTOCHECKBOX,4,4,52,0,FALSE);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Show time":L"\u6642\u523b\u3092\u8868\u793a",IDC_LED_CLOCK,WS_TABSTOP|BS_AUTOCHECKBOX,10,4,104,0,FALSE);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Date":L"\u65e5\u4ed8\u3092\u8868\u793a",IDC_LED_DATE,WS_TABSTOP|BS_AUTOCHECKBOX,11,11,64,0,FALSE);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Seconds":L"\u79d2\u3092\u8868\u793a",IDC_LED_SECONDS,WS_TABSTOP|BS_AUTOCHECKBOX,11,80,64,0,FALSE);
    ext_create_led(page,L"Button",b_EnglishMenu?L"Colon":L"\uff1a\u3092\u8868\u793a",IDC_LED_COLON,WS_TABSTOP|BS_AUTOCHECKBOX,11,149,64,0,FALSE);
    ext_label_led(page,L"Date format",L"\u65e5\u4ed8\u66f8\u5f0f",12,4,43,0,FALSE);
    HWND date=ext_create_led(page,L"Edit",L"",IDC_LED_DATE_FORMAT,WS_TABSTOP|ES_AUTOHSCROLL,12,49,169,0,FALSE);SendMessageW(date,EM_SETLIMITTEXT,LED_FORMAT_MAX,0);
    ext_label_led(page,L"<%yyyy%>/<%mm%>/<%dd%> -> 2026/10/04",L"<%yyyy%>/<%mm%>/<%dd%> \u2192 2026/10/04",13,49,169,0,FALSE);
    ext_number_led(page,IDC_LED_CLOCK_TIME,14,L"Duration (s)",L"\u8868\u793a\u6642\u9593 (\u79d2)",0,FALSE);
    ext_color_led(page,IDC_LED_CLOCK_COLOR,14,0);page->ledControls[page->ledControlCount-2].x=121;
    ext_place_led(page,ext_get_control(dialog,IDC_LED_CLOCK_COLOR),179,41);
    ext_label_led(page,L"Messages",L"\u6587\u5b57\u5217\u306e\u4ef6\u6570",20,4,43,0,FALSE);
    HWND count=ext_create_led(page,L"ComboBox",L"",IDC_LED_COUNT,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,20,49,62,0,FALSE);
    for(i=0;i<4;++i){WCHAR label[32];if(!i)wcscpy_s(label,_countof(label),b_EnglishMenu?L"Time only":L"\u6642\u523b\u306e\u307f");else swprintf_s(label,_countof(label),b_EnglishMenu?L"%d messages":L"%d \u4ef6",i);SendMessageW(count,CB_ADDSTRING,0,(LPARAM)label);}
    for(i=0;i<3;++i){WCHAR label[32];swprintf_s(label,_countof(label),b_EnglishMenu?L"Message %d":L"\u6587\u5b57\u5217 %d",i+1);
        ext_create_led(page,L"Button",label,IDC_LED_MESSAGE_FIRST+i,WS_TABSTOP|BS_AUTORADIOBUTTON|BS_PUSHLIKE,21,4+47*i,45,0,FALSE);
        int base=IDC_LED_TEXT_FIRST+i*10;
        ext_create_led(page,L"Static",b_EnglishMenu?L"Format":L"\u66f8\u5f0f",base+4,SS_LEFT,22,4,43,i+1,FALSE);
        HWND text=ext_create_led(page,L"Edit",L"",base,WS_TABSTOP|ES_AUTOHSCROLL,22,49,169,i+1,FALSE);SendMessageW(text,EM_SETLIMITTEXT,LED_FORMAT_MAX,0);
        ext_label_led(page,L"Example: <%yyyy%> -> 2026 / \"TCLOCK\" -> TCLOCK",L"\u4f8b: <%yyyy%> \u2192 2026 / \"TCLOCK\" \u2192 TCLOCK",23,49,169,i+1,FALSE);
        ext_create_led(page,L"Static",b_EnglishMenu?L"Method":L"\u8868\u793a\u65b9\u6cd5",base+6,SS_LEFT,24,4,43,i+1,FALSE);
        HWND effect=ext_create_led(page,L"ComboBox",L"",base+2,WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,24,49,64,i+1,FALSE);
        const WCHAR* en[]={L"Instant",L"Slide",L"Scroll"};const WCHAR* ja[]={L"\u5373\u6642\u5207\u66ff",L"\u6a2a\u30b9\u30e9\u30a4\u30c9",L"\u6a2a\u30b9\u30af\u30ed\u30fc\u30eb"};
        for(int e=0;e<3;++e)SendMessageW(effect,CB_ADDSTRING,0,(LPARAM)(b_EnglishMenu?en[e]:ja[e]));
        ext_create_led(page,L"Static",L"",base+5,SS_LEFT,24,121,55,i+1,FALSE);
        HWND hold=ext_create_led(page,L"Edit",L"",base+1,WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,24,179,28,i+1,FALSE);SendMessageW(hold,EM_SETLIMITTEXT,2,0);
        ext_label_led(page,L"s",L"\u79d2",24,211,8,i+1,FALSE);
        ext_color_led(page,base+3,25,i+1);
    }
    ext_number_led(page,IDC_LED_SPEED,25,L"Speed (dots/s)",L"\u901f\u3055 (dots/s)",0,FALSE);
    page->ledControls[page->ledControlCount-2].x=121;page->ledControls[page->ledControlCount-2].width=55;
    ext_place_led(page,ext_get_control(dialog,IDC_LED_SPEED),179,28);
    led_load(&page->ledOptions,GetMyRegLong,GetMyRegStr);LED_OPTIONS* o=&page->ledOptions;
    const int checks[]={IDC_LED_CLOCK,IDC_LED_SECONDS,IDC_LED_COLON,IDC_LED_FRAME,IDC_LED_DATE};
    const BOOL states[]={o->clock,o->seconds,o->colon,o->frame,o->date};
    for(i=0;i<(int)_countof(checks);++i)ext_check_control(dialog,checks[i],states[i]?BST_CHECKED:BST_UNCHECKED);
    ext_set_text(dialog,IDC_LED_DATE_FORMAT,o->dateFormat);
    ext_check_control(dialog,IDC_LED_DETAILS,(GetMyRegLong("ExtendedDisplay","OffsetXDip",0)||GetMyRegLong("ExtendedDisplay","OffsetYDip",0))?BST_CHECKED:BST_UNCHECKED);
    ext_set_number(dialog,IDC_LED_COLUMNS,o->columns,FALSE);ext_set_number(dialog,IDC_LED_SPEED,o->speed,FALSE);
    ext_set_number(dialog,IDC_LED_BRIGHTNESS,o->brightness,FALSE);ext_set_number(dialog,IDC_LED_CLOCK_TIME,o->clockSeconds,FALSE);
    ext_send_control(dialog,IDC_LED_COUNT,CB_SETCURSEL,o->count,0);ext_send_control(dialog,IDC_LED_CLOCK_COLOR,CB_SETCURSEL,o->clockColor-1,0);
    for(i=0;i<3;++i){int base=IDC_LED_TEXT_FIRST+i*10;ext_set_text(dialog,base,o->messages[i].text);ext_set_number(dialog,base+1,o->messages[i].seconds,FALSE);ext_send_control(dialog,base+2,CB_SETCURSEL,o->messages[i].effect,0);ext_send_control(dialog,base+3,CB_SETCURSEL,o->messages[i].color-1,0);}
    page->ledPreview=led_create();
}
static void ext_layout_led(HWND dialog,EXT_PAGE* page,BOOL show)
{
    if(!page||!page->ledPanel)return;
    ShowScrollBar(dialog,SB_VERT,FALSE);
    int count=(int)ext_send_control(dialog,IDC_LED_COUNT,CB_GETCURSEL,0,0);
    page->ledMessage=max(0,min(page->ledMessage,max(0,count-1)));
    for(int i=0;i<page->originalCount;++i){EXT_ORIGINAL* original=&page->originals[i];int id=GetDlgCtrlID(original->window);
        BOOL common=id==IDC_EXT_SIZE||id==IDC_EXT_PLACE||id==IDC_EXT_X||id==IDC_EXT_Y;
        if(common)SetParent(original->window,show?page->ledPanel:dialog);
        if(!show)SetWindowPos(original->window,NULL,original->rect.left,original->rect.top,original->rect.right-original->rect.left,original->rect.bottom-original->rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
        RECT limit={0,0,0,166};MapDialogRect(dialog,&limit);
        if(id!=IDC_EXT_ENABLE&&id!=IDC_EXT_DETACH&&id!=IDC_EXT_MODE&&id!=IDC_EXT_MODE_LABEL&&original->rect.top<limit.bottom)
            ShowWindow(original->window,!show?SW_SHOW:SW_HIDE);
        WCHAR cls[24];GetClassNameW(original->window,cls,_countof(cls));
        if(!lstrcmpW(cls,L"Button")&&(GetWindowLongPtrW(original->window,GWL_STYLE)&BS_TYPEMASK)==BS_GROUPBOX){
            WCHAR caption[64];
            if(show&&page->ledSection==2)swprintf_s(caption,_countof(caption),b_EnglishMenu?L"Preview: message %d":L"\u30d7\u30ec\u30d3\u30e5\u30fc: \u6587\u5b57\u5217 %d",page->ledMessage+1);
            else if(show&&page->ledSection==0)wcscpy_s(caption,_countof(caption),b_EnglishMenu?L"Preview: full display":L"\u30d7\u30ec\u30d3\u30e5\u30fc: \u5168\u4f53");
            else wcscpy_s(caption,_countof(caption),show?(b_EnglishMenu?L"Preview: time":L"\u30d7\u30ec\u30d3\u30e5\u30fc: \u6642\u523b"):(b_EnglishMenu?L"Preview":L"\u30d7\u30ec\u30d3\u30e5\u30fc"));
            SetWindowTextW(original->window,caption);if(show)ext_move_led(dialog,original->window,7,183,222,43);
        }
    }
    ShowWindow(page->ledPanel,show?SW_SHOW:SW_HIDE);ShowWindow(page->ledTabs,show?SW_SHOW:SW_HIDE);ShowWindow(page->ledPlay,show&&page->ledSection==2?SW_SHOW:SW_HIDE);
    if(!show)return;
    ext_move_led(dialog,ext_get_control(dialog,IDC_EXT_PREVIEW),13,192,209,21);
    ext_move_led(dialog,ext_get_control(dialog,IDC_EXT_EFFECTIVE),13,216,209,8);
    SetWindowTextW(page->ledPlay,page->ledPlaying?(b_EnglishMenu?L"Static":L"\u9759\u6b62"):(b_EnglishMenu?L"Play":L"\u518d\u751f"));

    for(int i=0;i<3;++i){int base=IDC_LED_TEXT_FIRST+i*10;
        int effect=(int)ext_send_control(dialog,base+2,CB_GETCURSEL,0,0);
        const WCHAR* en[]={L"Display time",L"Hold time",L"Minimum time"};
        const WCHAR* ja[]={L"\u8868\u793a\u6642\u9593",L"\u9759\u6b62\u6642\u9593",L"\u6700\u4f4e\u8868\u793a\u6642\u9593"};
        ext_set_text(dialog,base+5,b_EnglishMenu?en[max(0,min(2,effect))]:ja[max(0,min(2,effect))]);
        ext_check_control(dialog,IDC_LED_MESSAGE_FIRST+i,i==page->ledMessage?BST_CHECKED:BST_UNCHECKED);
    }
    for(int i=0;i<page->ledControlCount;++i){EXT_LED_CONTROL* item=&page->ledControls[i];
        BOOL visible=item->row/10==page->ledSection&&(!item->minimum||(item->minimum==page->ledMessage+1&&item->minimum<=count));
        if(GetDlgCtrlID(item->window)==IDC_LED_SPEED&&count==0)visible=FALSE;
        ShowWindow(item->window,visible?SW_SHOW:SW_HIDE);item->y=4+(item->row%10)*16;
    }
    RECT client;GetClientRect(page->ledPanel,&client);page->ledHeight=page->ledView=client.bottom;
    ext_scroll_led(dialog,page,0);
}
static void ext_scroll_led(HWND dialog,EXT_PAGE* page,int target)
{
    target=max(0,min(target,max(0,page->ledHeight-page->ledView)));page->ledScroll=target;
    SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS,0,page->ledHeight-1,(UINT)page->ledView,target,0};SetScrollInfo(dialog,SB_VERT,&si,TRUE);
    for(int i=0;i<page->ledControlCount;++i){EXT_LED_CONTROL* item=&page->ledControls[i];if(!(GetWindowLongPtrW(item->window,GWL_STYLE)&WS_VISIBLE))continue;
        WCHAR cls[24];GetClassNameW(item->window,cls,_countof(cls));BOOL label=!lstrcmpW(cls,L"Static"),combo=!lstrcmpW(cls,L"ComboBox");
        RECT rect={item->x,item->y+(label?1:0),item->x+item->width,item->y+(combo?90:label?11:12)};MapDialogRect(dialog,&rect);
        SetWindowPos(item->window,NULL,rect.left,rect.top-target,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    InvalidateRect(page->ledPanel,NULL,TRUE);
}
static BOOL ext_read_led(HWND dialog,EXT_PAGE* page,LED_OPTIONS* o,BOOL report)
{
    *o=page->ledOptions;
    o->date=ext_get_checked(dialog,IDC_LED_DATE)==BST_CHECKED;
    ext_get_text(dialog,IDC_LED_DATE_FORMAT,o->dateFormat,_countof(o->dateFormat));
    o->clock=ext_get_checked(dialog,IDC_LED_CLOCK)==BST_CHECKED;o->seconds=ext_get_checked(dialog,IDC_LED_SECONDS)==BST_CHECKED;
    o->colon=ext_get_checked(dialog,IDC_LED_COLON)==BST_CHECKED;
    o->frame=ext_get_checked(dialog,IDC_LED_FRAME)==BST_CHECKED;
    o->count=(int)ext_send_control(dialog,IDC_LED_COUNT,CB_GETCURSEL,0,0);o->clockColor=1+(int)ext_send_control(dialog,IDC_LED_CLOCK_COLOR,CB_GETCURSEL,0,0);
    const int ids[]={IDC_LED_COLUMNS,IDC_LED_SPEED,IDC_LED_BRIGHTNESS,IDC_LED_CLOCK_TIME};
    int* values[]={&o->columns,&o->speed,&o->brightness,&o->clockSeconds};const int lows[]={0,5,20,1},highs[]={LED_TEXT_MAX,50,100,60};
    int invalid=0;WCHAR value[32],*end;SYSTEMTIME time;GetLocalTime(&time);BOOL hasText=FALSE;
    WCHAR clockText[LED_TEXT_MAX+1];
    if(o->clock&&o->date&&!led_expand_clock(o,&time,clockText))invalid=IDC_LED_DATE_FORMAT;
    for(int i=0;i<4&&!invalid;++i){ext_get_text(dialog,ids[i],value,_countof(value));long n=wcstol(value,&end,10);if(!value[0]||*end||n<lows[i]||n>highs[i]){invalid=ids[i];break;}*values[i]=(int)n;}
    for(int i=0;i<3&&!invalid;++i){int base=IDC_LED_TEXT_FIRST+i*10;ext_get_text(dialog,base,o->messages[i].text,_countof(o->messages[i].text));
        ext_get_text(dialog,base+1,value,_countof(value));long n=wcstol(value,&end,10);
        if(!value[0]||*end||n<1||n>60){if(i<o->count)invalid=base+1;}else o->messages[i].seconds=(int)n;
        o->messages[i].effect=(int)ext_send_control(dialog,base+2,CB_GETCURSEL,0,0);o->messages[i].color=1+(int)ext_send_control(dialog,base+3,CB_GETCURSEL,0,0);
        if(i<o->count){WCHAR expanded[LED_TEXT_MAX+1];
            if(!led_expand_text(o->messages[i].text,&time,expanded))invalid=base;
            else {const WCHAR* text=expanded;while(*text==L' ')++text;if(*text)hasText=TRUE;}
        }
    }
    if(!o->clock&&!hasText)invalid=IDC_LED_COUNT;
    if(invalid&&report){
        if(invalid==IDC_LED_DATE_FORMAT||invalid==IDC_LED_CLOCK_TIME||invalid==IDC_LED_CLOCK_COLOR)page->ledSection=1;
        else if(invalid==IDC_LED_COUNT||invalid==IDC_LED_SPEED||invalid>=IDC_LED_TEXT_FIRST){page->ledSection=2;if(invalid>=IDC_LED_TEXT_FIRST)page->ledMessage=(invalid-IDC_LED_TEXT_FIRST)/10;}
        else page->ledSection=0;
        TabCtrl_SetCurSel(page->ledTabs,page->ledSection);ext_layout_led(dialog,page,TRUE);ext_reveal_led(dialog,page,ext_get_control(dialog,invalid));SetFocus(ext_get_control(dialog,invalid));
        MessageBoxW(dialog,b_EnglishMenu?L"Use clock format syntax, e.g. <%yyyy%> or \"TCLOCK\". Expanded output must use ASCII and contain at most 160 characters. Duration: 1-60 s; maximum characters: 0 (auto) or 1-160; speed: 5-50; brightness: 20-100. Enable time or a message.":L"\u66f8\u5f0f\u306f <%yyyy%> \u3084 \"TCLOCK\" \u3068\u540c\u3058\u66f8\u304d\u65b9\u3067\u3059\u3002\u5c55\u958b\u5f8c\u306f\u82f1\u6570\u8a18\u53f7\u3067160\u6587\u5b57\u307e\u3067\u3002\u6642\u9593:1-60\u79d2\u3001\u6700\u5927\u6587\u5b57\u6570:0(\u81ea\u52d5)\u307e\u305f\u306f1-160\u3001\u901f\u3055:5-50\u3001\u660e\u308b\u3055:20-100\u3002\u6642\u523b\u307e\u305f\u306f\u6587\u5b57\u5217\u3092\u6709\u52b9\u306b\u3057\u3066\u304f\u3060\u3055\u3044\u3002",b_EnglishMenu?L"LED Display":L"\u96fb\u5149\u63b2\u793a\u677f",MB_OK|MB_ICONWARNING);
    }
    return !invalid&&o->count>=0&&o->count<=3&&o->clockColor>=1&&o->clockColor<=7;
}
