#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "../common/ext_renderer.h"
#include "../common/taskbar_surface.h"

extern "C" BOOL b_EnglishMenu;

namespace {
struct DpiScope {
    typedef HANDLE (WINAPI *SetContext)(HANDLE);
    SetContext setter;
    HANDLE previous;
    DpiScope() : setter(reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetThreadDpiAwarenessContext"))),previous(nullptr) {
        if(setter){previous=setter(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));if(!previous)previous=setter(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3)));}
    }
    ~DpiScope(){if(setter && previous)setter(previous);}
};
constexpr UINT_PTR ext_timer = 0x544344;
constexpr size_t ext_limit = 32;
constexpr DWORD ext_magic = 0x31444354;
struct Pose {
    WCHAR home[128];
    WCHAR display[128];
    LONG floating;
    LONG x, y, size;
};
struct Storage {
    DWORD magic, version, bytes, count;
    ULONGLONG checksum;
    Pose poses[ext_limit];
};
struct Display {
    HMONITOR monitor;
    RECT work;
    std::wstring identity;
    UINT dpi;
};
struct Clock {
    std::wstring home;
    HWND window = nullptr, cue = nullptr;
    EXT_DETACH_HOME binding = {};
    EXT_RENDERER renderer;
    SIZE aspect = {1,1};
    int diameter = 0;
    bool floating = false, drag = false, moved = false, beforeFloating = false;
    bool resizeMode = false, resizing = false, topmost = false;
    int resizeCorner = HTNOWHERE;
    RECT frame = {}, lastFrame = {}, beforeFrame = {}, homeSlot = {};
    POINT origin = {}, grip = {};
    Pose preferred = {};
    WORD second = 65535;
    bool dirty = true;
};
// TClock has a custom CRT entry point. Construct and destroy STL state explicitly.
struct HostState {
    HWND owner = nullptr, mainTarget = nullptr;
    HINSTANCE module = nullptr;
    ACS_OPTIONS options = {};
    EXT_DETACH_PACKET packet = {};
    int offset = 0;
    bool ready = false, loaded = false, armed = false;
    HHOOK keyboard = nullptr;
    std::vector<std::unique_ptr<Clock>> clocks;
    std::vector<Pose> saved;
    std::vector<Display> displays;
    std::wstring path;
};
HostState* ext_state;
#define ext_owner ext_state->owner
#define ext_module ext_state->module
#define ext_options ext_state->options
#define ext_offset ext_state->offset
#define ext_ready ext_state->ready
#define ext_armed ext_state->armed
#define ext_keyboard ext_state->keyboard
#define ext_loaded ext_state->loaded
#define ext_clocks ext_state->clocks
#define ext_saved ext_state->saved
#define ext_displays ext_state->displays
#define ext_path ext_state->path
LRESULT CALLBACK ext_process_window(HWND, UINT, WPARAM, LPARAM);
void ext_set_floating(Clock& clock,bool floating);
LRESULT CALLBACK ext_observe_keys(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION && ext_state && ext_ready){
        const KBDLLHOOKSTRUCT* key=reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if(key->vkCode==VK_MENU || key->vkCode==VK_LMENU || key->vkCode==VK_RMENU){
            bool down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
            PostMessageW(ext_owner,EXT_DETACH_ARM,down?1:0,0);
        }
    }
    // Observe only the modifier; never suppress keyboard input.
    return CallNextHookEx(nullptr,code,message,data);
}

