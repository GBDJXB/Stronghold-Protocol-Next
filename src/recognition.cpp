#include "stronghold/recognition.hpp"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>

namespace stronghold {
namespace {
using namespace winrt::Windows::Data::Json;
using Microsoft::WRL::ComPtr;
constexpr std::uintmax_t metadata_limit = 1024 * 1024;
constexpr std::uintmax_t png_limit = 128 * 1024 * 1024;
constexpr std::uint64_t pixel_limit = 32 * 1024 * 1024;
[[noreturn]] void invalid(const char* field) { throw Error("archive_metadata_invalid", std::string("Invalid or missing metadata field: ") + field); }
IJsonValue member(const JsonObject& object, const char* key, JsonValueType type, bool nullable = false) {
    const auto name = winrt::to_hstring(key);
    if (!object.HasKey(name)) invalid(key);
    auto value = object.GetNamedValue(name);
    if (value.ValueType() != type && !(nullable && value.ValueType() == JsonValueType::Null)) invalid(key);
    return value;
}
JsonObject object(const JsonObject& o, const char* key) { return member(o,key,JsonValueType::Object).GetObject(); }
std::string text(const JsonObject& o, const char* key, bool nullable = false) {
    auto v = member(o,key,JsonValueType::String,nullable);
    return v.ValueType() == JsonValueType::Null ? "" : winrt::to_string(v.GetString());
}
bool boolean(const JsonObject& o, const char* key) { return member(o,key,JsonValueType::Boolean).GetBoolean(); }
double number(const JsonObject& o, const char* key, bool nullable = false) {
    auto v = member(o,key,JsonValueType::Number,nullable);
    if (v.ValueType() == JsonValueType::Null) return 0;
    auto n = v.GetNumber();
    if (!std::isfinite(n)) invalid(key);
    return n;
}
std::int64_t integer(const JsonObject& o, const char* key, double low, double high, bool nullable = false) {
    const double n = number(o,key,nullable);
    if (std::trunc(n) != n || n < low || n > high) invalid(key);
    return static_cast<std::int64_t>(n);
}
std::uint64_t decimal(const JsonObject& o, const char* key, bool nullable = false) {
    const auto s = text(o,key,nullable);
    if (s.empty() && nullable && member(o,key,JsonValueType::String,true).ValueType() == JsonValueType::Null) return 0;
    std::uint64_t n = 0;
    auto result = std::from_chars(s.data(),s.data()+s.size(),n);
    if (s.empty() || result.ec != std::errc{} || result.ptr != s.data()+s.size() || s.front() == '+') invalid(key);
    return n;
}
void timestamp(const std::string& s) {
    if(s.size()!=24 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':' || s[19]!='.' || s[23]!='Z') invalid("request timestamp");
    for(std::size_t i=0;i<s.size();++i)
        if(i!=4 && i!=7 && i!=10 && i!=13 && i!=16 && i!=19 && i!=23 && (s[i]<'0' || s[i]>'9')) invalid("request timestamp");
    auto n=[&](std::size_t pos,std::size_t size){ return std::stoi(s.substr(pos,size)); };
    const std::chrono::year_month_day date{std::chrono::year{n(0,4)},std::chrono::month{static_cast<unsigned>(n(5,2))},std::chrono::day{static_cast<unsigned>(n(8,2))}};
    if(!date.ok() || n(0,4)<1 || n(11,2)>23 || n(14,2)>59 || n(17,2)>59) invalid("request timestamp");
}
void nonnegative(const JsonObject& o, const char* key, bool nullable = false) { if(number(o,key,nullable) < 0) invalid(key); }
void strings(const JsonObject& o, const char* key, std::vector<std::string>& out) {
    for (const auto& v : member(o,key,JsonValueType::Array).GetArray()) {
        if(v.ValueType() != JsonValueType::String) invalid(key);
        out.push_back(winrt::to_string(v.GetString()));
    }
}
void require_file(const fs::path& path, std::uintmax_t limit, const char* missing) {
    std::error_code ec;
    const auto status = fs::symlink_status(path,ec);
    if(ec || !fs::exists(status)) throw Error(missing,"Required archive file is missing or inaccessible");
    if(!fs::is_regular_file(status) || (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error("archive_path", "Archive files must be regular files without reparse points");
    const auto bytes = fs::file_size(path,ec);
    if(ec) throw Error("archive_io", "Cannot determine archive file size");
    if(bytes > limit) throw Error("archive_resource_limit", "Archive file exceeds size limit");
}
void hr(HRESULT result) { if(FAILED(result)) throw Error("archive_image_invalid","Cannot decode RGB PNG"); }
struct Apartment {
    bool owned = false;
    Apartment() { auto r = RoInitialize(RO_INIT_MULTITHREADED); if(FAILED(r) && r != RPC_E_CHANGED_MODE) hr(r); owned=SUCCEEDED(r); }
    ~Apartment(){ if(owned) { winrt::clear_factory_cache(); RoUninitialize(); } }
};
Frame read_png(const fs::path& path, int width, int height) {
    ComPtr<IWICImagingFactory> factory;
    hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    hr(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    GUID container{}; hr(decoder->GetContainerFormat(&container));
    if(container != GUID_ContainerFormatPng) throw Error("archive_image_invalid","raw.png must contain a PNG");
    UINT count = 0; hr(decoder->GetFrameCount(&count));
    if(count != 1) throw Error("archive_image_invalid","Expected a single PNG frame");
    ComPtr<IWICBitmapFrameDecode> bitmap; hr(decoder->GetFrame(0,&bitmap));
    UINT w=0,h=0; hr(bitmap->GetSize(&w,&h));
    if(w != static_cast<UINT>(width) || h != static_cast<UINT>(height))
        throw Error("archive_image_dimensions","PNG dimensions disagree with metadata");
    WICPixelFormatGUID format{}; hr(bitmap->GetPixelFormat(&format));
    if(format != GUID_WICPixelFormat24bppBGR && format != GUID_WICPixelFormat24bppRGB)
        throw Error("archive_image_format","Expected original RGB8 PNG without alpha or palette");
    Frame frame{width,height,std::vector<std::uint8_t>(static_cast<std::size_t>(width)*height*3)};
    hr(bitmap->CopyPixels(nullptr,width*3,static_cast<UINT>(frame.rgb.size()),frame.rgb.data()));
    if(format == GUID_WICPixelFormat24bppBGR)
        for(std::size_t i=0;i<frame.rgb.size();i+=3) std::swap(frame.rgb[i],frame.rgb[i+2]);
    return frame;
}
}
LoadedCapture load_capture_archive(const fs::path& directory) {
    Apartment apartment;
    LoadedCapture loaded; loaded.directory = fs::absolute(directory);
    const auto metadata = loaded.directory / L"metadata.json";
    require_file(metadata,metadata_limit,"archive_metadata_missing");
    std::ifstream input(metadata,std::ios::binary);
        loaded.metadata_json.resize(metadata_limit+1);
    input.read(loaded.metadata_json.data(), static_cast<std::streamsize>(loaded.metadata_json.size()));
    loaded.metadata_json.resize(static_cast<std::size_t>(input.gcount()));
    if(loaded.metadata_json.size()>metadata_limit) throw Error("archive_resource_limit","Metadata exceeds size limit");
    if(input.bad() || (!input.eof() && input.fail())) throw Error("archive_io","Cannot read metadata.json");
    JsonObject root;
    try { root = JsonObject::Parse(winrt::hstring(wide(loaded.metadata_json))); }
    catch(const winrt::hresult_error&) { throw Error("archive_metadata_invalid","metadata.json is not a JSON object"); }
    catch(const Error& e) { if(e.code == "utf8") throw Error("archive_metadata_invalid","metadata.json is not UTF-8"); throw; }
    try {
        if(integer(root,"schema_version",0,9007199254740991.0) != 2) throw Error("archive_version","Only capture schema 2 is supported");
        loaded.id = text(root,"frame_id"); if(loaded.id.empty()) invalid("frame_id");
        auto& c = loaded.capture;
        c.requested_at=text(root,"request_start"); c.completed_at=text(root,"request_end");
        timestamp(c.requested_at); timestamp(c.completed_at); if(c.completed_at<c.requested_at) invalid("request_end");
        c.method=text(root,"method"); if(c.method != "wgc") invalid("method");
        c.duration_ms=number(root,"duration_ms"); if(c.duration_ms<0) invalid("duration_ms");
        auto backend=object(root,"backend");
        if(text(backend,"name") != "windows-graphics-capture") invalid("backend.name");
        c.backend_version=text(backend,"implementation");
        auto source=object(root,"source_frame");
        if(text(source,"clock") != "qpc" || text(source,"unit") != "100ns") invalid("source_frame");
        const auto time=decimal(source,"time",true);
        if(time>static_cast<std::uint64_t>(INT64_MAX)) invalid("source_frame.time");
        const bool has_time=member(source,"time",JsonValueType::String,true).ValueType()!=JsonValueType::Null;
        if(has_time && time==0) invalid("source_frame.time");
        auto freshness=text(root,"frame_freshness");
        const auto age_value=member(root,"frame_age_ms_at_archive_check",JsonValueType::Number,true);
        if(!has_time) {
            if(freshness!="unknown" || age_value.ValueType()!=JsonValueType::Null) invalid("source_frame timing");
        } else {
            if(age_value.ValueType()==JsonValueType::Null) invalid("frame_age_ms_at_archive_check");
            const double age=number(root,"frame_age_ms_at_archive_check");
            if(age<0 || freshness!=(age<=1000.0 ? "fresh" : "stale")) invalid("source_frame timing");
        }
        auto w=object(root,"window"); auto& window=c.window;
        window.hwnd=integer(w,"hwnd",0,9007199254740991.0); window.pid=static_cast<std::uint32_t>(integer(w,"pid",0,UINT32_MAX));
        window.process_created=decimal(w,"process_created",true);
        window.executable=text(w,"executable",true); window.title=text(w,"title"); window.class_name=text(w,"class");
        auto client=object(w,"client");
        window.left=static_cast<int>(integer(client,"left",INT32_MIN,INT32_MAX)); window.top=static_cast<int>(integer(client,"top",INT32_MIN,INT32_MAX));
        window.width=static_cast<int>(integer(client,"width",1,INT32_MAX)); window.height=static_cast<int>(integer(client,"height",1,INT32_MAX));
        window.visible=boolean(w,"visible"); window.minimized=boolean(w,"minimized"); window.foreground=boolean(w,"foreground");
        window.partly_offscreen=boolean(w,"partly_offscreen"); window.identity_verified=boolean(w,"identity_verified");
        window.cloaked=boolean(w,"cloaked"); window.opacity=text(w,"opacity"); window.visibility=text(w,"visibility");
        window.occluding_hwnd=integer(w,"occluding_hwnd",0,9007199254740991.0,true); window.identity_warning=text(w,"identity_warning",true);
        auto raw=object(root,"raw");
        if(text(raw,"path") != "raw.png") throw Error("archive_path","Only raw.png in the archive directory is supported");
        if(text(raw,"format") != "RGB8") throw Error("archive_image_format","Only RGB8 archives are supported");
        auto width=integer(raw,"width",1,INT32_MAX), height=integer(raw,"height",1,INT32_MAX);
        if(width != window.width || height != window.height) throw Error("archive_image_dimensions","Raw dimensions disagree with source client");
        if(width>16384 || height>16384 || static_cast<std::uint64_t>(width)*height>pixel_limit)
            throw Error("archive_resource_limit","PNG exceeds dimension/pixel limits");
        for(const auto& v:member(root,"attempts",JsonValueType::Array).GetArray()) {
            if(v.ValueType()!=JsonValueType::Object) invalid("attempts");
            auto a=v.GetObject(); Attempt attempt{text(a,"method"),text(a,"status"),text(a,"code"),text(a,"message"),number(a,"duration_ms")};
            if(attempt.duration_ms<0) invalid("attempts.duration_ms"); c.attempts.push_back(std::move(attempt));
        }
        auto perf=object(root,"performance"); nonnegative(perf,"encoding_ms"); nonnegative(perf,"archive_elapsed_ms");
        integer(perf,"parent_peak_working_set_bytes",0,9007199254740991.0,true);
        c.frame.worker_peak_working_set_bytes=integer(perf,"worker_peak_working_set_bytes",0,9007199254740991.0,true);
        const auto png_bytes=integer(perf,"png_bytes",0,9007199254740991.0);
        strings(root,"warnings",c.warnings);
        const auto png=loaded.directory / L"raw.png"; require_file(png,png_limit,"archive_image_missing");
        if(static_cast<std::uintmax_t>(png_bytes) != fs::file_size(png)) throw Error("archive_image_invalid","PNG size disagrees with metadata");
        const auto peak=c.frame.worker_peak_working_set_bytes;
        c.frame=read_png(png,static_cast<int>(width),static_cast<int>(height)); c.frame.source_time_100ns=static_cast<std::int64_t>(time); c.frame.worker_peak_working_set_bytes=peak;
    } catch(const winrt::hresult_error&) { throw Error("archive_metadata_invalid","Cannot read typed metadata"); }
    return loaded;
}
}
