#include "internal.hpp"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <thread>

namespace stronghold {
namespace {
int checks = 0;
void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) throw Error("test_failed", message);
}
template<class F> void expect_error(const std::string& code, F action) {
    try { action(); } catch (const Error& e) { expect(e.code == code, "Unexpected error classification."); return; }
    throw Error("test_failed", "Expected failure was accepted.");
}
Window fixture_window(int width = 160, int height = 90) {
    Window w;
    w.hwnd = 123; w.pid = 456; w.process_created = 133000000000000000;
    w.executable = "C:\\测试\\Arknights.exe"; w.title = "明日方舟"; w.class_name = "UnityWndClass";
    w.left = -5; w.top = 20; w.width = width; w.height = height;
    w.visible = true; w.foreground = false; w.partly_offscreen = false;
    w.identity_verified = true; w.opacity = "opaque"; w.visibility = "clear";
    return w;
}
Frame fixture_frame(const Window& w) {
    Frame f{w.width, w.height, std::vector<std::uint8_t>(pixel_bytes(w.width, w.height))};
    f.source_time_100ns = monotonic_100ns();
    for (std::size_t i = 0; i < f.rgb.size() / 3; ++i) {
        f.rgb[i * 3] = static_cast<std::uint8_t>(i % 251);
        f.rgb[i * 3 + 1] = static_cast<std::uint8_t>((i * 7) % 253);
        f.rgb[i * 3 + 2] = static_cast<std::uint8_t>((i * 13) % 255);
    }
    return f;
}
void check_hr(HRESULT hr) { if (FAILED(hr)) throw Error("test_failed", "WIC PNG decode failed."); }
void check_png(const fs::path& png, const Frame& expected) {
    using Microsoft::WRL::ComPtr;
    const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    check_hr(hr);
    struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
    ComPtr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check_hr(factory->CreateDecoderFromFilename(png.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame;
    check_hr(decoder->GetFrame(0, &frame));
    UINT width = 0, height = 0;
    check_hr(frame->GetSize(&width, &height));
    expect(width == static_cast<UINT>(expected.width) && height == static_cast<UINT>(expected.height), "PNG dimensions changed.");
    ComPtr<IWICFormatConverter> converter;
    check_hr(factory->CreateFormatConverter(&converter));
    check_hr(converter->Initialize(frame.Get(), GUID_WICPixelFormat24bppRGB, WICBitmapDitherTypeNone,
                                   nullptr, 0, WICBitmapPaletteTypeCustom));
    std::vector<std::uint8_t> decoded(expected.rgb.size());
    check_hr(converter->CopyPixels(nullptr, width * 3, static_cast<UINT>(decoded.size()), decoded.data()));
    expect(decoded == expected.rgb, "PNG colors or pixels changed.");
}
std::size_t count_directories(const fs::path& output) {
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(output)) if (entry.is_directory()) ++count;
    return count;
}
std::string read_text_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Error("test_failed", "Could not read test metadata file.");
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}
}