HWND ext_get_window(uint64_t value) { return reinterpret_cast<HWND>(static_cast<UINT_PTR>(value)); }
int ext_get_width(const RECT& r) { return r.right-r.left; }
int ext_get_height(const RECT& r) { return r.bottom-r.top; }
bool ext_equal_rect(const RECT& a,const RECT& b) { return EqualRect(&a,&b)!=FALSE; }
UINT ext_get_dpi(HMONITOR monitor) {
    // Optional per-monitor API keeps the host usable on older supported Windows.
    HMODULE library=LoadLibraryW(L"shcore.dll");
    UINT x=96,y=96;
    if(library){
        typedef HRESULT (WINAPI *GetMonitorScale)(HMONITOR,int*);
        auto query=reinterpret_cast<GetMonitorScale>(GetProcAddress(library,"GetScaleFactorForMonitor"));
        int scale=100;
        if(query && SUCCEEDED(query(monitor,&scale)))x=MulDiv(96,scale,100);
        (void)y;
        FreeLibrary(library);
    }
    return x>=48 && x<=768?x:96;
}
BOOL CALLBACK ext_collect_display(HMONITOR monitor,HDC,LPRECT,LPARAM) {
    MONITORINFOEXW info={};info.cbSize=sizeof(info);
    if(!GetMonitorInfoW(monitor,&info))return TRUE;
    Display display={monitor,info.rcWork,info.szDevice,ext_get_dpi(monitor)};
    UINT32 paths=0,modes=0;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&paths,&modes)==ERROR_SUCCESS && paths<=128 && modes<=256){
        std::vector<DISPLAYCONFIG_PATH_INFO> path(paths);
        std::vector<DISPLAYCONFIG_MODE_INFO> mode(modes);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&paths,path.data(),&modes,mode.data(),nullptr)==ERROR_SUCCESS){
            for(UINT32 i=0;i<paths;++i){
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source={};
                source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path[i].sourceInfo.adapterId,path[i].sourceInfo.id};
                if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || lstrcmpiW(source.viewGdiDeviceName,info.szDevice))continue;
                DISPLAYCONFIG_TARGET_DEVICE_NAME target={};
                target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path[i].targetInfo.adapterId,path[i].targetInfo.id};
                if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS && target.monitorDevicePath[0])display.identity=target.monitorDevicePath;
                break;
            }
        }
    }
    ext_displays.push_back(display);return TRUE;
}
void ext_refresh_displays() { ext_displays.clear();EnumDisplayMonitors(nullptr,nullptr,ext_collect_display,0); }
const Display* ext_find_display(HMONITOR monitor) {
    for(const auto& d:ext_displays)if(d.monitor==monitor)return &d;
    return nullptr;
}
const Display* ext_find_identity(const WCHAR* identity) {
    for(const auto& d:ext_displays)if(!lstrcmpiW(d.identity.c_str(),identity))return &d;
    return nullptr;
}
ULONGLONG ext_hash_storage(const Storage& store) {
    ULONGLONG hash=14695981039346656037ull;
    const BYTE* bytes=reinterpret_cast<const BYTE*>(store.poses);
    for(size_t i=0;i<sizeof(store.poses);++i){hash^=bytes[i];hash*=1099511628211ull;}
    return hash;
}
void ext_load_storage() {
    if(ext_loaded)return;
    ext_loaded=true;
    WCHAR module[32768];DWORD length=GetModuleFileNameW(nullptr,module,_countof(module));
    if(!length || length>=_countof(module))return;
    ext_path.assign(module,length);
    size_t slash=ext_path.find_last_of(L"\\/");if(slash==std::wstring::npos){ext_path.clear();return;}
    ext_path.resize(slash+1);ext_path+=L"TClock-detached.dat";
    HANDLE file=CreateFileW(ext_path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    Storage store={};DWORD read=0;LARGE_INTEGER size={};
    bool valid=GetFileSizeEx(file,&size) && size.QuadPart==sizeof(store) && ReadFile(file,&store,sizeof(store),&read,nullptr) && read==sizeof(store);
    CloseHandle(file);
    if(!valid || store.magic!=ext_magic || store.version!=1 || store.bytes!=sizeof(store) || store.count>ext_limit || store.checksum!=ext_hash_storage(store)){
        OutputDebugStringW(L"TClock: ignored invalid detached-clock placement file\n");return;
    }
    for(DWORD i=0;i<store.count;++i){
        const Pose& p=store.poses[i];
        if(!p.home[0] || p.home[127] || p.display[127] || (p.floating!=0 && p.floating!=1) || p.size<4 || p.size>4096 || p.x<-100000 || p.x>100000 || p.y<-100000 || p.y>100000)continue;
        bool duplicate=false;for(const auto& old:ext_saved)if(!lstrcmpiW(old.home,p.home))duplicate=true;
        if(!duplicate)ext_saved.push_back(p);
    }
}
void ext_remember_pose(Clock& clock) {
    Pose p=clock.preferred;
    wcsncpy_s(p.home,clock.home.c_str(),_TRUNCATE);p.floating=clock.floating?1:0;
    if(clock.floating){
        const Display* d=ext_find_display(MonitorFromRect(&clock.frame,MONITOR_DEFAULTTONEAREST));
        if(d){wcsncpy_s(p.display,d->identity.c_str(),_TRUNCATE);p.x=MulDiv(clock.frame.left-d->work.left,96,d->dpi);
            p.y=MulDiv(clock.frame.top-d->work.top,96,d->dpi);p.size=MulDiv(ext_get_width(clock.frame),96,d->dpi);}
    }
    if(p.size<4)p.size=32;
    clock.preferred=p;
    for(auto& old:ext_saved)if(!lstrcmpiW(old.home,p.home)){old=p;return;}
    if(ext_saved.size()<ext_limit)ext_saved.push_back(p);
}
void ext_save_storage() {
    if(ext_path.empty())return;
    Storage store={};store.magic=ext_magic;store.version=1;store.bytes=sizeof(store);
    store.count=static_cast<DWORD>(std::min(ext_limit,ext_saved.size()));
    for(DWORD i=0;i<store.count;++i)store.poses[i]=ext_saved[i];
    store.checksum=ext_hash_storage(store);
    std::wstring temporary=ext_path+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){OutputDebugStringW(L"TClock: cannot save detached-clock placement\n");return;}
    DWORD written=0;bool valid=WriteFile(file,&store,sizeof(store),&written,nullptr) && written==sizeof(store) && FlushFileBuffers(file);
    CloseHandle(file);
    if(valid)valid=MoveFileExW(temporary.c_str(),ext_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!valid){DeleteFileW(temporary.c_str());OutputDebugStringW(L"TClock: cannot commit detached-clock placement\n");}
}
RECT ext_clamp_frame(RECT frame,const Display& display) {
    int width=ext_get_width(frame),height=ext_get_height(frame);
    int fit=std::max(4,std::min(width,std::min(ext_get_width(display.work),MulDiv(ext_get_height(display.work),width,height))));
    height=std::max(4,MulDiv(fit,height,width));width=fit;
    int x=std::max(display.work.left,std::min(frame.left,display.work.right-width));
    int y=std::max(display.work.top,std::min(frame.top,display.work.bottom-height));
    return RECT{x,y,x+width,y+height};
}
void ext_restore_pose(Clock& clock) {
    const Display* display=ext_find_identity(clock.preferred.display);
    if(!display && !ext_displays.empty())display=&ext_displays.front();
    if(!display)return;
    int size=std::max(4,std::min(4096,MulDiv(clock.preferred.size,display->dpi,96)));
    int x=display->work.left+MulDiv(clock.preferred.x,display->dpi,96);
    int y=display->work.top+MulDiv(clock.preferred.y,display->dpi,96);
    SIZE dimensions=ext_scale_size(size,clock.aspect);
    clock.frame={x,y,x+dimensions.cx,y+dimensions.cy};
    RECT reachable;IntersectRect(&reachable,&clock.frame,&display->work);
    if(ext_get_width(reachable)<16 || ext_get_height(reachable)<16)clock.frame=ext_clamp_frame(clock.frame,*display);
    clock.dirty=true;
}
bool ext_get_home(Clock& clock,RECT& frame,RECT& slot) {
    HWND target=ext_get_window(clock.binding.target),taskbar=ext_get_window(clock.binding.taskbar);
    if(!IsWindow(target) || !tbs_can_present(target,taskbar))return false;
    POINT origin={};if(!ClientToScreen(target,&origin))return false;
    frame=clock.binding.bounds;slot=clock.binding.slot;
    OffsetRect(&frame,origin.x,origin.y);OffsetRect(&slot,origin.x,origin.y);
    return !IsRectEmpty(&frame) && !IsRectEmpty(&slot);
}
bool ext_get_area(Clock& clock,RECT& area) {
    RECT frame,slot;if(!ext_get_home(clock,frame,slot))return false;
    HWND target=ext_get_window(clock.binding.target);POINT origin={};
    if(!GetClientRect(target,&area) || !ClientToScreen(target,&origin))return false;
    OffsetRect(&area,origin.x,origin.y);return !IsRectEmpty(&area);
}
bool ext_can_dock(Clock& clock,POINT point) {
    RECT slot;if(!ext_get_area(clock,slot))return false;
    const Display* display=ext_find_display(MonitorFromWindow(ext_get_window(clock.binding.target),MONITOR_DEFAULTTONEAREST));
    int margin=display?MulDiv(6,display->dpi,96):6;
    InflateRect(&slot,margin,margin);return PtInRect(&slot,point)!=FALSE;
}
bool ext_present_pixels(HWND window,const RECT& frame,const BYTE* pixels) {
    int size=ext_get_width(frame),height=ext_get_height(frame);if(size<4 || size>4096 || height<4 || height>4096)return false;
    BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* data=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
    bool result=false;
    if(dc && bitmap && data){
        HGDIOBJ previous=SelectObject(dc,bitmap);memcpy(data,pixels,static_cast<size_t>(size)*height*4);
        POINT position={frame.left,frame.top},source={};SIZE dimensions={size,height};
        BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        result=UpdateLayeredWindow(window,nullptr,&position,&dimensions,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
        SelectObject(dc,previous);
    }
    if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return result;
}
void ext_hide_cue(Clock& clock) { if(clock.cue)ShowWindow(clock.cue,SW_HIDE); }
void ext_show_cue(Clock& clock,POINT point) {
    if(!clock.floating || !ext_can_dock(clock,point)){ext_hide_cue(clock);return;}
    RECT slot;if(!ext_get_area(clock,slot))return;
    int size=std::max(4,std::min(1024,std::max(ext_get_width(slot),ext_get_height(slot))));
    RECT cue={slot.left,slot.top,slot.left+size,slot.top+size};
    std::vector<BYTE> pixels(static_cast<size_t>(size)*size*4,0);
    int width=std::min(size,ext_get_width(slot)),height=std::min(size,ext_get_height(slot));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(x<2 || y<2 || x>=width-2 || y>=height-2){
        size_t index=(static_cast<size_t>(y)*size+x)*4;pixels[index]=255;pixels[index+1]=160;pixels[index+2]=40;pixels[index+3]=255;}
    if(ext_present_pixels(clock.cue,cue,pixels.data())){
        HWND anchor=ext_get_window(clock.binding.anchor);if(!IsWindow(anchor))anchor=ext_get_window(clock.binding.taskbar);
        tbs_sync_order(clock.cue,anchor);
    }
}
int ext_hit_handle(const Clock& clock,POINT point) {
    if(!clock.floating || !clock.resizeMode || !PtInRect(&clock.frame,point))return HTNOWHERE;
    const Display* display=ext_find_display(MonitorFromRect(&clock.frame,MONITOR_DEFAULTTONEAREST));
    int size=ext_get_width(clock.frame),zone=std::min(size/3,std::max(6,display?MulDiv(10,display->dpi,96):10));
    bool left=point.x<clock.frame.left+zone,right=point.x>=clock.frame.right-zone;
    bool top=point.y<clock.frame.top+zone,bottom=point.y>=clock.frame.bottom-zone;
    if(left && top)return HTTOPLEFT;if(right && top)return HTTOPRIGHT;
    if(left && bottom)return HTBOTTOMLEFT;if(right && bottom)return HTBOTTOMRIGHT;
    return HTNOWHERE;
}
RECT ext_resize_frame(const RECT& before,int corner,int dx,int dy,int minimum) {
    bool left=corner==HTTOPLEFT || corner==HTBOTTOMLEFT;
    bool top=corner==HTTOPLEFT || corner==HTTOPRIGHT;
    int horizontal=left?-dx:dx,vertical=top?-dy:dy;
    int width=ext_get_width(before),height=ext_get_height(before);
    int delta=abs(dx)*height>abs(dy)*width?horizontal:MulDiv(vertical,width,height);
    int lower=std::max(minimum,MulDiv(minimum,width,height));
    int upper=std::min(4096,MulDiv(4096,width,height));
    if(width==height)upper=1024; // Retain the confirmed analog size bound.
    int size=std::max(lower,std::min(upper,width+delta));
    int nextHeight=std::max(4,MulDiv(size,height,width));
    int x=left?before.right-size:before.left,y=top?before.bottom-nextHeight:before.top;
    return RECT{x,y,x+size,y+nextHeight};
}
void ext_draw_handles(const Clock& clock,std::vector<BYTE>& pixels) {
    const Display* display=ext_find_display(MonitorFromRect(&clock.frame,MONITOR_DEFAULTTONEAREST));
    UINT dpi=display?display->dpi:96;
    int size=ext_get_width(clock.frame),height=ext_get_height(clock.frame),stroke=std::max(1,MulDiv(1,dpi,96));
    int handle=std::min(std::min(size,height)/4,std::max(3,MulDiv(5,dpi,96)));
    for(int y=0;y<height;++y)for(int x=0;x<size;++x){
        bool corner=(x<handle || x>=size-handle) && (y<handle || y>=height-handle);
        if(!corner && x>=stroke && y>=stroke && x<size-stroke && y<height-stroke)continue;
        BYTE* pixel=pixels.data()+(static_cast<size_t>(y)*size+x)*4;
        unsigned alpha=corner?192:96,color=corner?210:160;
        // Composite the faint frame over the premultiplied clock pixels.
        for(int c=0;c<3;++c)pixel[c]=static_cast<BYTE>((color*alpha+pixel[c]*(255-alpha)+127)/255);
        pixel[3]=static_cast<BYTE>(alpha+(pixel[3]*(255-alpha)+127)/255);
    }
}
void ext_update_clock(Clock& clock,bool force=false) {
    RECT frame,slot;bool attached=!clock.floating;
    if(attached){
        if(!ext_get_home(clock,frame,slot)){ShowWindow(clock.window,SW_HIDE);return;}
        clock.frame=frame;clock.homeSlot=slot;
    }
    int size=ext_get_width(clock.frame);
    int height=ext_get_height(clock.frame);
    if(size<4 || size>4096 || height<4 || height>4096)return;
    FILETIME utc,local;SYSTEMTIME time;ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&utc);ticks.LowPart=utc.dwLowDateTime;ticks.HighPart=utc.dwHighDateTime;
    ticks.QuadPart+=static_cast<LONGLONG>(ext_offset)*10000;utc.dwLowDateTime=ticks.LowPart;utc.dwHighDateTime=ticks.HighPart;
    if(!FileTimeToLocalFileTime(&utc,&local) || !FileTimeToSystemTime(&local,&time))return;
    bool animated=ext_options.mode==EXT_MODE_LED || ext_options.mode==EXT_MODE_FLIP || ext_options.mode==EXT_MODE_NIXIE;
    if(force || animated || clock.dirty || clock.second!=time.wSecond || clock.diameter!=size || !ext_equal_rect(clock.frame,clock.lastFrame)){
        if(!clock.renderer.render(SIZE{size,height},time,ext_options,ext_state->packet.stacked!=FALSE,
            ext_state->packet.animate!=FALSE,GetTickCount64()))return;
        const BYTE* pixels=clock.renderer.pixels.data();std::vector<BYTE> clipped;
        if(!attached){
            clipped.assign(pixels,pixels+static_cast<size_t>(size)*height*4);
            // Alpha 1 makes transparent corners available as a drag handle.
            for(size_t i=3;i<clipped.size();i+=4)if(!clipped[i])clipped[i]=1;
            if(clock.resizeMode)ext_draw_handles(clock,clipped);
            pixels=clipped.data();
        }
        if(attached){
            clipped.assign(pixels,pixels+static_cast<size_t>(size)*height*4);
            for(int y=0;y<height;++y)for(int x=0;x<size;++x){POINT point={clock.frame.left+x,clock.frame.top+y};
                BYTE* pixel=clipped.data()+(static_cast<size_t>(y)*size+x)*4;
                if(!PtInRect(&slot,point))memset(pixel,0,4);
                else if(ext_armed && !pixel[3])pixel[3]=1; // Alt also owns transparent digital gaps.
            }
            pixels=clipped.data();
        }
        if(!ext_present_pixels(clock.window,clock.frame,pixels))return;
        clock.second=time.wSecond;clock.diameter=size;clock.lastFrame=clock.frame;clock.dirty=false;
    }
    if(attached && !clock.drag){
        HWND anchor=ext_get_window(clock.binding.anchor);if(!IsWindow(anchor))anchor=ext_get_window(clock.binding.taskbar);
        if(GetWindow(anchor,GW_HWNDPREV)!=clock.window || !IsWindowVisible(clock.window))tbs_sync_order(clock.window,anchor);
    }else if(!IsWindowVisible(clock.window))ShowWindow(clock.window,SW_SHOWNOACTIVATE);
}
void ext_set_floating(Clock& clock,bool floating) {
    bool changed=clock.floating!=floating;
    clock.floating=floating;clock.dirty=true;
    if(!floating)clock.resizeMode=false;
    if(changed && ext_ready && IsWindow(ext_state->mainTarget))PostMessageW(ext_state->mainTarget,EXT_DETACH_LAYOUT,0,0);
    LONG_PTR style=GetWindowLongPtrW(clock.window,GWL_EXSTYLE);
    if(floating || clock.drag || ext_armed)style&=~WS_EX_TRANSPARENT;else style|=WS_EX_TRANSPARENT;
    SetWindowLongPtrW(clock.window,GWL_EXSTYLE,style);
    SetWindowPos(clock.window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    // Floating stacking is per clock; docking restores taskbar-owned ordering.
    if(floating || (style&WS_EX_TOPMOST))SetWindowPos(clock.window,
        floating && clock.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
void ext_finish_drag(Clock& clock,bool cancel) {
    if(!clock.drag)return;
    clock.drag=false;
    if(GetCapture()==clock.window)ReleaseCapture();
    ext_hide_cue(clock);
    if(cancel){clock.frame=clock.beforeFrame;ext_set_floating(clock,clock.beforeFloating);}
    else{
        POINT point;GetCursorPos(&point);
        if(clock.floating && !clock.resizing && ext_can_dock(clock,point))ext_set_floating(clock,false);
        ext_set_floating(clock,clock.floating);
        if(clock.moved){ext_remember_pose(clock);ext_save_storage();}
    }
    ext_update_clock(clock,true);
}
bool ext_begin_drag(Clock& clock) {
    if(clock.drag || !(GetAsyncKeyState(VK_LBUTTON)&0x8000))return false;
    ext_update_clock(clock,true);
    if(!IsWindowVisible(clock.window))return false;
    POINT point;if(!GetCursorPos(&point))return false;
    clock.beforeFloating=clock.floating;clock.beforeFrame=clock.frame;clock.origin=point;
    clock.grip={point.x-clock.frame.left,point.y-clock.frame.top};clock.moved=false;clock.drag=true;
    clock.resizeCorner=ext_hit_handle(clock,point);
    clock.resizing=clock.resizeCorner!=HTNOWHERE;
    ext_set_floating(clock,clock.floating);
    ext_update_clock(clock,true);
    SetForegroundWindow(clock.window);SetCapture(clock.window);
    DWORD foregroundPID=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPID);
    if(GetCapture()!=clock.window || foregroundPID!=GetCurrentProcessId()){
        if(GetCapture()==clock.window)ReleaseCapture();clock.drag=false;ext_set_floating(clock,clock.floating);return false;}
    return true;
}
void ext_move_clock(Clock& clock) {
    POINT point;if(!GetCursorPos(&point))return;
    if(!clock.moved){
        if(abs(point.x-clock.origin.x)<GetSystemMetrics(SM_CXDRAG) && abs(point.y-clock.origin.y)<GetSystemMetrics(SM_CYDRAG))return;
        clock.moved=true;
    }
    if(clock.resizing){
        int dx=point.x-clock.origin.x,dy=point.y-clock.origin.y;
        const Display* display=ext_find_display(MonitorFromRect(&clock.beforeFrame,MONITOR_DEFAULTTONEAREST));
        int minimum=display?MulDiv(16,display->dpi,96):16;
        clock.frame=ext_resize_frame(clock.beforeFrame,clock.resizeCorner,dx,dy,minimum);
        ext_hide_cue(clock);ext_update_clock(clock);return;
    }
    if(!clock.floating && PtInRect(&clock.homeSlot,point))return;
    if(!clock.floating)ext_set_floating(clock,true);
    int width=ext_get_width(clock.frame),height=ext_get_height(clock.frame);clock.frame={point.x-clock.grip.x,point.y-clock.grip.y,point.x-clock.grip.x+width,point.y-clock.grip.y+height};
    ext_update_clock(clock);ext_show_cue(clock,point);
}
bool ext_dock_clock(Clock& clock) {
    if(clock.drag)ext_finish_drag(clock,true);
    RECT frame,slot;if(!ext_get_home(clock,frame,slot))return false;
    ext_set_floating(clock,false);ext_hide_cue(clock);ext_remember_pose(clock);ext_update_clock(clock,true);return true;
}
LRESULT CALLBACK ext_process_window(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    Clock* clock=reinterpret_cast<Clock*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){auto create=reinterpret_cast<CREATESTRUCTW*>(lParam);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));return TRUE;}
    if(!clock)return DefWindowProcW(window,message,wParam,lParam);
    switch(message){
    case WM_NCHITTEST:return HTCLIENT;
    case WM_SETCURSOR:{
        POINT point;GetCursorPos(&point);
        int corner=clock->drag && clock->resizing?clock->resizeCorner:ext_hit_handle(*clock,point);
        int cursor=corner==HTTOPLEFT || corner==HTBOTTOMRIGHT?32642:
            corner==HTTOPRIGHT || corner==HTBOTTOMLEFT?32643:clock->floating && clock->resizeMode?32646:32512;
        SetCursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(cursor)));return TRUE;
    }
    case WM_MOUSEACTIVATE:return (clock->floating || ext_armed)?MA_ACTIVATE:MA_NOACTIVATE;
    case WM_LBUTTONDOWN:if(clock->floating || ext_armed)ext_begin_drag(*clock);return 0;
    case WM_MOUSEMOVE:if(clock->drag)ext_move_clock(*clock);return 0;
    case WM_LBUTTONUP:ext_finish_drag(*clock,false);return 0;
    case WM_KEYDOWN:if(wParam==VK_ESCAPE){
        if(clock->drag)ext_finish_drag(*clock,true);
        else if(clock->resizeMode){clock->resizeMode=false;ext_update_clock(*clock,true);}
    }return 0;
    case WM_CANCELMODE:case WM_CAPTURECHANGED:ext_finish_drag(*clock,true);return 0;
    case WM_DPICHANGED:
        if(clock->floating){
            // A corner gesture owns physical bounds, including its fixed opposite corner.
            if(!clock->drag || !clock->resizing){
                const RECT* proposed=reinterpret_cast<const RECT*>(lParam);clock->frame=*proposed;
                int size=std::max(4,std::min(4096,ext_get_width(clock->frame)));
                SIZE dimensions=ext_scale_size(size,clock->aspect);
                clock->frame.right=clock->frame.left+dimensions.cx;
                clock->frame.bottom=clock->frame.top+dimensions.cy;
                if(clock->drag){POINT point;GetCursorPos(&point);clock->grip={point.x-clock->frame.left,point.y-clock->frame.top};}
            }
            ext_update_clock(*clock,true);
        }return 0;
    case WM_CONTEXTMENU:{
        HMENU menu=CreatePopupMenu();if(!menu)return 0;
        AppendMenuW(menu,MF_STRING,1,b_EnglishMenu?L"Return to original clock":L"\u5143\u306e\u6642\u8a08\u9818\u57df\u306b\u623b\u3059");
        AppendMenuW(menu,MF_STRING,2,b_EnglishMenu?L"Return all clocks":L"\u3059\u3079\u3066\u306e\u6642\u8a08\u3092\u623b\u3059");
        if(clock->floating){
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING|(clock->resizeMode?MF_CHECKED:0),3,b_EnglishMenu?L"Resize":L"\u30b5\u30a4\u30ba\u5909\u66f4");
            AppendMenuW(menu,MF_STRING|(clock->topmost?MF_CHECKED:0),4,b_EnglishMenu?L"Always on top":L"\u6700\u524d\u9762\u306b\u8868\u793a");
        }
        POINT point;GetCursorPos(&point);SetForegroundWindow(window);
        UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);
        if(command==1 && !ext_dock_clock(*clock))MessageBoxW(window,b_EnglishMenu?L"The original clock area is unavailable. Show the taskbar or reconnect the display, then try again.":L"\u5143\u306e\u6642\u8a08\u9818\u57df\u304c\u5229\u7528\u3067\u304d\u307e\u305b\u3093\u3002\u30bf\u30b9\u30af\u30d0\u30fc\u3092\u8868\u793a\u3059\u308b\u304b\u753b\u9762\u3092\u518d\u63a5\u7d9a\u3057\u3066\u304f\u3060\u3055\u3044\u3002",L"TClock",MB_OK|MB_ICONINFORMATION);
        if(command==2)for(auto& item:ext_clocks)ext_dock_clock(*item);
        if(command==3){clock->resizeMode=!clock->resizeMode;ext_update_clock(*clock,true);}
        if(command==4 && clock->floating){clock->topmost=!clock->topmost;ext_set_floating(*clock,true);}
        if(command==1 || command==2)ext_save_storage();return 0;
    }
    case WM_CLOSE:ext_dock_clock(*clock);ext_save_storage();return 0;
    case WM_ERASEBKGND:return 1;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
