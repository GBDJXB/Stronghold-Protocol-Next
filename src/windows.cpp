#include "internal.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <iterator>
#include <limits>
#include <set>
#include <thread>

namespace stronghold {
namespace {
std::string win32_message(const char* op, DWORD code = GetLastError()) {
    return std::string(op) + " (Win32 error " + std::to_string(code) + ')';
}
bool contains_ci(std::wstring value, std::wstring needle) {
    std::transform(value.begin(), value.end(), value.begin(), towlower);
    std::transform(needle.begin(), needle.end(), needle.begin(), towlower);
    return value.find(needle) != std::wstring::npos;
}
std::wstring path_stem(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/"), dot = path.find_last_of(L'.');
    const auto begin = slash == std::wstring::npos ? 0 : slash + 1;
    return path.substr(begin, dot == std::wstring::npos || dot < begin ? path.size() - begin : dot - begin);
}
bool excluded(const std::wstring& executable, const std::wstring& cls) {
    const auto stem = path_stem(executable);
    return contains_ci(stem, L"launcher") || stem.find(L"启动器") != std::wstring::npos ||
           contains_ci(stem, L"chrome") || contains_ci(cls, L"Chrome_WidgetWin");
}
bool game_title(const std::wstring& title) {
    return title.find(L"明日方舟") != std::wstring::npos || contains_ci(title, L"arknights");
}
ScreenRect screen_rect(const RECT& r) { return {r.left, r.top, r.right, r.bottom}; }
bool is_cloaked(HWND hwnd) {
    DWORD value = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &value, sizeof(value))))
        throw Error("window_visibility_unknown", "Cannot query DWM window visibility.");
    return value != 0;
}
std::string window_opacity(HWND hwnd) {
    SetLastError(0);
    const auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!ex && GetLastError()) return "unknown";
    if (!(ex & WS_EX_LAYERED)) return "opaque";
    COLORREF key{}; BYTE alpha{}; DWORD flags{};
    if (!GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags)) return "unknown";
    if ((flags & LWA_ALPHA) && alpha < 255) return "non_opaque";
    if (flags & LWA_COLORKEY) return "unknown";
    return flags & LWA_ALPHA ? "opaque" : "unknown";
}
bool entirely_transparent(HWND hwnd) {
    if (!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED)) return false;
    COLORREF key{}; BYTE alpha{}; DWORD flags{};
    return GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) && (flags & LWA_ALPHA) && alpha == 0;
}
BOOL CALLBACK monitor_rects(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    reinterpret_cast<std::vector<ScreenRect>*>(data)->push_back(screen_rect(info.rcMonitor));
    return TRUE;
}
void inspect_visibility(HWND target, Window& w) {
    w.cloaked = is_cloaked(target); w.opacity = window_opacity(target);
    const auto right = static_cast<std::int64_t>(w.left) + w.width;
    const auto bottom = static_cast<std::int64_t>(w.top) + w.height;
    if (right > std::numeric_limits<std::int32_t>::max() || bottom > std::numeric_limits<std::int32_t>::max())
        throw Error("window_inspection_failed", "Client rectangle exceeds supported physical coordinates.");
    const ScreenRect client{w.left, w.top, w.left + w.width, w.top + w.height};
    std::vector<ScreenRect> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, monitor_rects, reinterpret_cast<LPARAM>(&monitors)))
        throw Error("window_visibility_unknown", "Cannot enumerate monitor regions.");
    w.partly_offscreen = !rectangle_covered(client, monitors);
    w.visibility = "clear";
    std::set<HWND> visited;
    bool found = false;
    for (HWND above = GetTopWindow(nullptr); above; above = GetWindow(above, GW_HWNDNEXT)) {
        if (!visited.insert(above).second || visited.size() > 4096) { w.visibility = "unknown"; return; }
        if (above == target) { found = true; break; }
        if (!IsWindowVisible(above) || IsIconic(above)) continue;
        RECT bounds{};
        // DWM bounds excludes shadows/invisible resize borders. Always test the target client.
        if (FAILED(DwmGetWindowAttribute(above, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
            // An unrelated window must not invalidate the target just because its
            // DWM attributes cannot be queried. A coarse disjoint rectangle suffices.
            if (GetWindowRect(above, &bounds) && !rectangles_overlap(client, screen_rect(bounds))) continue;
            w.visibility = "unknown"; return;
        }
        if (!rectangles_overlap(client, screen_rect(bounds))) continue;
        try { if (is_cloaked(above)) continue; }
        catch (const Error&) { w.visibility = "unknown"; return; }
        if (entirely_transparent(above)) continue;
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        if (!region) { w.visibility = "unknown"; return; }
        const int kind = GetWindowRgn(above, region);
        if (kind == NULLREGION) { DeleteObject(region); continue; }
        if (kind == SIMPLEREGION || kind == COMPLEXREGION) {
            RECT origin{};
            if (!GetWindowRect(above, &origin)) { DeleteObject(region); w.visibility = "unknown"; return; }
            OffsetRgn(region, origin.left, origin.top);
            const RECT overlap{std::max<LONG>(client.left, bounds.left), std::max<LONG>(client.top, bounds.top),
                               std::min<LONG>(client.right, bounds.right), std::min<LONG>(client.bottom, bounds.bottom)};
            const bool intersects = RectInRegion(region, &overlap) != FALSE;
            DeleteObject(region);
            if (!intersects) continue;
        } else DeleteObject(region); // No explicit region: conservative visible-frame rectangle.
        w.occluding_hwnd = reinterpret_cast<std::uint64_t>(above);
        w.visibility = window_opacity(above) == "unknown" ? "unknown" : "occluded";
        return;
    }
    if (!found) w.visibility = "unknown";
}
struct EnumContext {
    std::vector<Window> windows;
    std::vector<std::pair<std::string, std::string>> errors;
};
BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM data) {
    auto& context = *reinterpret_cast<EnumContext*>(data);
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[1024]{}, cls[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (!game_title(title)) return TRUE;
    if (!GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)))) {
        context.errors.emplace_back("window_inspection_failed", win32_message("GetClassNameW")); return TRUE;
    }
    if (excluded(L"", cls)) return TRUE;
    try {
        auto w = inspect_window(reinterpret_cast<std::uint64_t>(hwnd));
        if (is_game_window(w)) context.windows.push_back(std::move(w));
    } catch (const Error& e) {
        context.errors.emplace_back(e.code, "HWND " + std::to_string(reinterpret_cast<std::uint64_t>(hwnd)) + ": " + e.what());
    } catch (...) { context.errors.emplace_back("window_inspection_failed", "Candidate inspection failed."); }
    return TRUE;
}
} // namespace

