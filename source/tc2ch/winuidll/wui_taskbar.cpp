// Native vertical taskbar reservation through XAML diagnostics. Subscription is
// gated on the private cache-release capability verified on the native taskbar.
// Each diagnostics session loads a private shadow copy. Diagnostics may retain
// its loader reference after unadvise; the normal renderer DLL remains replaceable.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "wui_taskbar.h"
#include <ocidl.h>
#include <xamlom.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>
#undef GetCurrentTime
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.h>
#include <weakreference.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Data.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")
#pragma comment(linker, "/EXPORT:WuiConfigureTaskbar,PRIVATE")

namespace {
using namespace winrt::Windows::UI::Xaml;
using winrt::Windows::UI::Core::CoreDispatcher;
using winrt::Windows::UI::Core::CoreDispatcherPriority;
using Inspectable = winrt::Windows::Foundation::IInspectable;
const CLSID kTaskbar = {0xb54cf279,0x1fd7,0x4262,{0x9e,0x37,0x6a,0x64,0xe6,0x62,0xe0,0x92}};
std::atomic<long> g_objects{0};
SRWLOCK g_configLock = SRWLOCK_INIT;
std::wstring g_sessionData;
bool g_configured = false, g_configClaimed = false;

bool wui_take_config(std::wstring& data) {
    AcquireSRWLockExclusive(&g_configLock);
    if (!g_configured || g_configClaimed) {
        ReleaseSRWLockExclusive(&g_configLock); return false;
    }
    try { data = g_sessionData; g_configClaimed = true; }
    catch (...) { ReleaseSRWLockExclusive(&g_configLock); throw; }
    ReleaseSRWLockExclusive(&g_configLock); return true;
}

struct Log {
    HANDLE file = INVALID_HANDLE_VALUE;
    SRWLOCK lock = SRWLOCK_INIT;
    DWORD bytes = 0;
    ~Log() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
    void Open(const wchar_t* path) {
        file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            WORD bom = 0xfeff; DWORD count;
            WriteFile(file, &bom, sizeof(bom), &count, nullptr);
        }
    }
    void Write(const wchar_t* format, ...) {
        wchar_t line[1024]; va_list args; va_start(args, format);
        int length = _vsnwprintf_s(line, _countof(line), _TRUNCATE, format, args);
        va_end(args);
        if (length < 0) length = static_cast<int>(wcslen(line));
        AcquireSRWLockExclusive(&lock);
        if (file != INVALID_HANDLE_VALUE && bytes < 262144) {
            DWORD count;
            WriteFile(file, line, static_cast<DWORD>(length * sizeof(wchar_t)), &count, nullptr);
            bytes += count;
        }
        ReleaseSRWLockExclusive(&lock);
    }
};

enum class DockEdge { Unknown, Left, Top, Right, Bottom };
DockEdge wui_get_edge(HWND shell) {
    RECT task{}; MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    if (!GetWindowRect(shell, &task) || !GetMonitorInfoW(
        MonitorFromWindow(shell, MONITOR_DEFAULTTONEAREST), &monitor)) return DockEdge::Unknown;
    if (task.bottom - task.top > task.right - task.left) {
        return static_cast<long long>(task.left) + task.right <
            static_cast<long long>(monitor.rcMonitor.left) + monitor.rcMonitor.right
            ? DockEdge::Left : DockEdge::Right;
    }
    return static_cast<long long>(task.top) + task.bottom <
        static_cast<long long>(monitor.rcMonitor.top) + monitor.rcMonitor.bottom
        ? DockEdge::Top : DockEdge::Bottom;
}

bool wui_check_windows(HWND shell, HWND clock) {
    DWORD shellPid = 0, clockPid = 0;
    if (!IsWindow(shell) || !IsWindow(clock)) return false;
    GetWindowThreadProcessId(shell, &shellPid);
    GetWindowThreadProcessId(clock, &clockPid);
    return shellPid == GetCurrentProcessId() && clockPid == shellPid &&
        GetAncestor(clock, GA_ROOT) == shell;
}
double wui_measure_slot(HWND shell, HWND clock) {
    RECT task{}, area{};
    if (!wui_check_windows(shell, clock) || !IsWindowVisible(shell) || !IsWindowVisible(clock) ||
        !GetWindowRect(shell, &task) || !GetWindowRect(clock, &area)) return 0;
    if (task.bottom - task.top <= task.right - task.left ||
        area.left < task.left - 2 || area.right > task.right + 2 ||
        area.top < task.top || area.bottom > task.bottom || area.bottom <= area.top) return 0;
    return static_cast<double>(area.bottom - area.top);
}

// The lease and all local-value operations belong to the captured UI dispatcher.
// Only a weak element reference survives between operations or tree generations.
template<typename T> struct PropertyLease {
    Inspectable local{nullptr};
    T baseline{}, written{};
    bool owned = false, unset = false;
    bool Changed(FrameworkElement const& element, DependencyProperty const& property) const {
        return element.GetBindingExpression(property) ||
            (owned && winrt::unbox_value<T>(element.GetValue(property)) != written);
    }
    HRESULT Write(FrameworkElement const& element, DependencyProperty const& property, T desired) {
        if (owned && written == desired) return S_OK;
        if (!owned) {
            local = element.ReadLocalValue(property);
            unset = local == DependencyProperty::UnsetValue();
            baseline = winrt::unbox_value<T>(element.GetValue(property));
        }
        written = desired; owned = true;
        element.SetValue(property, winrt::box_value(written));
        return winrt::unbox_value<T>(element.GetValue(property)) == written ? S_OK : E_FAIL;
    }
    HRESULT Restore(FrameworkElement const& element, DependencyProperty const& property) {
        if (!owned) return S_OK;
        if (Changed(element, property)) { Clear(); return S_FALSE; }
        if (unset) element.ClearValue(property); else element.SetValue(property, local);
        if (winrt::unbox_value<T>(element.GetValue(property)) != baseline ||
            (unset && element.ReadLocalValue(property) != DependencyProperty::UnsetValue())) return E_FAIL;
        Clear(); return S_OK;
    }
    void Clear() { local = nullptr; owned = false; }
};
struct Lease {
    winrt::weak_ref<FrameworkElement> target;
    PropertyLease<Thickness> margin;
    HWND shell = nullptr, clock = nullptr, bridge = nullptr, tray = nullptr;
    HANDLE published = nullptr;
    double contentDelta = 0, marginScale = 0;
    bool baselineKnown = false;
    bool anchorOwned = false, conflict = false, restoring = false;
    DockEdge edge = DockEdge::Unknown;