int run_tests(const fs::path& executable) {
    // All sources below are synthetic. No DPI setup, EnumWindows, controller or screen access.
    const auto artifacts = executable.parent_path() / wide("selftest-" + unique_id()) / L"测试 空格";
    fs::create_directories(artifacts);
    const auto w = fixture_window();
    const auto frame = fixture_frame(w);
    const Verify unchanged = [](const Window&) {};
    expect(is_game_window(w), "Game candidate rejected.");
    auto wrong = w; wrong.executable = "C:\\launcher.exe";
    expect(!is_game_window(wrong), "Launcher accepted.");
    wrong = w; wrong.class_name = "Chrome_WidgetWin_1";
    expect(!is_game_window(wrong), "Browser accepted.");
    wrong = w; wrong.title = "Other game";
    expect(!is_game_window(wrong), "Non-game title accepted.");
    wrong = w; wrong.process_created++;
    expect(!same_source(w, wrong), "Reused HWND accepted.");
    wrong = w; wrong.identity_verified = false;
    expect(!same_source(w, wrong), "Verified identity matched a limited identity.");
    wrong = w; wrong.width++;
    expect(!same_source(w, wrong), "Resize accepted.");
    wrong = w; wrong.left++;
    expect(!same_source(w, wrong), "Moved client origin accepted.");
    wrong = w; wrong.title += " changed";
    expect(same_source(w, wrong), "Title-only change invalidated source identity.");
    auto limited = w; limited.identity_verified = false;
    expect(same_source(limited, limited), "Two limited identities with matching geometry should match.");
    wrong = limited; wrong.hwnd++;
    expect(!same_source(limited, wrong), "Limited identity accepted a different HWND.");
    wrong = limited; wrong.pid++;
    expect(!same_source(limited, wrong), "Limited identity accepted a different PID.");
    wrong = limited; wrong.class_name += "_other";
    expect(!same_source(limited, wrong), "Limited identity accepted a different window class.");
    require_usable(w); // A non-foreground but fully visible window is usable.
    wrong = w; wrong.minimized = true;
    expect_error("window_minimized", [&] { require_usable(wrong); });
    wrong = w; wrong.visible = false;
    expect_error("window_not_visible", [&] { require_usable(wrong); });
    wrong = w; wrong.cloaked = true;
    expect_error("window_cloaked", [&] { require_usable(wrong); });
    wrong = w; wrong.width = 0;
    expect_error("window_empty_client", [&] { require_usable(wrong); });
    wrong = w; wrong.partly_offscreen = true;
    expect_error("window_offscreen", [&] { require_usable(wrong); });
    wrong = w; wrong.opacity = "unknown";
    expect_error("window_opacity_unknown", [&] { require_usable(wrong); });
    wrong = w; wrong.opacity = "non_opaque";
    expect_error("window_transparent", [&] { require_usable(wrong); });
    wrong = w; wrong.visibility = "unknown";
    expect_error("window_visibility_unknown", [&] { require_usable(wrong); });
    wrong = w; wrong.visibility = "occluded";
    wrong.occluding_hwnd = 789;
    expect_error("window_occluded", [&] { require_usable(wrong); });

    const ScreenRect target{0, 0, 100, 100};
    const std::array<ScreenRect, 2> adjacent_monitors{{{0, 0, 50, 100}, {50, 0, 100, 100}}};
    const std::array<ScreenRect, 2> gapped_monitors{{{0, 0, 49, 100}, {51, 0, 100, 100}}};
    expect(rectangle_covered(target, adjacent_monitors), "Adjacent monitor union did not cover target.");
    expect(!rectangle_covered(target, gapped_monitors), "Monitor gap was treated as covered.");
    expect(!rectangle_covered(target, std::span<const ScreenRect>{}), "Empty monitor set covered target.");
    expect(rectangles_overlap({0, 0, 10, 10}, {9, 9, 20, 20}), "Overlapping rectangles were missed.");
    expect(!rectangles_overlap({0, 0, 10, 10}, {10, 0, 20, 10}), "Edge-only contact counted as overlap.");

    Frame timestamped = frame;
    const auto now = monotonic_100ns();
    timestamped.source_time_100ns = now - 250000;
    expect(frame_age_ms(timestamped, now) == 25.0, "Source-frame age conversion failed.");
    timestamped.source_time_100ns = 0;
    expect(frame_age_ms(timestamped, now) == -1.0, "Missing source timestamp was not unknown.");
    timestamped.source_time_100ns = now + 1;
    expect_error("capture_invalid_timestamp", [&] { (void)frame_age_ms(timestamped, now); });
    auto same_pixels = frame;
    same_pixels.source_time_100ns = now;
    expect(frame_age_ms(same_pixels, now + 100000) == 10.0,
           "Identical pixels were incorrectly treated as stale.");
    expect_error("capture_invalid", [] { pixel_bytes(0, 90); });
    expect_error("capture_invalid", [] { pixel_bytes(INT32_MAX, INT32_MAX); });
    const std::vector<std::uint8_t> bgr{1, 2, 3, 4, 5, 6}, bgra{1, 2, 3, 99, 4, 5, 6, 0};
    const std::vector<std::uint8_t> rgb{3, 2, 1, 6, 5, 4};
    expect(frame_from_bgr(2, 1, 3, bgr).rgb == rgb, "BGR channel conversion failed.");
    expect(frame_from_bgr(2, 1, 4, bgra).rgb == rgb, "BGRA channel conversion failed.");
    expect_error("capture_invalid", [&] { frame_from_bgr(2, 1, 1, bgr); });
    expect_error("capture_invalid", [&] { frame_from_bgr(2, 1, 4, bgr); });
    std::vector<std::uint8_t> padded_bgra(3 * 16, 0xee);
    const auto set_bgra = [&](int x, int y, std::array<std::uint8_t, 4> color) {
        std::copy(color.begin(), color.end(), padded_bgra.begin() + y * 16 + x * 4);
    };
    set_bgra(1, 1, {3, 2, 1, 255}); set_bgra(2, 1, {6, 5, 4, 255});
    set_bgra(1, 2, {9, 8, 7, 255}); set_bgra(2, 2, {12, 11, 10, 255});
    const std::vector<std::uint8_t> cropped_rgb{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const auto cropped = frame_from_bgra_rows(2, 2, padded_bgra, 16, 1, 1);
    expect(cropped.rgb == cropped_rgb, "BGRA row pitch or nonzero crop conversion failed.");
    expect_error("capture_invalid", [&] { frame_from_bgra_rows(2, 2, padded_bgra, 16, -1, 1); });
    expect_error("capture_invalid", [&] {
        frame_from_bgra_rows(2, 2, std::span<const std::uint8_t>(padded_bgra.data(), 43), 16, 1, 1);
    });
    expect_error("capture_invalid", [&] { frame_from_bgra_rows(2, 2, padded_bgra, 11, 1, 1); });
    auto invalid = frame; invalid.rgb.pop_back();
    expect_error("capture_invalid", [&] { validate_frame(invalid, w); });
    invalid = frame; invalid.width++;
    invalid.rgb.resize(pixel_bytes(invalid.width, invalid.height));
    expect_error("capture_size_mismatch", [&] { validate_frame(invalid, w); });
    invalid = frame; std::fill(invalid.rgb.begin(), invalid.rgb.end(), std::uint8_t{0});
    expect_error("capture_black", [&] { validate_frame(invalid, w); });
    invalid.rgb[0] = 1;
    expect(validate_frame(invalid, w) == std::vector<std::string>{"near_black_image"}, "Dark frame was not a warning.");
    expect(quote_json("a\n\"\\\x01") == "\"a\\n\\\"\\\\\\u0001\"", "JSON escaping failed.");
    expect(utf8(wide("中文😀")) == "中文😀", "Unicode roundtrip failed.");
    expect_error("utf8", [] { (void)quote_json(std::string(1, static_cast<char>(0xff))); });

    int calls = 0;
    auto c = capture_once(w, "auto", unchanged, [&](const Window&, const std::string& method) {
        ++calls;
        expect(method == "wgc", "Auto did not resolve to the sole WGC backend.");
        return frame;
    });
    expect(calls == 1 && c.method == "wgc" && c.attempts.size() == 1, "Single WGC acquisition contract failed.");
    expect(c.backend_version == "stronghold-wgc-v1", "WGC backend version was not recorded.");
    expect(c.attempts.front().method == "wgc" && c.attempts.front().status == "succeeded",
           "Successful WGC attempt was not recorded.");
    auto limited_metadata_capture = c;
    limited_metadata_capture.window.identity_verified = false;
    limited_metadata_capture.window.process_created = 0;
    limited_metadata_capture.window.executable.clear();
    limited_metadata_capture.window.identity_warning = "process query unavailable";
    const auto limited_metadata = capture_json(limited_metadata_capture, "limited-source");
    expect(limited_metadata.find("\"identity_verified\":false") != std::string::npos,
           "Limited identity status was not serialized.");
    expect(limited_metadata.find("\"process_created\":null") != std::string::npos &&
           limited_metadata.find("\"executable\":null") != std::string::npos,
           "Unknown process provenance was not serialized as null.");
    expect(limited_metadata.find("\"identity_warning\":\"process query unavailable\"") != std::string::npos,
           "Limited identity warning was not serialized.");
    expect_error("invalid_argument", [&] { capture_once(w, "bad", unchanged, {}); });
    calls = 0;
    expect_error("window_changed", [&] {
        capture_once(w, "auto", [](const Window&) { throw Error("window_changed", "Simulated change."); },
                     [&](const Window&, const std::string&) { ++calls; return frame; });
    });
    expect(calls == 0, "Capture started after source failure.");
    int verifications = 0;
    calls = 0;
    expect_error("window_occluded", [&] {
        capture_once(w, "auto", [&](const Window&) { if (++verifications == 2) throw Error("window_occluded", "Simulated occlusion."); },
                     [&](const Window&, const std::string&) { ++calls; return frame; });
    });
    expect(calls == 1, "WGC failure incorrectly returned to acquisition or retried another backend.");
    try {
        capture_once(w, "auto", unchanged, [](const Window&, const std::string&) -> Frame {
            throw Error("capture_timeout", "Simulated timeout.");
        });
        throw Error("test_failed", "All-failed capture returned a frame.");
    } catch (const CaptureError& e) {
        expect(e.attempts.size() == 1, "Single backend failure did not preserve exactly one attempt.");
        expect(e.attempts.front().method == "wgc" && e.attempts.front().code == "capture_timeout",
               "Single backend failure details were lost.");
    }

    CaptureGate gate;
    const auto gate_start = std::chrono::steady_clock::time_point{} + std::chrono::seconds(10);
    gate.begin(gate_start);
    try {
        gate.begin(gate_start + std::chrono::milliseconds(100));
        throw Error("test_failed", "Busy capture admission was accepted.");
    } catch (const AdmissionError& e) {
        expect(e.code == "capture_busy" && e.retry_after == std::chrono::milliseconds(1000),
               "Busy admission response was incorrect.");
    }
    gate.finish(); // A failed or completed attempt frees the gate; no request is queued.
    try {
        gate.begin(gate_start + std::chrono::milliseconds(499));
        throw Error("test_failed", "A start inside the 500 ms interval was accepted.");
    } catch (const AdmissionError& e) {
        expect(e.code == "capture_throttled" && e.retry_after == std::chrono::milliseconds(1),
               "499 ms throttling response was incorrect.");
    }
    gate.begin(gate_start + std::chrono::milliseconds(500));
    gate.finish();

    CaptureGate concurrent_gate;
    std::atomic<int> ready{0};
    std::atomic<bool> release{false};
    std::atomic<int> admitted{0};
    std::vector<std::future<void>> admission_jobs;
    for (int i = 0; i < 8; ++i) {
        admission_jobs.push_back(std::async(std::launch::async, [&] {
            ++ready;
            while (!release.load()) std::this_thread::yield();
            try {
                concurrent_gate.begin(gate_start);
                ++admitted;
                concurrent_gate.finish();
            } catch (const AdmissionError&) {}
        }));
    }
    while (ready.load() != 8) std::this_thread::yield();
    release = true;
    for (auto& job : admission_jobs) job.get();
    expect(admitted == 1, "Concurrent admission allowed more than one capture.");

    for (const auto dimensions : {std::pair{160, 90}, std::pair{120, 90}, std::pair{113, 71}}) {
        const auto source = fixture_window(dimensions.first, dimensions.second);
        auto sample = capture_once(source, "wgc", unchanged,
            [](const Window& window, const std::string&) { return fixture_frame(window); });
        if (dimensions.first == 160) {
            sample.frame.source_time_100ns = monotonic_100ns() - 1000000; // 100 ms old.
        } else if (dimensions.first == 120) {
            sample.frame.source_time_100ns = monotonic_100ns() - 20000000; // 2 s old.
        } else {
            sample.frame.source_time_100ns = 0;
        }
        sample.window.title += "\n\"quoted\"\\";
        const auto source_snapshot = capture_json(sample, "source-snapshot");
        const auto saved = save_capture(sample, artifacts);
        const std::string expected_freshness = dimensions.first == 160 ? "fresh" :
                                               dimensions.first == 120 ? "stale" : "unknown";
        expect(saved.freshness == expected_freshness, "SavedCapture freshness classification was incorrect.");
        const auto metadata = read_text_file(saved.directory / L"metadata.json");
        expect(metadata.find("\"frame_freshness\":\"" + expected_freshness + "\"") != std::string::npos,
               "Archive metadata freshness classification was incorrect.");
        if (dimensions.first == 113) {
            expect(metadata.find("\"source_frame\":{\"clock\":\"qpc\",\"unit\":\"100ns\",\"time\":null}") != std::string::npos,
                   "Missing source time was not serialized as null.");
            expect(saved.delivery_age_ms == -1.0, "Unknown delivery age was not represented as unavailable.");
        } else {
            expect(saved.delivery_age_ms >= 0.0, "Known delivery age was unavailable.");
        }
        expect(capture_json(sample, "source-snapshot") == source_snapshot,
               "Saving changed the source capture snapshot.");
        check_png(saved.directory / L"raw.png", sample.frame);
        expect(saved.id == utf8(saved.directory.filename().wstring()), "Frame ID differs from directory.");
    }
    auto dark_capture = c; dark_capture.frame = invalid;
    const auto original_metadata = capture_json(dark_capture, "source");
    const auto dark_saved = save_capture(dark_capture, artifacts);
    check_png(dark_saved.directory / L"raw.png", invalid);
    expect(capture_json(dark_capture, "source") == original_metadata, "Saving changed source metadata.");
    std::ifstream dark_metadata_file(dark_saved.directory / L"metadata.json", std::ios::binary);
    std::ostringstream dark_metadata; dark_metadata << dark_metadata_file.rdbuf();
    expect(dark_metadata.str().find("\"near_black_image\"") != std::string::npos,
           "Saving lost the image validation warning.");
    const auto before_failure = count_directories(artifacts);
    expect_error("window_occluded", [&] {
        save_capture(c, artifacts, [](const Window&) {
            throw Error("window_occluded", "Synthetic publish-time visibility failure.");
        });
    });
    expect(before_failure == count_directories(artifacts), "Pre-publish verification failure published a record.");
    auto invalid_time_capture = c;
    invalid_time_capture.frame.source_time_100ns = -1;
    expect_error("capture_invalid_timestamp", [&] { save_capture(invalid_time_capture, artifacts); });
    invalid_time_capture.frame.source_time_100ns = monotonic_100ns() + 1000000000;
    expect_error("capture_invalid_timestamp", [&] { save_capture(invalid_time_capture, artifacts); });
    expect(before_failure == count_directories(artifacts), "Invalid source timestamps published an archive.");
    dark_capture.frame.rgb.clear();
    expect_error("capture_invalid", [&] { save_capture(dark_capture, artifacts); });
    expect(before_failure == count_directories(artifacts), "Failed archive published a record.");
    const auto blocked = artifacts / L"blocked";
    { std::ofstream file(blocked); file << "file blocks output directory"; }
    bool storage_failed = false;
    try { save_capture(c, blocked); } catch (const std::exception&) { storage_failed = true; }
    expect(storage_failed, "Storage failure was not reported.");
    auto bad_metadata = c; bad_metadata.duration_ms = std::numeric_limits<double>::infinity();
    expect_error("metadata_number", [&] { save_capture(bad_metadata, artifacts); });
    expect(before_failure == count_directories(artifacts), "Mid-write failure left a record or temporary directory.");

    std::vector<std::future<SavedCapture>> jobs;
    for (int i = 0; i < 6; ++i) jobs.push_back(std::async(std::launch::async, [&] { return save_capture(c, artifacts); }));
    std::set<std::string> ids;
    for (auto& job : jobs) {
        const auto saved = job.get(); ids.insert(saved.id);
        check_png(saved.directory / L"raw.png", c.frame);
    }
    expect(ids.size() == 6, "Concurrent archives overwrote each other.");
    for (const auto& entry : fs::directory_iterator(artifacts))
        expect(!entry.path().filename().wstring().starts_with(L".tmp-"), "Unpublished temporary archive remained.");
    expect(count_directories(artifacts) == 10, "Synthetic archive success baseline changed.");
    test_worker_supervision(executable);
    ++checks;
    std::cout << "{\"ok\":true,\"checks\":" << checks << ",\"artifacts\":" << quote_json(utf8(artifacts.wstring())) << "}\n";
    return 0;
}
} // namespace stronghold
