#include "stronghold/state.hpp"
#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <cmath>
#include <charconv>
#include <fstream>
#include <limits>
namespace stronghold {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr size_t record_limit=2*1024*1024, metadata_limit=1024*1024;
[[noreturn]] void invalid(const char* field) { throw Error("state_invalid",std::string("Invalid or missing state field: ")+field); }
IJsonValue member(const JsonObject& o,const char* key,JsonValueType type) {
    const auto k=winrt::to_hstring(key);
    if(!o.HasKey(k)) invalid(key);
    auto v=o.GetNamedValue(k); if(v.ValueType()!=type) invalid(key); return v;
}
std::string text(const JsonObject& o,const char* key) { return winrt::to_string(member(o,key,JsonValueType::String).GetString()); }
JsonObject object(const JsonObject& o,const char* key) { return member(o,key,JsonValueType::Object).GetObject(); }
double number(const JsonObject& o,const char* key) {
    double n=member(o,key,JsonValueType::Number).GetNumber(); if(!std::isfinite(n)) invalid(key); return n;
}
void timestamp(const std::string& s) {
    if(s.size()!=24 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':' || s[19]!='.' || s[23]!='Z') invalid("timestamp");
    for(size_t i=0;i<s.size();++i) if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&i!=23&&(s[i]<'0'||s[i]>'9')) invalid("timestamp");
    auto n=[&](size_t i,size_t count){return std::stoi(s.substr(i,count));};
    std::chrono::year_month_day date{std::chrono::year{n(0,4)},std::chrono::month{static_cast<unsigned>(n(5,2))},std::chrono::day{static_cast<unsigned>(n(8,2))}};
    if(!date.ok()||n(0,4)<1||n(11,2)>23||n(14,2)>59||n(17,2)>59) invalid("timestamp");
}
void digest(const std::string& s) {
    if(s.empty()) return;
    if(s.size()!=64 || s.find_first_not_of("0123456789abcdef")!=std::string::npos) invalid("sha256");
}
void validate(const StateRecord& r) {
    if(r.id.empty()||r.frame_id.empty()||r.source_directory.empty()||!r.source_directory.is_absolute()) invalid("source/id");
    if(r.observation_status!="historical") invalid("observation_status");
    timestamp(r.saved_at); timestamp(r.request_start); timestamp(r.request_end);
    if(r.request_end<r.request_start) invalid("request_end");
    if(r.category!="unknown"&&r.category!="ordinary_in_game"&&r.category!="out_of_game"&&r.category!="special_in_game") invalid("category");
    if(r.category=="special_in_game") {
        if(r.special_view!="information"&&r.special_view!="result") invalid("special_view");
    } else if(!r.special_view.empty()) invalid("special_view");
    if(r.recognizer_version.empty()||r.reason.empty()) invalid("recognizer/reason");
    digest(r.model_sha256); digest(r.dictionary_sha256); digest(r.funds.model_sha256); digest(r.funds.dictionary_sha256);
    const auto& f=r.funds;
    if(!std::isfinite(f.confidence)||f.confidence<0||f.confidence>1||f.reason.empty()) invalid("funds.confidence/reason");
    if(f.status=="recognized") {
        if(!f.value||*f.value<0||f.model_sha256.empty()||f.dictionary_sha256.empty()||r.runtime_version.empty()||r.category!="ordinary_in_game") invalid("funds.recognized");
    } else if(f.status!="unknown"||f.value) invalid("funds.status/value");
    if(r.source_metadata_json.size()>metadata_limit) throw Error("state_resource_limit","Source metadata exceeds limit");
    auto decimal=[](const std::string& value) {
        std::uint64_t n=0;auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
        if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()) invalid("source decimal");return n;
    };
    auto m=JsonObject::Parse(winrt::hstring(wide(r.source_metadata_json)));
    if(number(m,"schema_version")!=2||text(m,"frame_id")!=r.frame_id||text(m,"request_start")!=r.request_start||text(m,"request_end")!=r.request_end||text(object(m,"raw"),"path")!="raw.png") invalid("source linkage");
    auto source_frame=object(m,"source_frame");
    if(text(source_frame,"clock")!="qpc"||text(source_frame,"unit")!="100ns"||!source_frame.HasKey(L"time")) invalid("source_frame");
    auto qpc=source_frame.GetNamedValue(L"time");
    if(qpc.ValueType()!=JsonValueType::Null) {
        auto ticks=decimal(text(source_frame,"time"));if(ticks==0||ticks>INT64_MAX) invalid("source_frame.time");
    }
    auto window=object(m,"window");
    auto hwnd=number(window,"hwnd"),pid=number(window,"pid");
    if(std::trunc(hwnd)!=hwnd||hwnd<0||hwnd>9007199254740991.0||std::trunc(pid)!=pid||pid<0||pid>UINT32_MAX||!window.HasKey(L"process_created")) invalid("source identity");
    auto created=window.GetNamedValue(L"process_created");
    if(created.ValueType()!=JsonValueType::Null) decimal(text(window,"process_created"));
}
std::string serialize(const StateRecord& r) {
    const auto& f=r.funds;
    std::string s="{\"schema_version\":2,\"id\":"+quote_json(r.id)+",\"saved_at\":"+quote_json(r.saved_at)+",\"observation_status\":\"historical\",\"source\":{\"frame_id\":"+quote_json(r.frame_id)+",\"directory\":"+quote_json(utf8(r.source_directory.wstring()))+",\"request_start\":"+quote_json(r.request_start)+",\"request_end\":"+quote_json(r.request_end)+",\"metadata_json\":"+quote_json(r.source_metadata_json)+"},\"category\":"+quote_json(r.category)+",\"special_view\":"+quote_json(r.special_view)+",\"reason\":"+quote_json(r.reason)+",\"recognizer_version\":"+quote_json(r.recognizer_version)+",\"runtime_version\":"+quote_json(r.runtime_version)+",\"model_sha256\":"+quote_json(r.model_sha256)+",\"dictionary_sha256\":"+quote_json(r.dictionary_sha256)+",\"funds\":{\"status\":"+quote_json(f.status)+",\"value\":"+(f.value?std::to_string(*f.value):"null")+",\"confidence\":"+std::to_string(f.confidence)+",\"reason\":"+quote_json(f.reason)+",\"raw_text\":"+quote_json(f.raw_text)+",\"model_sha256\":"+quote_json(f.model_sha256)+",\"dictionary_sha256\":"+quote_json(f.dictionary_sha256)+"},\"evidence\":[";
    for(size_t i=0;i<r.evidence.size();++i) {if(i)s+=',';s+=quote_json(r.evidence[i]);} return s+"]}";
}
StateRecord parse(const std::string& content) {
    try {
        auto o=JsonObject::Parse(winrt::hstring(wide(content)));
        if(number(o,"schema_version")!=2) throw Error("state_version","Only state schema 2 is supported");
        StateRecord r;
        r.id=text(o,"id");r.saved_at=text(o,"saved_at");r.observation_status=text(o,"observation_status");
        auto source=object(o,"source");r.frame_id=text(source,"frame_id");r.source_directory=wide(text(source,"directory"));r.source_metadata_json=text(source,"metadata_json");r.request_start=text(source,"request_start");r.request_end=text(source,"request_end");
        r.category=text(o,"category");r.special_view=text(o,"special_view");r.reason=text(o,"reason");r.recognizer_version=text(o,"recognizer_version");r.runtime_version=text(o,"runtime_version");r.model_sha256=text(o,"model_sha256");r.dictionary_sha256=text(o,"dictionary_sha256");
        auto f=object(o,"funds");r.funds.status=text(f,"status");r.funds.confidence=number(f,"confidence");r.funds.reason=text(f,"reason");r.funds.raw_text=text(f,"raw_text");r.funds.model_sha256=text(f,"model_sha256");r.funds.dictionary_sha256=text(f,"dictionary_sha256");
        if(!f.HasKey(L"value")) invalid("funds.value");
        auto v=f.GetNamedValue(L"value");
        if(v.ValueType()!=JsonValueType::Null) {
            double n=number(f,"value");if(std::trunc(n)!=n||n<0||n>INT_MAX) invalid("funds.value");r.funds.value=static_cast<int>(n);
        }
        for(auto item:member(o,"evidence",JsonValueType::Array).GetArray()) {if(item.ValueType()!=JsonValueType::String) invalid("evidence");r.evidence.push_back(winrt::to_string(item.GetString()));}
        validate(r);return r;
    } catch(const winrt::hresult_error&) {throw Error("state_invalid","Malformed or inconsistent state JSON");}
      catch(const Error& e) {if(e.code=="utf8") throw Error("state_invalid","State JSON is not UTF-8");throw;}
}
}
std::string state_record_json(const StateRecord& record) { auto content=serialize(record); if(content.size()>record_limit) throw Error("state_resource_limit","State exceeds size limit"); parse(content); return content; }
fs::path save_state_record(const Analysis& a,const fs::path& output) {
    if(a.source.directory.empty()) invalid("source.directory");
    StateRecord r;r.id=unique_id();r.saved_at=utc_now();r.frame_id=a.source.id;r.source_directory=fs::absolute(a.source.directory);r.source_metadata_json=a.source.metadata_json;
    r.request_start=a.source.capture.requested_at;r.request_end=a.source.capture.completed_at;r.category=a.category;r.special_view=a.special_view;r.reason=a.reason;r.recognizer_version=a.recognizer_version;r.runtime_version=a.runtime_version;r.model_sha256=a.model_sha256;r.dictionary_sha256=a.dictionary_sha256;r.evidence=a.evidence;r.funds=a.funds;
    // Reuse the public-load validation contract before any filesystem mutation.
    const auto content=serialize(r);if(content.size()>record_limit) throw Error("state_resource_limit","State exceeds size limit");parse(content);
    std::error_code ec;fs::create_directories(output,ec);if(ec) throw Error("state_io","Cannot create state output directory");
    const auto file=fs::absolute(output)/wide(r.id+".json"),temp=fs::absolute(output)/wide("."+r.id+".tmp");
    HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) throw Error("state_io","Cannot create unique state temporary file");
    DWORD written=0;const bool ok=WriteFile(h,content.data(),static_cast<DWORD>(content.size()),&written,nullptr)&&written==content.size()&&FlushFileBuffers(h);
    CloseHandle(h);
    if(!ok) {fs::remove(temp,ec);throw Error("state_io","Cannot durably write state temporary file");}
    // MoveFileEx without REPLACE_EXISTING: a previous record can never be truncated.
    if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH)) {fs::remove(temp,ec);throw Error("state_io","Cannot publish immutable state record");}
    return file;
}
StateRecord load_state_record(const fs::path& file) {
    std::error_code ec;auto status=fs::symlink_status(file,ec);
    if(ec||!fs::exists(status)) throw Error("state_missing","State record missing or inaccessible");
    auto attrs=GetFileAttributesW(file.c_str());
    if(!fs::is_regular_file(status)||attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)) throw Error("state_path","State must be a regular file without reparse points");
    auto size=fs::file_size(file,ec);if(ec) throw Error("state_io","Cannot determine state size");
    if(size>record_limit) throw Error("state_resource_limit","State exceeds size limit");
    std::ifstream in(file,std::ios::binary);if(!in) throw Error("state_io","Cannot open state record");std::string content(record_limit+1,'\0');in.read(content.data(),static_cast<std::streamsize>(content.size()));content.resize(static_cast<size_t>(in.gcount()));
    if(content.size()>record_limit) throw Error("state_resource_limit","State exceeds size limit");
    if(in.bad()||(!in.eof()&&in.fail())) throw Error("state_io","Cannot read state record");
    return parse(content);
}
}