    bool ContentTop(FrameworkElement const& element, double& top) {
        RECT task{}, host{}, native{};
        if (!IsWindow(bridge) || !IsWindow(tray) || GetAncestor(bridge, GA_ROOT) != shell ||
            GetAncestor(tray, GA_ROOT) != shell || !GetWindowRect(shell, &task) ||
            !GetWindowRect(bridge, &host) || !GetWindowRect(tray, &native)) return false;
        if (task.bottom - task.top <= task.right - task.left ||
            !element.IsLoaded() || element.Visibility() != Visibility::Visible ||
            element.ActualWidth() <= 0 || element.ActualHeight() <= 0) return false;
        auto root = element.XamlRoot();
        auto parent = Media::VisualTreeHelper::GetParent(element).try_as<FrameworkElement>();
        if (!root || !parent || !parent.IsLoaded() || parent.Visibility() != Visibility::Visible) return false;
        double scale = root.RasterizationScale();
        if (!std::isfinite(scale) || scale <= 0) return false;
        auto size = root.Size();
        if (size.Height <= size.Width ||
            std::abs(size.Width * scale - (host.right - host.left)) > 2 ||
            std::abs(size.Height * scale - (host.bottom - host.top)) > 2) return false;
        auto position = parent.TransformToVisual(nullptr).TransformPoint({0, 0});
        double bottom = (position.Y + parent.ActualHeight()) * scale + host.top;
        if (!std::isfinite(bottom) || std::abs(bottom - native.bottom) > 2 ||
            std::abs(parent.ActualWidth() * scale - (native.right - native.left)) > 2) return false;
        top = element.TransformToVisual(nullptr).TransformPoint({0, 0}).Y * scale + host.top - task.top;
        return std::isfinite(top);
    }
    HRESULT Publish(FrameworkElement const& element, Log& log) {
        if (!wui_check_windows(shell, clock)) return S_FALSE;
        HANDLE current = GetPropW(clock, WUI_TASKBAR_ANCHOR_PROPERTY);
        if ((anchorOwned && current != published) || (!anchorOwned && current)) {
            anchorOwned = false; conflict = true;
            log.Write(L"anchor-conflict\tno-overwrite\r\n"); return S_FALSE;
        }
        double content = 0; RECT task{};
        if (!ContentTop(element, content) || !GetWindowRect(shell, &task)) return E_PENDING;
        auto root = element.XamlRoot();
        double scale = root ? root.RasterizationScale() : 0;
        if (!std::isfinite(scale) || scale <= 0) return E_FAIL;
        double position = content - contentDelta * scale;
        RECT area{};
        if (!GetWindowRect(clock, &area) || !std::isfinite(position) ||
            position < area.bottom - area.top || position > task.bottom - task.top) return E_PENDING;
        HANDLE value = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(std::lround(position)) + 1);
        if (current != value) {
            if (!SetPropW(clock, WUI_TASKBAR_ANCHOR_PROPERTY, value)) return HRESULT_FROM_WIN32(GetLastError());
            log.Write(L"anchor-published\ty=%lld\r\n", static_cast<long long>(reinterpret_cast<INT_PTR>(value) - 1));
        }
        published = value; anchorOwned = true; return S_OK;
    }
    HRESULT ReleaseAnchor(Log& log) {
        if (!anchorOwned) return S_OK;
        if (!wui_check_windows(shell, clock)) { anchorOwned = false; return S_OK; }
        if (GetPropW(clock, WUI_TASKBAR_ANCHOR_PROPERTY) != published) {
            anchorOwned = false; conflict = true; return S_FALSE;
        }
        if (RemovePropW(clock, WUI_TASKBAR_ANCHOR_PROPERTY) != published) return E_FAIL;
        anchorOwned = false; log.Write(L"anchor-removed\r\n"); return S_OK;
    }
    HRESULT Restore(Log& log, bool settle = true, bool boundary = false) {
        auto element = target.get();
        if (element && !element.Dispatcher().HasThreadAccess()) return RPC_E_WRONG_THREAD;
        restoring = true;
        if (!element) margin.Clear();
        else {
            bool hadLease = margin.owned;
            HRESULT result = margin.Restore(element, FrameworkElement::MarginProperty());
            if (FAILED(result)) return result;
            if (result == S_FALSE) { conflict = true; log.Write(L"restore-external-change\tno-overwrite\r\n"); }
            if (hadLease && result == S_OK) log.Write(L"restored\tmarginTop=%.3f\ttid=%lu\r\n",
                element.Margin().Top, GetCurrentThreadId());
        }
        if ((anchorOwned || (boundary && baselineKnown)) && settle && !conflict && element && wui_check_windows(shell, clock) &&
            IsWindow(tray) && IsWindow(bridge)) {
            HRESULT result = Publish(element, log);
            // A transitional XAML placement must never overwrite a valid anchor.
            // The native tray can still finish restoring to the last published value.
            if (FAILED(result) && (result != E_PENDING || !anchorOwned)) return result;
            if (result == S_OK || (result == E_PENDING && anchorOwned)) {
                RECT task{}, native{};
                if (!GetWindowRect(shell, &task) || !GetWindowRect(tray, &native)) return E_FAIL;
                auto anchor = reinterpret_cast<INT_PTR>(published) - 1;
                if (std::abs(static_cast<long long>(native.top - task.top) - anchor) > 1) return E_PENDING;
            }
        }
        HRESULT result = ReleaseAnchor(log);
        if (FAILED(result)) return result;
        restoring = false; return conflict ? S_FALSE : S_OK;
    }
    HRESULT ChangeEdge(DockEdge next, Log& log) {
        if (edge == next) return S_OK;
        // Preserve the unreserved content offset until native layout has consumed
        // the restored margin. A moved tray may still expose the previous lease.
        bool vertical = next == DockEdge::Left || next == DockEdge::Right;
        HRESULT result = Restore(log, vertical, vertical);
        if (FAILED(result)) return result;
        margin.Clear(); conflict = false;
        log.Write(L"edge-change\tprevious=%d\tnext=%d\r\n", static_cast<int>(edge), static_cast<int>(next));
        edge = next; return S_OK;
    }
    HRESULT Update(double pixels, Log& log) {
        auto element = target.get();
        if (!element) return Restore(log);
        if (!element.Dispatcher().HasThreadAccess()) return RPC_E_WRONG_THREAD;
        if (edge != DockEdge::Left && edge != DockEdge::Right) return Restore(log, false);
        if (pixels <= 0 || restoring) return Restore(log);
        if (conflict) return S_FALSE;
        auto property = FrameworkElement::MarginProperty();
        if (margin.Changed(element, property)) {
            bool hadLease = margin.owned;
            HRESULT result = Restore(log);
            if (hadLease) conflict = true;
            return FAILED(result) ? result : S_FALSE;
        }
        auto root = element.XamlRoot(); RECT task{}, native{};
        double scale = root ? root.RasterizationScale() : 0;
        if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(pixels) ||
            !GetWindowRect(shell, &task) || pixels >= task.bottom - task.top) return Restore(log);
        if (margin.owned && scale != marginScale) return Restore(log);
        if (!anchorOwned) {
            double content = 0;
            if (!IsWindow(tray) || GetAncestor(tray, GA_ROOT) != shell ||
                !GetWindowRect(tray, &native) || !ContentTop(element, content)) return S_FALSE;
            contentDelta = (content - (native.top - task.top)) / scale;
            baselineKnown = true;
        }
        // Publish before measure can move the native tray. The clock consumes
        // the notification content anchor, independent of this added margin.
        HRESULT result = Publish(element, log);
        if (result != S_OK) {
            if (margin.owned) return Restore(log);
            return result;
        }
        Thickness desired = margin.owned ? margin.baseline : element.Margin();
        desired.Top += pixels / scale;
        bool unchanged = margin.owned && margin.written == desired;
        result = margin.Write(element, property, desired);
        if (FAILED(result)) return result;
        marginScale = scale;
        if (!unchanged) log.Write(L"applied\tpixels=%.3f\tscale=%.6f\tmarginTop=%.3f\ttid=%lu\r\n",
            pixels, scale, desired.Top, GetCurrentThreadId());
        return S_OK;
    }
    void Clear() {
        target = {}; margin.Clear(); shell = clock = bridge = tray = nullptr;
        published = nullptr; anchorOwned = conflict = restoring = baselineKnown = false;
        contentDelta = marginScale = 0; edge = DockEdge::Unknown;
    }
};
// Diagnostics retain reported objects beyond native-window destruction unless their
// cache references are returned. This private capability is required before Advise.
// ABI reference: https://github.com/ramensoftware/windhawk-mods/blob/main/mods/windows-11-taskbar-styler.wh.cpp
static constexpr GUID kCacheHooks = {0x735941a2,0x3ee3,0x495a,{0x8d,0xa9,0x97,0x26,0x27,0x00,0x30,0x75}};
struct CacheHooks : IUnknown { virtual HRESULT STDMETHODCALLTYPE UnregisterInstance(InstanceHandle) = 0; };
using CacheQueue = winrt::Windows::System::DispatcherQueue;
struct CacheEntry { winrt::weak_ref<Inspectable> weak; Inspectable held{nullptr}; unsigned long long sequence = 0; };
struct CacheThread {
    CacheQueue dispatcher{nullptr};
    std::unordered_map<InstanceHandle, CacheEntry> pending;
    ULONGLONG changed = 0;
    DWORD thread = 0;
    bool queued = false;
};
struct Node { InstanceHandle parent = 0; std::wstring type; winrt::weak_ref<Inspectable> weak; };
struct Candidate {
    winrt::weak_ref<FrameworkElement> target;
    CoreDispatcher dispatcher{nullptr};
    HWND bridge = nullptr;
    InstanceHandle handle = 0;
    unsigned long long generation = 0;
};

