#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include <mutex>

namespace stronghold {
namespace fs = std::filesystem;

struct Error : std::runtime_error {
    std::string code;
    Error(std::string code, std::string message) : std::runtime_error(std::move(message)), code(std::move(code)) {}
};

struct Window {
    std::uint64_t hwnd = 0;
    std::uint32_t pid = 0;
    std::uint64_t process_created = 0; // Windows FILETIME ticks.
    std::string executable, title, class_name; // UTF-8.
    std::int32_t left = 0, top = 0, width = 0, height = 0;
    bool visible = false, minimized = false, foreground = false, partly_offscreen = false;
    bool identity_verified = false, cloaked = false;
    std::string opacity = "unknown", visibility = "unknown";
    std::uint64_t occluding_hwnd = 0;
    std::string identity_warning;
};

bool same_source(const Window& a, const Window& b);
bool is_game_window(const Window& window);
void initialize_dpi();
Window inspect_window(std::uint64_t hwnd);
std::vector<Window> find_game_windows();
Window select_window(std::optional<std::uint64_t> hwnd);
Window wait_foreground(Window window, std::chrono::milliseconds timeout = std::chrono::seconds(5));
void require_usable(const Window& window);
void verify_window(const Window& expected);

struct Frame {
    std::int32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgb; // Packed RGB8; no alpha, padding, resize or content crop.
    std::int64_t source_time_100ns = 0; // WGC SystemRelativeTime; QPC epoch, 100 ns units.
    std::uint64_t worker_peak_working_set_bytes = 0;
};

std::int64_t monotonic_100ns();
double frame_age_ms(const Frame& frame, std::int64_t now_100ns);

struct Attempt {
    std::string method, status, code, message;
    double duration_ms = 0;
};

struct CaptureError : Error {
    std::vector<Attempt> attempts;
    CaptureError(const Error& error, std::vector<Attempt> attempts)
        : Error(error), attempts(std::move(attempts)) {}
};

struct Capture {
    Window window;
    Frame frame;
    std::string method, backend_version;
    std::string requested_at, completed_at; // UTC ISO-8601; not source-frame timestamps.
    double duration_ms = 0;
    std::vector<Attempt> attempts;
    std::vector<std::string> warnings;
};

using Verify = std::function<void(const Window&)>;
using Acquire = std::function<Frame(const Window&, const std::string&)>;
Capture capture_once(const Window& window, const std::string& method, const Verify& verify, const Acquire& acquire);
// Isolated native WGC entry point.
Capture capture_window(const Window& window, const std::string& method,
                       const fs::path& worker_executable);
std::vector<std::string> validate_frame(const Frame& frame, const Window& window);
std::size_t pixel_bytes(std::int32_t width, std::int32_t height);
Frame frame_from_bgr(std::int32_t width, std::int32_t height, int channels, std::span<const std::uint8_t> data);

struct SavedCapture {
    std::string id; fs::path directory;
    std::string freshness = "unknown";
    double delivery_age_ms = -1;
};
// before_publish is a production window recheck; offline callers may omit it.
SavedCapture save_capture(const Capture& capture, const fs::path& output, const Verify& before_publish = {});

struct AdmissionError : Error {
    std::chrono::milliseconds retry_after;
    AdmissionError(std::string code, std::string message, std::chrono::milliseconds retry)
        : Error(std::move(code), std::move(message)), retry_after(retry) {}
};

// Share one gate across all acquisition callers in the application. Does not queue requests.
class CaptureGate {
public:
    void begin(std::chrono::steady_clock::time_point now);
    void finish();
private:
    std::mutex mutex_;
    bool busy_ = false;
    std::chrono::steady_clock::time_point next_start_{};
};
SavedCapture capture_and_save(const Window& window, const fs::path& output, const fs::path& worker_executable);
// Stable metadata preview: archive age/performance have not been measured here.
std::string capture_json(const Capture& capture, const std::string& id);
std::string quote_json(const std::string& text);
std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
std::string utc_now();
std::string unique_id();

} // namespace stronghold
