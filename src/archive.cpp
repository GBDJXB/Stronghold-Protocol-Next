#include "stronghold/capture.hpp"

#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <psapi.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace stronghold {
namespace {
template<class T> class ComPtr {
public:
    ~ComPtr() { if (p_) p_->Release(); }
    T** put() { if (p_) { p_->Release(); p_ = nullptr; } return &p_; }
    T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
private: T* p_ = nullptr;
};

class ComApartment {
public:
    ComApartment() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) throw Error("com_init", "CoInitializeEx failed");
        uninit_ = SUCCEEDED(hr);
    }
    ~ComApartment() { if (uninit_) CoUninitialize(); }
private: bool uninit_ = false;
};

void check_hr(HRESULT hr, const char* code, const char* what) {
    if (FAILED(hr)) throw Error(code, what);
}

void write_png(const Frame& frame, const fs::path& path) {
    ComApartment apartment;
    ComPtr<IWICImagingFactory> factory;
    check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.put())), "png_encoder", "Cannot create WIC imaging factory");
    ComPtr<IWICBitmapEncoder> encoder;
    check_hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()), "png_encoder", "Cannot create PNG encoder");
    ComPtr<IWICStream> stream;
    check_hr(factory->CreateStream(stream.put()), "png_encoder", "Cannot create WIC stream");
    check_hr(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "png_encoder", "Cannot open PNG output");
    check_hr(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache), "png_encoder", "Cannot initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> bitmap;
    ComPtr<IPropertyBag2> props;
    check_hr(encoder->CreateNewFrame(bitmap.put(), props.put()), "png_encoder", "Cannot create PNG frame");
    check_hr(bitmap->Initialize(props.get()), "png_encoder", "Cannot initialize PNG frame");
    check_hr(bitmap->SetSize(static_cast<UINT>(frame.width), static_cast<UINT>(frame.height)), "png_encoder", "Cannot set PNG dimensions");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check_hr(bitmap->SetPixelFormat(&format), "png_encoder", "Cannot set BGR8 pixel format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR)) throw Error("png_encoder", "WIC did not accept BGR8 pixel format");
    const auto stride64 = static_cast<std::uint64_t>(frame.width) * 3;
    if (stride64 > std::numeric_limits<UINT>::max() || frame.rgb.size() > std::numeric_limits<UINT>::max())
        throw Error("png_encoder", "Frame exceeds WIC limits");
    std::vector<std::uint8_t> bgr(frame.rgb.size());
    for (std::size_t i = 0; i < frame.rgb.size(); i += 3) {
        bgr[i] = frame.rgb[i + 2];
        bgr[i + 1] = frame.rgb[i + 1];
        bgr[i + 2] = frame.rgb[i];
    }
    check_hr(bitmap->WritePixels(static_cast<UINT>(frame.height), static_cast<UINT>(stride64),
                                 static_cast<UINT>(bgr.size()), bgr.data()),
             "png_encoder", "Cannot write PNG pixels");
    check_hr(bitmap->Commit(), "png_encoder", "Cannot commit PNG frame");
    check_hr(encoder->Commit(), "png_encoder", "Cannot commit PNG file");
}

std::string number(double d) {
    if(!std::isfinite(d)) throw Error("metadata_number", "Non-finite numeric value cannot be encoded in JSON");
    std::ostringstream s; s.imbue(std::locale::classic()); s << std::setprecision(17) << d; return s.str();
}
std::string u64(std::uint64_t n) { return std::to_string(n); }
std::string nullable_text(const std::string& value) { return value.empty() ? "null" : quote_json(value); }
std::string nullable_u64(std::uint64_t value) { return value == 0 ? "null" : u64(value); }
struct TempGuard { fs::path path; bool keep=false; ~TempGuard(){ if(!keep) { std::error_code ec; fs::remove_all(path, ec); } } };

struct FrameAge {
    std::string freshness = "unknown";
    std::optional<double> age_ms;
};

FrameAge measure_frame_age(const Frame& frame, std::int64_t now = 0) {
    if (frame.source_time_100ns == 0) return {};
    const double age = frame_age_ms(frame, now ? now : monotonic_100ns());
    return {age <= 1000.0 ? "fresh" : "stale", age};
}