class Taskbar final : public IObjectWithSite, public IVisualTreeServiceCallback2 {
    std::atomic<ULONG> refs{1};
    std::atomic<bool> stopping{false};
    SRWLOCK treeLock = SRWLOCK_INIT;
    IXamlDiagnostics* diagnostics = nullptr;
    IVisualTreeService* service = nullptr;
    CacheHooks* cacheHooks = nullptr;
    std::unordered_map<DWORD, std::shared_ptr<CacheThread>> cacheThreads;
    unsigned long long cacheSequence = 0, cacheCallbacks = 0;
    std::atomic<unsigned> cacheActions{0};
    std::atomic<bool> cacheFailed{false};
    std::unordered_map<InstanceHandle, Node> nodes;
    Candidate candidate;
    Lease lease;
    CoreDispatcher active{nullptr};
    HANDLE stop = nullptr, done = nullptr, failed = nullptr, ready = nullptr, actionDone = nullptr;
    HWND shell = nullptr, clock = nullptr;
    HMODULE module = nullptr;
    std::atomic<HRESULT> actionResult{E_PENDING};
    bool actionPending = false;
    Log log;

    Inspectable Inspect(InstanceHandle handle) {
        Inspectable value{nullptr};
        winrt::check_hresult(diagnostics->GetIInspectableFromHandle(handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(value))));
        return value;
    }
    // Called on the reporting UI thread under treeLock; never unregister here.
    winrt::weak_ref<Inspectable> Observe(InstanceHandle handle) {
        if (!handle) return {};
        Inspectable value{nullptr};
        HRESULT result = diagnostics->GetIInspectableFromHandle(handle,
            reinterpret_cast<::IInspectable**>(winrt::put_abi(value)));
        if (FAILED(result) || !value) return {};
        auto weakSource = value.try_as<::IWeakReferenceSource>();
        // The verified native composition visuals do not expose weak references.
        // Keep only that typed capability alive until its reporting UI drains it.
        // Every other non-weak object remains unsupported rather than released blindly.
        Inspectable held{nullptr}; winrt::weak_ref<Inspectable> weak;
        if (weakSource) weak = winrt::make_weak(value);
        else if (value.try_as<winrt::Windows::UI::Composition::Visual>()) held = value;
        else {
            log.Write(L"cache-unsupported\thandle=%llu\ttype=%s\ttid=%lu\r\n", handle, winrt::get_class_name(value).c_str(), GetCurrentThreadId());
            cacheFailed = true; return {};
        }
        DWORD thread = GetCurrentThreadId();
        auto& state = cacheThreads[thread];
        if (!state) {
            state = std::make_shared<CacheThread>(); state->thread = thread;
            state->dispatcher = CacheQueue::GetForCurrentThread();
            if (!state->dispatcher) { cacheFailed = true; return weak; }
            log.Write(L"cache-ui\ttid=%lu\r\n", thread);
        }
        state->pending[handle] = {weak, held, ++cacheSequence};
        state->changed = GetTickCount64();
        return weak;
    }
    void ReleaseCache(const std::shared_ptr<CacheThread>& state) {
        if (GetCurrentThreadId() != state->thread) {
            cacheFailed = true; log.Write(L"cache-wrong-thread\texpected=%lu\tactual=%lu\r\n", state->thread, GetCurrentThreadId());
            return;
        }
        std::unordered_map<InstanceHandle, CacheEntry> batch;
        AcquireSRWLockExclusive(&treeLock);
        if (GetTickCount64() - state->changed >= 100) batch.swap(state->pending);
        ReleaseSRWLockExclusive(&treeLock);
        unsigned released = 0, expired = 0, skipped = 0, weakExpired = 0, lookupMissing = 0, changedIdentity = 0;
        for (auto& record : batch) {
            bool matches = false;
            try {
                // Both strong references leave scope BEFORE unregister. Weak identity
                // prevents a queued record releasing an object reusing the same handle.
                {
                    auto expected = record.second.held ? record.second.held : record.second.weak.get();
                    Inspectable current{nullptr};
                    HRESULT hr = diagnostics->GetIInspectableFromHandle(record.first,
                        reinterpret_cast<::IInspectable**>(winrt::put_abi(current)));
                    if (!expected) ++weakExpired;
                    if (FAILED(hr) || !current) ++lookupMissing;
                    matches = expected && SUCCEEDED(hr) && current &&
                        expected.as<::IUnknown>().get() == current.as<::IUnknown>().get();
                    if (expected && SUCCEEDED(hr) && current && !matches) ++changedIdentity;
                }
                // Release the queued composition reference on this UI before the cache reference.
                record.second.held = nullptr;
                if (!matches) { ++skipped; continue; }
                HRESULT hr = cacheHooks->UnregisterInstance(record.first);
                if (FAILED(hr)) {
                    log.Write(L"cache-release-error\thandle=%llu\tsequence=%llu\thr=0x%08X\ttid=%lu\r\n",
                        record.first, record.second.sequence, static_cast<unsigned>(hr), GetCurrentThreadId());
                    cacheFailed = true;
                } else { ++released; if (record.second.weak && !record.second.weak.get()) ++expired; }
            } catch (...) { cacheFailed = true; }
        }
        AcquireSRWLockExclusive(&treeLock); state->queued = false; ReleaseSRWLockExclusive(&treeLock);
        if (!batch.empty()) log.Write(L"cache-batch\tcount=%zu\treleased=%u\texpired=%u\tidentity-skipped=%u\tweakExpired=%u\tlookupMissing=%u\tchangedIdentity=%u\ttid=%lu\r\n",
            batch.size(), released, expired, skipped, weakExpired, lookupMissing, changedIdentity, GetCurrentThreadId());
    }
    void FlushCache() {
        std::vector<std::shared_ptr<CacheThread>> readyStates;
        AcquireSRWLockExclusive(&treeLock);
        for (auto& item : cacheThreads) {
            auto state = item.second;
            if (!state->queued && !state->pending.empty() && GetTickCount64() - state->changed >= 100) {
                state->queued = true; readyStates.push_back(state);
            }
        }
        ReleaseSRWLockExclusive(&treeLock);
        for (auto& state : readyStates) {
            AddRef(); ++cacheActions;
            bool queued = false;
            try { queued = state->dispatcher && state->dispatcher.TryEnqueue(
                winrt::Windows::System::DispatcherQueuePriority::Low, [this, state] {
                    try { ReleaseCache(state); } catch (...) { cacheFailed = true; }
                    --cacheActions; Release();
                }); } catch (...) { cacheFailed = true; }
            if (!queued) {
                cacheFailed = true; --cacheActions; Release();
                AcquireSRWLockExclusive(&treeLock); state->queued = false; ReleaseSRWLockExclusive(&treeLock);
            }
        }
    }
    bool DrainCache() {
        ULONGLONG started = GetTickCount64();
        do {
            FlushCache(); size_t pending = 0;
            AcquireSRWLockShared(&treeLock);
            for (auto& item : cacheThreads) pending += item.second->pending.size();
            ReleaseSRWLockShared(&treeLock);
            if (!pending && !cacheActions.load()) {
                log.Write(L"cache-drained\tcallbacks=%llu\tobserved=%llu\tfailed=%u\r\n",
                    cacheCallbacks, cacheSequence, cacheFailed.load() ? 1U : 0U);
                return !cacheFailed;
            }
            Sleep(25);
        } while (GetTickCount64() - started < 5000);
        log.Write(L"cache-drain-timeout\r\n"); return false;
    }
    void Select(InstanceHandle handle) {
        bool frame = false; InstanceHandle root = 0, cursor = handle;
        for (unsigned depth = 0; cursor && depth < 32; ++depth) {
            auto found = nodes.find(cursor); if (found == nodes.end()) return;
            if (found->second.type == L"SystemTray.SystemTrayFrame") frame = true;
            if (found->second.type == L"Windows.UI.Xaml.Hosting.DesktopWindowXamlSource") {
                root = cursor; break;
            }
            cursor = found->second.parent;
        }
        if (!frame || !root) return;
        auto rootObject = nodes[root].weak.get();
        if (!rootObject) return;
        auto native = rootObject.as<IDesktopWindowXamlSourceNative>();
        HWND bridge = nullptr;
        winrt::check_hresult(native->get_WindowHandle(&bridge));
        if (GetAncestor(bridge, GA_ROOT) != shell) return;
        auto element = Inspect(handle).as<FrameworkElement>();
        candidate.bridge = bridge;
        candidate.target = winrt::make_weak(element);
        candidate.dispatcher = element.Dispatcher();
        candidate.handle = handle; ++candidate.generation;
        log.Write(L"selected\telement=%llu\tshell=0x%p\tgeneration=%llu\ttid=%lu\r\n",
            handle, shell, candidate.generation, GetCurrentThreadId());
    }
    // No property write happens in a visual-tree callback or while holding treeLock.
    bool Run(const Candidate* replacement, bool clear) {
        if (!active) return true;
        ULONGLONG started = GetTickCount64();
        for (;;) {
        ResetEvent(actionDone); actionResult = E_PENDING; actionPending = true;
        Candidate next; if (replacement) next = *replacement;
        bool replace = replacement != nullptr;
        AddRef();
        try {
            active.RunAsync(CoreDispatcherPriority::Normal, [this, next, replace, clear] {
                HRESULT result = S_OK;
                try {
                    if (clear) {
                        result = lease.Restore(log);
                        if (SUCCEEDED(result)) lease.Clear();
                    } else {
                        if (replace) {
                            lease.target = next.target; lease.shell = shell; lease.clock = clock;
                            lease.bridge = next.bridge;
                            lease.tray = FindWindowExW(shell, nullptr, L"TrayNotifyWnd", nullptr);
                        }
                        result = lease.ChangeEdge(wui_get_edge(shell), log);
                        if (SUCCEEDED(result))
                            result = lease.Update(stopping ? 0 : wui_measure_slot(shell, clock), log);
                    }
                } catch (...) { result = winrt::to_hresult(); }
                actionResult = result; SetEvent(actionDone); Release();
            });
        } catch (...) {
            actionPending = false; Release(); log.Write(L"dispatcher-error\thr=0x%08X\r\n",
                static_cast<unsigned>(winrt::to_hresult())); return false;
        }
        DWORD wait = WaitForSingleObject(actionDone, 5000);
        actionPending = wait != WAIT_OBJECT_0;
        if (wait == WAIT_OBJECT_0 && actionResult.load() == E_PENDING && GetTickCount64() - started < 10000) {
            Sleep(50); continue;
        }
        if (wait != WAIT_OBJECT_0 || FAILED(actionResult.load())) {
            log.Write(L"action-failed\twait=%lu\thr=0x%08X\r\n", wait,
                static_cast<unsigned>(actionResult.load())); return false;
        }
        return true;
        }
    }
    bool StopCallback(bool restored) {
        // Restoration and subscription ownership are independent. Even an
        // unresponsive lease must stop reporting new diagnostics-cache objects.
        log.Write(L"unadvise-start\r\n");
        HRESULT result = service->UnadviseVisualTreeChange(this);
        log.Write(L"unadvise-end\thr=0x%08X\r\n", static_cast<unsigned>(result));
        if (FAILED(result)) {
            log.Write(L"cleanup-maintenance\tcallback-still-subscribed\r\n");
            SetEvent(failed);
            ULONGLONG retryAt = GetTickCount64() + 5000;
            do {
                // No lease writes or new connection attempts occur here. Keep the
                // shadow worker alive to return references while removal is pending.
                FlushCache(); Sleep(250);
                if (GetTickCount64() >= retryAt) {
                    result = service->UnadviseVisualTreeChange(this);
                    log.Write(L"unadvise-retry\thr=0x%08X\r\n", static_cast<unsigned>(result));
                    retryAt = GetTickCount64() + 5000;
                }
            } while (FAILED(result));
        }
        bool drained = DrainCache();
        return restored && drained;
    }
    static DWORD WINAPI Worker(void* parameter) {
        auto self = static_cast<Taskbar*>(parameter);
        HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HMODULE owned = self->module;
        self->log.Write(L"advise-start\ttid=%lu\r\n", GetCurrentThreadId());
        HRESULT advise = self->service->AdviseVisualTreeChange(self);
        self->log.Write(L"advise-end\thr=0x%08X\r\n", static_cast<unsigned>(advise));
        bool safe = true; unsigned long long generation = 0;
        if (SUCCEEDED(advise)) {
            SetEvent(self->ready);
            while (WaitForSingleObject(self->stop, 250) == WAIT_TIMEOUT &&
                wui_check_windows(self->shell, self->clock)) {
                self->FlushCache();
                if (self->cacheFailed) break;
                Candidate next;
                AcquireSRWLockShared(&self->treeLock); next = self->candidate;
                ReleaseSRWLockShared(&self->treeLock);
                if (next.generation != generation) {
                    if (!self->Run(nullptr, true)) { safe = false; break; }
                    self->active = next.dispatcher; generation = next.generation;
                    if (next.target && !self->Run(&next, false)) { safe = false; break; }
                } else if (!self->Run(nullptr, false)) { safe = false; break; }
            }
        }
        self->stopping = true;
        // Drain a previously queued action before reusing its completion event.
        // A completed property error still needs restoration: a write may already
        // have succeeded. An unresponsive dispatcher keeps the shadow references.
        if (self->actionPending) {
            safe = WaitForSingleObject(self->actionDone, 5000) == WAIT_OBJECT_0;
            if (safe) self->actionPending = false;
        } else safe = true;
        if (safe) safe = self->Run(nullptr, true);
        // Attempt removal even when Advise or restoration failed. A failed
        // Unadvise keeps this shadow worker maintaining the still-active cache.
        safe = self->StopCallback(safe);
        if (!safe) {
            self->log.Write(L"cleanup-incomplete\tshadow-references-retained\r\n");
            SetEvent(self->failed);
            if (SUCCEEDED(apartment)) CoUninitialize();
            return 1;
        }
        AcquireSRWLockExclusive(&self->treeLock);
        self->candidate = {}; self->nodes.clear(); self->active = nullptr;
        self->cacheThreads.clear();
        self->cacheHooks->Release(); self->cacheHooks = nullptr;
        self->service->Release(); self->service = nullptr;
        self->diagnostics->Release(); self->diagnostics = nullptr;
        ReleaseSRWLockExclusive(&self->treeLock);
        self->log.Write(L"detached\tadvise=0x%08X\r\n", static_cast<unsigned>(advise));
        HANDLE completed = SUCCEEDED(advise) ? self->done : self->failed;
        if (SUCCEEDED(advise)) self->done = nullptr; else self->failed = nullptr;
        self->Release();
        SetEvent(completed); CloseHandle(completed);
        if (SUCCEEDED(apartment)) CoUninitialize();
        FreeLibraryAndExitThread(owned, SUCCEEDED(advise) ? 0 : 1);
    }
