#include "internal.hpp"

#include <algorithm>
#include <limits>
#include <windows.h>

namespace stronghold {
std::int64_t monotonic_100ns() {
    LARGE_INTEGER ticks{}, frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        throw Error("clock", "Cannot read the QPC clock.");
    const long double value = static_cast<long double>(ticks.QuadPart) * 10000000.0L / frequency.QuadPart;
    if (value < 0 || value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        throw Error("clock", "QPC value cannot be represented in 100 ns units.");
    return static_cast<std::int64_t>(value);
}
double frame_age_ms(const Frame& frame, std::int64_t now_100ns) {
    if (frame.source_time_100ns == 0) return -1.0;
    if (frame.source_time_100ns < 0 || frame.source_time_100ns > now_100ns)
        throw Error("capture_invalid_timestamp", "Source timestamp is invalid or ahead of the QPC clock.");
    return static_cast<double>(now_100ns - frame.source_time_100ns) / 10000.0;
}

void CaptureGate::begin(std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (busy_) throw AdmissionError("capture_busy", "A capture is already in progress; no request was queued.", std::chrono::milliseconds(1000));
    if (now < next_start_) {
        const auto retry = std::chrono::ceil<std::chrono::milliseconds>(next_start_ - now);
        throw AdmissionError("capture_throttled", "The minimum capture start interval has not elapsed; no request was queued.", retry);
    }
    busy_ = true; next_start_ = now + std::chrono::milliseconds(500);
}
void CaptureGate::finish() { std::lock_guard lock(mutex_); busy_ = false; }
std::size_t pixel_bytes(std::int32_t width, std::int32_t height) {
    // Bound allocations and IPC payloads before multiplication or conversion to Windows UINT.
    constexpr std::size_t max_bytes = 512ULL * 1024 * 1024;
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height) > max_bytes / 3) {
        throw Error("capture_invalid", "Invalid or oversized image dimensions (maximum RGB payload: 512 MiB).");
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3;
}

std::vector<std::string> validate_frame(const Frame& frame, const Window& window) {
    const auto bytes = pixel_bytes(frame.width, frame.height);
    if (frame.rgb.size() != bytes) throw Error("capture_invalid", "Image data is not packed RGB8.");
    if (frame.width != window.width || frame.height != window.height)
        throw Error("capture_size_mismatch", "Image dimensions do not match the physical client area.");
    if (std::all_of(frame.rgb.begin(), frame.rgb.end(), [](auto p) { return p == 0; }))
        throw Error("capture_black", "Image is entirely black.");
    // A dark scene can be valid: this is a quality warning, never an OCR/game-state verdict.
    const auto dark = std::count_if(frame.rgb.begin(), frame.rgb.end(), [](auto p) { return p <= 8; });
    if (static_cast<double>(dark) / static_cast<double>(bytes) >= 0.99)
        return {"near_black_image"};
    return {};
}

Frame frame_from_bgr(std::int32_t width, std::int32_t height, int channels, std::span<const std::uint8_t> data) {
    const auto bytes = pixel_bytes(width, height);
    if ((channels != 3 && channels != 4) || data.size() != bytes / 3 * channels)
        throw Error("capture_invalid", "Invalid BGR/BGRA pixel layout.");
    Frame frame{width, height, std::vector<std::uint8_t>(bytes)};
    for (std::size_t pixel = 0; pixel < bytes / 3; ++pixel) {
        frame.rgb[pixel * 3] = data[pixel * channels + 2];
        frame.rgb[pixel * 3 + 1] = data[pixel * channels + 1];
        frame.rgb[pixel * 3 + 2] = data[pixel * channels];
    }
    return frame;
}

Frame frame_from_bgra_rows(std::int32_t width, std::int32_t height, std::span<const std::uint8_t> pixels,
                          std::uint32_t row_pitch, std::int32_t crop_x, std::int32_t crop_y) {
    const auto bytes = pixel_bytes(width, height);
    if (crop_x < 0 || crop_y < 0 ||
        (static_cast<std::uint64_t>(crop_x) + width) * 4 > row_pitch ||
        (static_cast<std::uint64_t>(crop_y) + height - 1) * row_pitch +
            (static_cast<std::uint64_t>(crop_x) + width) * 4 > pixels.size())
        throw Error("capture_invalid", "BGRA row pitch or crop exceeds the mapped pixel data.");
    Frame frame{width, height, std::vector<std::uint8_t>(bytes)};
    for (std::int32_t y = 0; y < height; ++y) {
        const auto* source = pixels.data() + (static_cast<std::size_t>(crop_y) + y) * row_pitch + static_cast<std::size_t>(crop_x) * 4;
        auto* dest = frame.rgb.data() + static_cast<std::size_t>(y) * width * 3;
        for (std::int32_t x = 0; x < width; ++x) {
            dest[x * 3] = source[x * 4 + 2]; dest[x * 3 + 1] = source[x * 4 + 1]; dest[x * 3 + 2] = source[x * 4];
        }
    }
    return frame;
}

Capture capture_once(const Window& window, const std::string& method, const Verify& verify, const Acquire& acquire) {
    if (method != "auto" && method != "wgc") throw Error("invalid_argument", "Only the native wgc backend is supported.");
    Capture result;
    result.window = window;
    result.backend_version = "stronghold-wgc-v1";
    result.requested_at = utc_now();
    const auto started = std::chrono::steady_clock::now();
    try {
        verify(window);
        auto frame = acquire(window, "wgc");
        verify(window);
        result.warnings = validate_frame(frame, window);
        result.frame = std::move(frame);
        result.method = "wgc";
        if (!window.identity_verified) result.warnings.push_back("limited_window_identity");
        result.warnings.push_back("visibility_checked_from_window_attributes");
        result.warnings.push_back("game_content_not_verified");
        result.completed_at = utc_now();
        result.duration_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        result.attempts.push_back({"wgc", "succeeded", "", "", result.duration_ms});
        return result;
    } catch (const Error& e) {
        result.attempts.push_back({"wgc", "failed", e.code, e.what(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()});
        throw CaptureError(e, std::move(result.attempts));
    }
}

SavedCapture capture_and_save(const Window& window, const fs::path& output, const fs::path& executable) {
    static CaptureGate gate;
    gate.begin(std::chrono::steady_clock::now());
    struct Release { CaptureGate& gate; ~Release() { gate.finish(); } } release{gate};
    auto capture = capture_window(window, "wgc", executable);
    return save_capture(capture, output, verify_window);
}
} // namespace stronghold