std::unique_ptr<Clock> ext_create_clock(const std::wstring& identity) {
    auto clock=std::make_unique<Clock>();clock->home=identity;
    clock->aspect=ext_state->packet.baseSize;
    if(!clock->renderer.configure(ext_module,ext_state->packet))return nullptr;
    clock->window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,L"TClockDetachedClock",L"TClock",WS_POPUP,0,0,4,4,ext_owner,nullptr,ext_module,clock.get());
    clock->cue=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"TClockDetachCue",L"",WS_POPUP,0,0,4,4,ext_owner,nullptr,ext_module,nullptr);
    if(!clock->window || !clock->cue){if(clock->window)DestroyWindow(clock->window);if(clock->cue)DestroyWindow(clock->cue);return nullptr;}
    for(const auto& saved:ext_saved)if(!lstrcmpiW(saved.home,identity.c_str())){clock->preferred=saved;clock->floating=saved.floating!=0;break;}
    if(clock->floating){ext_restore_pose(*clock);ext_set_floating(*clock,true);}
    return clock;
}
void ext_destroy_clocks(bool reset) {
    for(auto& clock:ext_clocks){
        if(clock->drag)ext_finish_drag(*clock,true);
        if(reset){clock->floating=false;ext_remember_pose(*clock);}
        DestroyWindow(clock->window);DestroyWindow(clock->cue);
    }
    if(ext_keyboard){UnhookWindowsHookEx(ext_keyboard);ext_keyboard=nullptr;}
    ext_armed=false;
    if(reset){for(auto& p:ext_saved)p.floating=0;ext_save_storage();}
    ext_clocks.clear();ext_ready=false;KillTimer(ext_owner,ext_timer);
}
bool ext_validate_packet(const EXT_DETACH_PACKET& packet,HWND sender) {
    if(packet.version!=EXT_DETACH_VERSION || packet.bytes!=sizeof(packet) || packet.count>EXT_DETACH_MAX || packet.enabled>1)return false;
    DWORD process=0;GetWindowThreadProcessId(sender,&process);if(!process)return false;
    if(!packet.enabled)return packet.count==0;
    if(packet.options.mode<EXT_MODE_NORMAL || packet.options.mode>EXT_MODE_LED || !packet.options.enabled || packet.options.face<0 || packet.options.face>1 || (packet.offsetMS<-32768 || packet.offsetMS>32767))return false;
    if(packet.baseSize.cx<4 || packet.baseSize.cy<4 || packet.baseSize.cx>4096 || packet.baseSize.cy>4096)return false;
    if(packet.options.mode==EXT_MODE_LED){
        if(packet.text.columns<1 || packet.text.columns>LED_TEXT_MAX || packet.text.options.count<0 ||
            packet.text.options.count>LED_MESSAGE_MAX || packet.text.clockText[LED_TEXT_MAX] ||
            packet.text.options.dateFormat[LED_FORMAT_MAX])return false;
        for(int i=0;i<LED_MESSAGE_MAX;++i)if(packet.text.expanded[i][LED_TEXT_MAX] || packet.text.options.messages[i].text[LED_FORMAT_MAX])return false;
    }
    for(UINT i=0;i<packet.count;++i){
        const auto& h=packet.homes[i];DWORD targetPID=0,barPID=0;
        GetWindowThreadProcessId(ext_get_window(h.target),&targetPID);GetWindowThreadProcessId(ext_get_window(h.taskbar),&barPID);
        if(targetPID!=process || barPID!=process || h.bounds.left<-10000 || h.bounds.left>10000 ||
            h.bounds.top<-10000 || h.bounds.top>10000 || h.bounds.right<-10000 || h.bounds.right>10000 ||
            h.bounds.bottom<-10000 || h.bounds.bottom>10000 || h.slot.left<0 || h.slot.top<0 ||
            h.slot.right>10000 || h.slot.bottom>10000 || IsRectEmpty(&h.slot))return false;
        if(h.bounds.right-h.bounds.left<4 || h.bounds.right-h.bounds.left>4096 ||
            h.bounds.bottom-h.bounds.top<4 || h.bounds.bottom-h.bounds.top>4096)return false;
        for(UINT j=0;j<i;++j)if(packet.homes[j].target==h.target)return false;
    }
    return packet.count>0;
}
bool ext_receive_packet(const EXT_DETACH_PACKET& packet,HWND sender) {
    if(!ext_validate_packet(packet,sender))return false;
    if(!packet.enabled){ext_destroy_clocks(true);return true;}
    ext_load_storage();if(ext_displays.empty())ext_refresh_displays();
    bool optionsChanged=memcmp(&ext_options,&packet.options,sizeof(ext_options))!=0 || ext_offset!=packet.offsetMS;
    ext_options=packet.options;ext_offset=packet.offsetMS;ext_state->packet=packet;
    std::array<Clock*,EXT_DETACH_MAX> homes={};
    for(UINT i=0;i<packet.count;++i){
        const Display* display=ext_find_display(MonitorFromWindow(ext_get_window(packet.homes[i].target),MONITOR_DEFAULTTONEAREST));
        if(!display)return false;
        for(auto& clock:ext_clocks)if(!lstrcmpiW(clock->home.c_str(),display->identity.c_str())){homes[i]=clock.get();break;}
        if(!homes[i]){
            if(ext_clocks.size()>=ext_limit)return false;
            auto clock=ext_create_clock(display->identity);if(!clock)return false;
            homes[i]=clock.get();ext_clocks.push_back(std::move(clock));
        }
        if(!homes[i]->renderer.configure(ext_module,packet))return false;
    }
    // Commit bindings only after every required renderer and window exists.
    for(auto& clock:ext_clocks){bool retained=false;for(UINT i=0;i<packet.count;++i)if(homes[i]==clock.get())retained=true;if(!retained)clock->binding={};}
    for(UINT i=0;i<packet.count;++i){
        bool geometryChanged=memcmp(&homes[i]->binding,&packet.homes[i],sizeof(EXT_DETACH_HOME))!=0;
        SIZE aspect={ext_get_width(packet.homes[i].bounds),ext_get_height(packet.homes[i].bounds)};
        bool aspectChanged=static_cast<LONGLONG>(aspect.cx)*homes[i]->aspect.cy!=static_cast<LONGLONG>(aspect.cy)*homes[i]->aspect.cx;
        homes[i]->aspect=aspect;homes[i]->binding=packet.homes[i];
        if(homes[i]->floating && aspectChanged && !homes[i]->drag){
            SIZE dimensions=ext_scale_size(ext_get_width(homes[i]->frame),aspect);
            homes[i]->frame.right=homes[i]->frame.left+dimensions.cx;
            homes[i]->frame.bottom=homes[i]->frame.top+dimensions.cy;
        }
        if(optionsChanged || geometryChanged)homes[i]->dirty=true;
    }
    // Restore floating homes whose monitor is disconnected without fabricating a taskbar binding.
    for(const auto& saved:ext_saved)if(saved.floating){
        bool found=false;for(const auto& clock:ext_clocks)if(!lstrcmpiW(clock->home.c_str(),saved.home))found=true;
        if(!found && ext_clocks.size()<ext_limit){auto clock=ext_create_clock(saved.home);if(clock)ext_clocks.push_back(std::move(clock));}
    }
    // Disconnected floating homes share appearance changes, but retain their own pose.
    for(auto& clock:ext_clocks)if(!clock->binding.target){
        if(!clock->renderer.configure(ext_module,packet))return false;
        if(optionsChanged && !clock->drag){
            clock->aspect=packet.baseSize;
            SIZE dimensions=ext_scale_size(ext_get_width(clock->frame),clock->aspect);
            clock->frame.right=clock->frame.left+dimensions.cx;clock->frame.bottom=clock->frame.top+dimensions.cy;
            clock->dirty=true;
        }
    }
    if(!ext_keyboard){
        ext_keyboard=SetWindowsHookExW(WH_KEYBOARD_LL,ext_observe_keys,ext_module,0);
        if(!ext_keyboard)return false;
    }
    ext_state->mainTarget=ext_get_window(packet.homes[0].target);
    ext_ready=true;SetTimer(ext_owner,ext_timer,ext_options.mode==EXT_MODE_LED?LED_FRAME_MS:
        (ext_options.mode==EXT_MODE_FLIP || ext_options.mode==EXT_MODE_NIXIE)?33:100,nullptr);return true;
}
} // namespace