public:
    Taskbar() { ++g_objects; }
    ~Taskbar() {
        if (cacheHooks) cacheHooks->Release();
        if (service) service->Release(); if (diagnostics) diagnostics->Release();
        for (HANDLE handle : {stop, done, failed, ready, actionDone}) if (handle) CloseHandle(handle);
        --g_objects;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id == IID_IUnknown || id == IID_IObjectWithSite) *out = static_cast<IObjectWithSite*>(this);
        else if (id == __uuidof(IVisualTreeServiceCallback) || id == __uuidof(IVisualTreeServiceCallback2))
            *out = static_cast<IVisualTreeServiceCallback2*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count = --refs; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        return diagnostics ? diagnostics->QueryInterface(id, out) : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        if (!site) { if (stop) SetEvent(stop); return S_OK; }
        try {
            if (service) return E_UNEXPECTED;
            winrt::check_hresult(site->QueryInterface(__uuidof(IXamlDiagnostics), reinterpret_cast<void**>(&diagnostics)));
            winrt::check_hresult(site->QueryInterface(__uuidof(IVisualTreeService), reinterpret_cast<void**>(&service)));
            // The exact shadow owns its immutable configuration. The verified UWP
            // diagnostics implementation returned empty initialization data.
            std::wstring data;
            if (!wui_take_config(data)) return E_UNEXPECTED;
            std::vector<std::wstring> parts; size_t begin = 0, end;
            while ((end = data.find(L'|', begin)) != std::wstring::npos) {
                parts.push_back(data.substr(begin, end - begin)); begin = end + 1;
            }
            parts.push_back(data.substr(begin));
            if (parts.size() != 7 || parts[0] != L"1") return E_INVALIDARG;
            shell = reinterpret_cast<HWND>(_wcstoui64(parts[1].c_str(), nullptr, 10));
            clock = reinterpret_cast<HWND>(_wcstoui64(parts[2].c_str(), nullptr, 10));
            wchar_t type[64] = {};
            GetClassNameW(shell, type, _countof(type));
            if (!wui_check_windows(shell, clock) || (wcscmp(type, L"Shell_TrayWnd") &&
                wcscmp(type, L"Shell_SecondaryTrayWnd"))) return E_INVALIDARG;
            stop = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, parts[3].c_str());
            done = OpenEventW(EVENT_MODIFY_STATE, FALSE, parts[4].c_str());
            failed = OpenEventW(EVENT_MODIFY_STATE, FALSE, parts[5].c_str());
            ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (parts[3] + L"-ready").c_str());
            actionDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!stop || !done || !failed || !ready || !actionDone) return E_FAIL;
            log.Open(parts[6].c_str());
            HRESULT capability = diagnostics->QueryInterface(kCacheHooks, reinterpret_cast<void**>(&cacheHooks));
            log.Write(L"cache-capability\thr=0x%08X\ttid=%lu\r\n", static_cast<unsigned>(capability), GetCurrentThreadId());
            if (FAILED(capability)) { SetEvent(failed); return capability; }
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&Worker), &module)) return HRESULT_FROM_WIN32(GetLastError());
            log.Write(L"site\tpid=%lu\tshell=0x%p\tclock=0x%p\ttid=%lu\r\n",
                GetCurrentProcessId(), shell, clock, GetCurrentThreadId());
            AddRef(); HANDLE thread = CreateThread(nullptr, 0, Worker, this, 0, nullptr);
            if (!thread) { Release(); FreeLibrary(module); module = nullptr; return E_FAIL; }
            CloseHandle(thread); return S_OK;
        } catch (...) { return winrt::to_hresult(); }
    }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element,
        VisualMutationType mutation) override {
        AcquireSRWLockExclusive(&treeLock);
        try {
            ++cacheCallbacks;
            auto weak = Observe(element.Handle);
            if (mutation == VisualMutationType::Add) Observe(relation.Parent);
            if (!stopping) {
                if (mutation == VisualMutationType::Add) {
                    nodes[element.Handle] = {relation.Parent, element.Type ? element.Type : L"", weak};
                    if (element.Name && wcscmp(element.Name, L"SystemTrayFrameGrid") == 0 &&
                        element.Type && wcscmp(element.Type, L"Windows.UI.Xaml.Controls.StackPanel") == 0)
                        Select(element.Handle);
                } else {
                    nodes.erase(element.Handle);
                    if (candidate.handle == element.Handle) {
                        candidate.target = {}; candidate.dispatcher = nullptr;
                        candidate.handle = 0; ++candidate.generation;
                    }
                }
            }
        } catch (...) { log.Write(L"selection-error\thr=0x%08X\r\n", static_cast<unsigned>(winrt::to_hresult())); }
        ReleaseSRWLockExclusive(&treeLock); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override { return S_OK; }
};