bool rectangles_overlap(const ScreenRect& a, const ScreenRect& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}
bool rectangle_covered(const ScreenRect& target, std::span<const ScreenRect> monitors) {
    if (target.left >= target.right || target.top >= target.bottom) return false;
    std::vector<ScreenRect> remaining{target};
    for (const auto& m : monitors) {
        std::vector<ScreenRect> next;
        for (const auto& r : remaining) {
            if (!rectangles_overlap(r, m)) { next.push_back(r); continue; }
            const auto l = std::max(r.left, m.left), t = std::max(r.top, m.top);
            const auto rr = std::min(r.right, m.right), b = std::min(r.bottom, m.bottom);
            if (r.top < t) next.push_back({r.left, r.top, r.right, t});
            if (b < r.bottom) next.push_back({r.left, b, r.right, r.bottom});
            if (r.left < l) next.push_back({r.left, t, l, b});
            if (rr < r.right) next.push_back({rr, t, r.right, b});
        }
        remaining = std::move(next);
        if (remaining.empty()) return true;
    }
    return remaining.empty();
}
void initialize_dpi() {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        throw Error("dpi_unverified", "The current thread must use per-monitor-v2 DPI awareness.");
}
bool same_source(const Window& a, const Window& b) {
    if (a.identity_verified && !b.identity_verified) return false;
    return a.hwnd == b.hwnd && a.pid == b.pid && a.class_name == b.class_name &&
           (!a.identity_verified || (a.process_created == b.process_created && a.executable == b.executable)) &&
           a.left == b.left && a.top == b.top && a.width == b.width && a.height == b.height;
}
bool is_game_window(const Window& w) {
    try {
        const auto title = wide(w.title), cls = wide(w.class_name), exe = wide(w.executable);
        if (!game_title(title) || excluded(exe, cls)) return false;
        const auto stem = path_stem(exe);
        return cls == L"UnityWndClass" || _wcsicmp(stem.c_str(), L"arknights") == 0 || stem == L"明日方舟";
    } catch (...) { return false; }
}
Window inspect_window(std::uint64_t value) {
    const HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
    if (!IsWindow(hwnd)) throw Error("window_closed", "The window handle is no longer valid.");
    Window w; w.hwnd = value;
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid) || !pid)
        throw Error("window_process_unavailable", win32_message("GetWindowThreadProcessId"));
    w.pid = pid;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, w.pid);
    if (!process) {
        const auto code = GetLastError();
        if (code != ERROR_ACCESS_DENIED) throw Error("window_process_unavailable", win32_message("OpenProcess", code));
        w.identity_warning = win32_message("OpenProcess", code);
    } else {
        wchar_t path[32768]{}; DWORD size = static_cast<DWORD>(std::size(path));
        FILETIME created{}, exited{}, kernel{}, user{};
        const bool path_ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
        const DWORD pe = path_ok ? ERROR_SUCCESS : GetLastError();
        const bool times_ok = GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
        const DWORD te = times_ok ? ERROR_SUCCESS : GetLastError();
        CloseHandle(process);
        if (!path_ok && pe != ERROR_ACCESS_DENIED)
            throw Error("window_process_unavailable", win32_message("QueryFullProcessImageNameW", pe));
        if (!times_ok && te != ERROR_ACCESS_DENIED)
            throw Error("window_process_unavailable", win32_message("GetProcessTimes", te));
        if (path_ok && times_ok) {
            w.identity_verified = true; w.executable = utf8(std::wstring(path, size));
            w.process_created = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        } else w.identity_warning = !path_ok ? win32_message("QueryFullProcessImageNameW", pe) : win32_message("GetProcessTimes", te);
    }
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring title(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    title.resize(static_cast<std::size_t>(std::max(0, GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size())))));
    wchar_t cls[256]{};
    if (!GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))))
        throw Error("window_inspection_failed", win32_message("GetClassNameW"));
    w.title = utf8(title); w.class_name = utf8(cls);
    RECT client{}; POINT origin{};
    if (!GetClientRect(hwnd, &client) || !ClientToScreen(hwnd, &origin))
        throw Error("window_inspection_failed", win32_message("Client geometry"));
    w.left = origin.x; w.top = origin.y; w.width = client.right - client.left; w.height = client.bottom - client.top;
    if (w.width < 0 || w.height < 0) throw Error("window_inspection_failed", "Invalid client dimensions.");
    w.visible = IsWindowVisible(hwnd) != FALSE; w.minimized = IsIconic(hwnd) != FALSE;
    w.foreground = GetForegroundWindow() == hwnd;
    inspect_visibility(hwnd, w);
    DWORD after_pid = 0;
    if (!IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd, &after_pid) || after_pid != w.pid)
        throw Error("window_changed", "Window process changed during inspection.");
    return w;
}
std::vector<Window> find_game_windows() {
    initialize_dpi(); EnumContext context;
    if (!EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&context)))
        throw Error("window_enumeration_failed", win32_message("EnumWindows"));
    if (!context.errors.empty())
        throw Error(context.errors.front().first, "Automatic window selection could not inspect a candidate; use --hwnd to select explicitly. " + context.errors.front().second);
    std::sort(context.windows.begin(), context.windows.end(), [](const Window& a, const Window& b) { return a.hwnd < b.hwnd; });
    return context.windows;
}
Window select_window(std::optional<std::uint64_t> hwnd) {
    initialize_dpi();
    if (hwnd) {
        auto w = inspect_window(*hwnd);
        if (!is_game_window(w)) throw Error("not_game_window", "The selected window is not an Arknights game window.");
        return w;
    }
    auto windows = find_game_windows();
    if (windows.empty()) throw Error("no_window", "No visible Arknights game window was found.");
    if (windows.size() != 1) throw Error("ambiguous_window", "Multiple Arknights game windows found; use --hwnd.");
    return windows.front();
}
Window wait_foreground(Window expected, std::chrono::milliseconds timeout) {
    initialize_dpi();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        const auto current = inspect_window(expected.hwnd);
        if (!same_source(expected, current)) throw Error("window_changed", "Window source changed.");
        require_usable(current);
        if (current.foreground) return current;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) throw Error("focus_failed", "Window did not become foreground.");
        std::this_thread::sleep_until(std::min(deadline, now + std::chrono::seconds(1)));
    }
}
void require_usable(const Window& w) {
    if (!w.visible) throw Error("window_not_visible", "Game window is hidden.");
    if (w.minimized) throw Error("window_minimized", "Game window is minimized.");
    if (w.cloaked) throw Error("window_cloaked", "Game window is cloaked by DWM.");
    if (w.width <= 0 || w.height <= 0) throw Error("window_empty_client", "Game client area is empty.");
    if (w.partly_offscreen) throw Error("window_offscreen", "Client area is outside the actual monitor regions.");
    if (w.opacity == "non_opaque") throw Error("window_transparent", "Game window is not fully opaque.");
    if (w.opacity != "opaque") throw Error("window_opacity_unknown", "Cannot determine game window opacity.");
    if (w.visibility == "occluded") throw Error("window_occluded", "A visible window overlaps the game client area.");
    if (w.visibility != "clear") throw Error("window_visibility_unknown", "Cannot establish client visibility from window attributes.");
}
void verify_window(const Window& expected) {
    initialize_dpi(); const auto current = inspect_window(expected.hwnd);
    if (!same_source(expected, current) || !is_game_window(current))
        throw Error("window_changed", "Window identity or client geometry changed.");
    require_usable(current); // Focus is diagnostic only; no foreground requirement.
}
} // namespace stronghold