extern "C" void ext_init_host(HWND owner,HINSTANCE module) {
    if(!ext_state){try{ext_state=new HostState();}catch(...){return;}}
    ext_owner=owner;ext_module=module;
    WNDCLASSEXW cls={};cls.cbSize=sizeof(cls);cls.hInstance=module;cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));
    cls.lpfnWndProc=ext_process_window;cls.lpszClassName=L"TClockDetachedClock";RegisterClassExW(&cls);
    cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"TClockDetachCue";RegisterClassExW(&cls);
}
extern "C" void ext_close_hosts() { if(ext_state){ext_destroy_clocks(false);delete ext_state;ext_state=nullptr;} }
extern "C" BOOL ext_handle_message(HWND owner,UINT message,WPARAM wParam,LPARAM lParam,LRESULT* result) {
    if(!ext_state)return FALSE;
    DpiScope dpi;
    if(message==WM_COPYDATA){
        const COPYDATASTRUCT* data=reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if(!data || data->dwData!=EXT_DETACH_DATA)return FALSE;
        *result=FALSE;
        if(data->cbData==sizeof(EXT_DETACH_PACKET) && data->lpData){
            EXT_DETACH_PACKET packet;memcpy(&packet,data->lpData,sizeof(packet));
            try{
                if(ext_receive_packet(packet,reinterpret_cast<HWND>(wParam))){
                    *result=EXT_DETACH_ACK;
                    for(UINT i=0;i<packet.count;++i)for(const auto& clock:ext_clocks)
                        if(clock->floating && clock->binding.target==packet.homes[i].target)*result|=static_cast<LRESULT>(1)<<i;
                }
            }catch(...){OutputDebugStringW(L"TClock: detached-clock allocation failed\n");}
            if(!*result && !ext_ready)ext_destroy_clocks(false);
        }return TRUE;
    }
    if(message==EXT_DETACH_ARM){
        *result=0;
        if(ext_ready && ext_armed!=(wParam!=0)){ext_armed=wParam!=0;for(auto& clock:ext_clocks)if(!clock->floating && !clock->drag){ext_set_floating(*clock,false);ext_update_clock(*clock,true);}}
        return TRUE;
    }
    if(message==WM_TIMER && wParam==ext_timer && ext_ready && ext_armed!=((GetAsyncKeyState(VK_MENU)&0x8000)!=0))PostMessageW(ext_owner,EXT_DETACH_ARM,(GetAsyncKeyState(VK_MENU)&0x8000)?1:0,0);
    if(message==EXT_DETACH_COMMIT || (message==WM_TIMER && wParam==ext_timer)){
        *result=0;if(ext_ready)try{for(auto& clock:ext_clocks)ext_update_clock(*clock);}catch(...){OutputDebugStringW(L"TClock: detached-clock update failed\n");}
        return TRUE;
    }
    if(message==WM_DISPLAYCHANGE && ext_ready){
        ext_refresh_displays();for(auto& clock:ext_clocks){if(clock->drag)ext_finish_drag(*clock,true);if(clock->floating)ext_restore_pose(*clock);clock->dirty=true;}
    }
    if((message==WM_TIMECHANGE || message==WM_SETTINGCHANGE || message==WM_POWERBROADCAST) && ext_ready)for(auto& clock:ext_clocks)clock->dirty=true;
    (void)owner;return FALSE;
}