class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory() { ++g_objects; } ~Factory() { --g_objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG count = --refs; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new(std::nothrow) Taskbar; if (!object) return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(id, out); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++g_objects; else --g_objects; return S_OK; }
};

// Original-module coordinator: one session at a time, including restoration.
struct Request { HWND shell = nullptr, clock = nullptr; unsigned long long epoch = 0; };
SRWLOCK g_requestLock = SRWLOCK_INIT;
Request g_request;
HANDLE g_wake = nullptr;
bool g_running = false;
unsigned long long g_failedEpoch = 0;

struct Session {
    Log control;
    HANDLE stop = nullptr, done = nullptr, failed = nullptr, ready = nullptr;
    HMODULE shadow = nullptr;
    bool activationStarted = false, completed = false;
    std::wstring path;
    ~Session() {
        for (HANDLE handle : {stop, done, failed, ready}) if (handle) CloseHandle(handle);
        // Initialize can return before factory activation. Keep this exact image's
        // configuration alive until completion; a timeout retains only the shadow.
        if (shadow && (!activationStarted || completed)) FreeLibrary(shadow);
        else if (shadow) control.Write(L"shadow-retained\tactivation-or-cleanup-unconfirmed\r\n");
        // Only this session's exact copy is eligible for cleanup. Diagnostics may
        // retain it until Explorer exits; never force an unload or sweep user files.
        if (!path.empty()) DeleteFileW(path.c_str());
    }
};
using Configure = HRESULT(WINAPI*)(LPCWSTR);
using Initializer = HRESULT(WINAPI*)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, CLSID, LPCWSTR);
HRESULT wui_begin_session(HMODULE original, const Request& request, Session& session) {
    wchar_t originalPath[32768], temp[32768], guidText[64], system[32768]; GUID guid{};
    DWORD length = GetModuleFileNameW(original, originalPath, _countof(originalPath));
    if (!length || length >= _countof(originalPath)) return E_FAIL;
    length = GetTempPathW(_countof(temp), temp);
    if (!length || length >= _countof(temp)) return E_FAIL;
    winrt::check_hresult(CoCreateGuid(&guid)); StringFromGUID2(guid, guidText, _countof(guidText));
    std::wstring stem = std::wstring(L"TClockTaskbar-") + guidText;
    session.control.Open((std::wstring(temp) + stem + L".control.log").c_str());
    session.control.Write(L"coordinator-start\tpid=%lu\ttid=%lu\r\n", GetCurrentProcessId(), GetCurrentThreadId());
    session.path = std::wstring(temp) + stem + L".dll";
    if (!CopyFileW(originalPath, session.path.c_str(), TRUE)) { session.path.clear(); return HRESULT_FROM_WIN32(GetLastError()); }
    std::wstring stopName = L"Local\\" + stem + L"-stop";
    std::wstring doneName = L"Local\\" + stem + L"-done";
    std::wstring failName = L"Local\\" + stem + L"-failed";
    session.stop = CreateEventW(nullptr, TRUE, FALSE, stopName.c_str());
    session.done = CreateEventW(nullptr, TRUE, FALSE, doneName.c_str());
    session.failed = CreateEventW(nullptr, TRUE, FALSE, failName.c_str());
    session.ready = CreateEventW(nullptr, TRUE, FALSE, (stopName + L"-ready").c_str());
    if (!session.stop || !session.done || !session.failed || !session.ready) return E_FAIL;
    std::wstring logPath = std::wstring(temp) + stem + L".log";
    std::wstring data = L"1|" + std::to_wstring(reinterpret_cast<UINT_PTR>(request.shell)) + L"|" +
        std::to_wstring(reinterpret_cast<UINT_PTR>(request.clock)) + L"|" + stopName + L"|" + doneName + L"|" + failName + L"|" + logPath;
    session.shadow = LoadLibraryExW(session.path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!session.shadow) return HRESULT_FROM_WIN32(GetLastError());
    auto configure = reinterpret_cast<Configure>(GetProcAddress(session.shadow, "WuiConfigureTaskbar"));
    HRESULT configured = configure ? configure(data.c_str()) : E_NOINTERFACE;
    session.control.Write(L"shadow-configured\thr=0x%08X\r\n", static_cast<unsigned>(configured));
    if (FAILED(configured)) return configured;
    length = GetSystemDirectoryW(system, _countof(system));
    if (!length || length >= _countof(system)) return E_FAIL;
    std::wstring runtimePath = std::wstring(system) + L"\\Windows.UI.Xaml.dll";
    HMODULE runtime = LoadLibraryExW(runtimePath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!runtime) return HRESULT_FROM_WIN32(GetLastError());
    auto initialize = reinterpret_cast<Initializer>(GetProcAddress(runtime, "InitializeXamlDiagnosticsEx"));
    HRESULT result = E_NOINTERFACE;
    if (initialize) {
        // XAML diagnostics endpoints are per process. The public UWP endpoint is
        // named VisualDiagConnection1 on the verified native Windows taskbar.
        session.activationStarted = true;
        session.control.Write(L"initialize-start\r\n");
        result = initialize(L"VisualDiagConnection1", GetCurrentProcessId(), runtimePath.c_str(),
            session.path.c_str(), kTaskbar, data.c_str());
        session.control.Write(L"initialize-end\thr=0x%08X\r\n", static_cast<unsigned>(result));
    }
    FreeLibrary(runtime);
    OutputDebugStringW((L"[TClock taskbar] diagnostics log: " + logPath + L"\r\n").c_str());
    return result;
}
DWORD WINAPI wui_run_coordinator(void* parameter) {
    HMODULE module = static_cast<HMODULE>(parameter);
    HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Request request;
        AcquireSRWLockExclusive(&g_requestLock);
        request = g_request;
        if (!request.clock || request.epoch == g_failedEpoch || !wui_check_windows(request.shell, request.clock)) {
            g_running = false; HANDLE wake = g_wake; g_wake = nullptr;
            ReleaseSRWLockExclusive(&g_requestLock);
            if (wake) CloseHandle(wake); break;
        }
        ReleaseSRWLockExclusive(&g_requestLock);
        Session session; HRESULT result = E_FAIL;
        try { result = wui_begin_session(module, request, session); }
        catch (...) { result = winrt::to_hresult(); }
        bool cleanup = false;
        if (SUCCEEDED(result)) {
            ULONGLONG stopAt = 0, startedAt = GetTickCount64();
            bool ready = false;
            for (;;) {
                HANDLE events[] = {session.done, session.failed, g_wake};
                DWORD wait = WaitForMultipleObjects(_countof(events), events, FALSE, 250);
                if (wait == WAIT_OBJECT_0) { cleanup = true; session.completed = true; break; }
                if (wait == WAIT_OBJECT_0 + 1) { session.completed = true; break; }
                if (wait == WAIT_FAILED) break;
                if (!ready && WaitForSingleObject(session.ready, 0) == WAIT_OBJECT_0) {
                    ready = true; session.control.Write(L"shadow-ready\r\n");
                }
                AcquireSRWLockShared(&g_requestLock);
                bool changed = g_request.epoch != request.epoch;
                ReleaseSRWLockShared(&g_requestLock);
                bool startupTimeout = !ready && GetTickCount64() - startedAt > 10000;
                if (changed || startupTimeout || !wui_check_windows(request.shell, request.clock)) {
                    SetEvent(session.stop);
                    if (!stopAt) {
                        stopAt = GetTickCount64();
                        session.control.Write(startupTimeout ? L"startup-timeout\r\n" : L"stop-requested\r\n");
                    }
                }
                if (stopAt && GetTickCount64() - stopAt > 15000) break;
            }
        } else if (session.stop) SetEvent(session.stop);
        if (!cleanup) {
            OutputDebugStringW(L"[TClock taskbar] diagnostics unavailable or detach incomplete; session disabled.\r\n");
            AcquireSRWLockExclusive(&g_requestLock); g_failedEpoch = g_request.epoch;
            ReleaseSRWLockExclusive(&g_requestLock);
        }
        // The shadow contains all callbacks and queued UI actions. Once the
        // initializer returns, no pending diagnostics operation calls this copy.
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    FreeLibraryAndExitThread(module, 0);
}
} // namespace