std::uint64_t parent_peak_working_set() {
    PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
    return GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)) ? memory.PeakWorkingSetSize : 0;
}

void write_metadata_file(const fs::path& path, const std::string& metadata) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw Error("archive_metadata", "Cannot create metadata.json");
    f.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));
    f.flush();
    if (!f) throw Error("archive_metadata", "Cannot write metadata.json");
}
}

std::string utf8(const std::wstring& text) {
    if(text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if(n <= 0) throw Error("utf8", "Invalid UTF-16 text");
    std::string out(static_cast<std::size_t>(n), '\0');
    if(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr) != n)
        throw Error("utf8", "UTF-16 to UTF-8 conversion failed");
    return out;
}
std::wstring wide(const std::string& text) {
    if(text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if(n <= 0) throw Error("utf8", "Invalid UTF-8 text");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    if(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n) != n)
        throw Error("utf8", "UTF-8 to UTF-16 conversion failed");
    return out;
}
std::string quote_json(const std::string& text) {
    // Validate UTF-8 while preserving its original byte encoding.
    (void)wide(text);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out; out.reserve(text.size()+2); out.push_back('"');
    for(unsigned char c : text) {
        switch(c) {
        case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break; case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
        default: if(c < 0x20) { out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); } else out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"'); return out;
}
std::string utc_now() {
    SYSTEMTIME t{}; GetSystemTime(&t);
    char buf[32]{};
    const int n = std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    if(n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) throw Error("clock", "Cannot format UTC timestamp");
    return buf;
}
std::string unique_id() {
    GUID id{}; const HRESULT hr = CoCreateGuid(&id);
    if(FAILED(hr)) throw Error("guid", "CoCreateGuid failed");
    wchar_t buf[39]{};
    const int n = StringFromGUID2(id, buf, static_cast<int>(std::size(buf)));
    if(n != 39) throw Error("guid", "Cannot format GUID");
    std::wstring value(buf+1, buf+37); for(auto& c : value) c = static_cast<wchar_t>(towlower(c));
    return utf8(value);
}

namespace {
std::string capture_json_with_warnings(const Capture& c, const std::string& id,
                                     std::span<const std::string> warnings,
                                     const FrameAge& age,
                                     double encoding_ms,
                                     double archive_elapsed_ms,
                                     std::uintmax_t png_bytes) {
    const auto& w = c.window;
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "{\"schema_version\":2,\"frame_id\":" << quote_json(id)
      << ",\"request_start\":" << quote_json(c.requested_at) << ",\"request_end\":" << quote_json(c.completed_at)
      << ",\"duration_ms\":" << number(c.duration_ms) << ",\"method\":" << quote_json(c.method)
      << ",\"backend\":{\"name\":\"windows-graphics-capture\",\"implementation\":\"stronghold-wgc-v1\"}"
      << ",\"source_frame\":{\"clock\":\"qpc\",\"unit\":\"100ns\",\"time\":"
      << (c.frame.source_time_100ns > 0 ? quote_json(std::to_string(c.frame.source_time_100ns)) : "null") << '}'
      << ",\"frame_freshness\":" << quote_json(age.freshness)
      << ",\"frame_age_ms_at_archive_check\":" << (age.age_ms ? number(*age.age_ms) : "null")
      << ",\"window\":{\"hwnd\":" << u64(w.hwnd)
      << ",\"pid\":" << w.pid << ",\"process_created\":" << (w.process_created ? quote_json(std::to_string(w.process_created)) : "null")
      << ",\"executable\":" << nullable_text(w.executable) << ",\"title\":" << quote_json(w.title)
      << ",\"class\":" << quote_json(w.class_name) << ",\"client\":{\"left\":" << w.left << ",\"top\":" << w.top
      << ",\"width\":" << w.width << ",\"height\":" << w.height << "},\"visible\":" << (w.visible?"true":"false")
      << ",\"minimized\":" << (w.minimized?"true":"false") << ",\"foreground\":" << (w.foreground?"true":"false")
      << ",\"partly_offscreen\":" << (w.partly_offscreen?"true":"false")
      << ",\"identity_verified\":" << (w.identity_verified?"true":"false")
      << ",\"opacity\":" << quote_json(w.opacity) << ",\"visibility\":" << quote_json(w.visibility)
      << ",\"cloaked\":" << (w.cloaked?"true":"false")
      << ",\"occluding_hwnd\":" << nullable_u64(w.occluding_hwnd)
      << ",\"identity_warning\":" << nullable_text(w.identity_warning) << "},\"raw\":{\"path\":\"raw.png\",\"width\":" << c.frame.width
      << ",\"height\":" << c.frame.height << ",\"format\":\"RGB8\"},\"attempts\":[";
    for(std::size_t i=0;i<c.attempts.size();++i) { const auto& a=c.attempts[i]; if(i) o << ',';
        o << "{\"method\":" << quote_json(a.method) << ",\"status\":" << quote_json(a.status) << ",\"code\":" << quote_json(a.code)
          << ",\"message\":" << quote_json(a.message) << ",\"duration_ms\":" << number(a.duration_ms) << '}'; }
    o << "],\"performance\":{\"encoding_ms\":" << number(encoding_ms)
      << ",\"archive_elapsed_ms\":" << number(archive_elapsed_ms)
      << ",\"parent_peak_working_set_bytes\":" << nullable_u64(png_bytes ? parent_peak_working_set() : 0)
      << ",\"worker_peak_working_set_bytes\":" << nullable_u64(c.frame.worker_peak_working_set_bytes)
      << ",\"png_bytes\":" << png_bytes << "},\"warnings\":[";
    for(std::size_t i=0;i<warnings.size();++i) { if(i) o << ','; o << quote_json(warnings[i]); }
    o << "]}"; return o.str();
}
} // namespace

std::string capture_json(const Capture& c, const std::string& id) {
    // No archive check has happened in a pure preview; save_capture measures age.
    return capture_json_with_warnings(c, id, c.warnings, FrameAge{}, 0.0, 0.0, 0);
}

SavedCapture save_capture(const Capture& c, const fs::path& output, const Verify& before_publish) {
    const auto archive_start = std::chrono::steady_clock::now();
    const auto frame_warnings = validate_frame(c.frame, c.window);
    (void)frame_age_ms(c.frame, monotonic_100ns());
    std::error_code ec; fs::create_directories(output, ec);
    if(ec) throw Error("archive_directory", "Cannot create output directory (system code " + std::to_string(ec.value()) + ").");
    const auto id = unique_id();
    const auto final_dir = output / wide(id);
    const auto temp_dir = output / wide(".tmp-" + id);
    if(fs::exists(final_dir) || fs::exists(temp_dir)) throw Error("archive_collision", "Generated archive identifier already exists");
    if(!fs::create_directory(temp_dir, ec) || ec) throw Error("archive_directory", "Cannot create temporary archive directory");
    TempGuard cleanup{temp_dir};
    const auto png = temp_dir / L"raw.png";
    const auto encoding_start = std::chrono::steady_clock::now();
    write_png(c.frame, png);
    const double encoding_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - encoding_start).count();
    const auto png_bytes = fs::file_size(png, ec);
    if (ec) throw Error("archive_metadata", "Cannot read encoded PNG file size (system code " + std::to_string(ec.value()) + ").");
    // Source age is deliberately measured after encoding and before metadata creation.
    const auto age = measure_frame_age(c.frame);
    const double archive_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - archive_start).count();
    auto warnings = c.warnings;
    for (const auto& warning : frame_warnings) {
        if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end())
            warnings.push_back(warning);
    }
    const auto metadata = capture_json_with_warnings(c, id, warnings, age, encoding_ms, archive_elapsed_ms, png_bytes);
    const auto meta_path = temp_dir / L"metadata.json";
    write_metadata_file(meta_path, metadata);
    if (before_publish) before_publish(c.window);
    fs::rename(temp_dir, final_dir, ec);
    if(ec) throw Error("archive_publish", "Cannot publish capture directory (system code " + std::to_string(ec.value()) + ").");
    cleanup.keep = true;
    // Once published, a failed clock observation must not report the archive as
    // failed or hide its path. Unknown freshness prevents current-state use.
    FrameAge delivery_age;
    try { delivery_age = measure_frame_age(c.frame); } catch (const Error&) {}
    return {id, final_dir, delivery_age.freshness, delivery_age.age_ms.value_or(-1.0)};
}
} // namespace stronghold