void wui_reserve_taskbar(HWND taskbar, HWND clock) {
    if (!wui_check_windows(taskbar, clock)) return;
    AcquireSRWLockExclusive(&g_requestLock);
    bool changed = g_request.shell != taskbar || g_request.clock != clock;
    if (changed) {
        g_request.shell = taskbar; g_request.clock = clock; ++g_request.epoch;
    }
    if (!g_running && g_request.epoch != g_failedEpoch && wui_measure_slot(taskbar, clock) > 0) {
        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&wui_run_coordinator), &module)) {
            g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            HANDLE thread = g_wake ? CreateThread(nullptr, 0, wui_run_coordinator, module, 0, nullptr) : nullptr;
            if (thread) { g_running = true; CloseHandle(thread); }
            else { if (g_wake) CloseHandle(g_wake); g_wake = nullptr; FreeLibrary(module); }
        }
    }
    if (changed && g_wake) SetEvent(g_wake);
    ReleaseSRWLockExclusive(&g_requestLock);
}
void wui_release_taskbar(void) {
    AcquireSRWLockExclusive(&g_requestLock);
    g_request.shell = nullptr; g_request.clock = nullptr; ++g_request.epoch;
    if (g_wake) SetEvent(g_wake);
    ReleaseSRWLockExclusive(&g_requestLock);
}
// Only the coordinator's explicitly loaded, uniquely named shadow calls this.
// The string is copied before publication; no original-module pointer is retained.
extern "C" HRESULT WINAPI WuiConfigureTaskbar(LPCWSTR data) {
    if (!data || !*data || wcslen(data) > 65535) return E_INVALIDARG;
    try {
        std::wstring copy(data);
        AcquireSRWLockExclusive(&g_configLock);
        if (g_configured || g_configClaimed) {
            ReleaseSRWLockExclusive(&g_configLock); return E_UNEXPECTED;
        }
        g_sessionData.swap(copy); g_configured = true;
        ReleaseSRWLockExclusive(&g_configLock);
        return S_OK;
    } catch (...) { return winrt::to_hresult(); }
}
STDAPI DllGetClassObject(REFCLSID id, REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (id != kTaskbar) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new(std::nothrow) Factory; if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
STDAPI DllCanUnloadNow(void) { return g_objects.load() ? S_FALSE : S_OK; }
