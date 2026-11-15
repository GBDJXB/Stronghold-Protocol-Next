#include "internal.hpp"
#include "stronghold/recognition.hpp"
#include "stronghold/analysis.hpp"
#include "stronghold/state.hpp"
namespace stronghold { int run_recognition_tests(); }

#include <Windows.h>
#include <iostream>
#include <limits>

namespace {
using namespace stronghold;
fs::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) throw Error("executable_path", "Cannot locate executable.");
    buffer.resize(length);
    return fs::path(buffer);
}
std::string error_json(const Error& error, const std::vector<Attempt>& attempts = {}) {
    std::string result = "{\"ok\":false,\"error\":{\"code\":" + quote_json(error.code) +
        ",\"message\":" + quote_json(error.what()) + "},\"attempts\":[";
    for (std::size_t i = 0; i < attempts.size(); ++i) {
        if (i) result += ',';
        const auto& a = attempts[i];
        result += "{\"method\":" + quote_json(a.method) + ",\"status\":" + quote_json(a.status) +
            ",\"code\":" + quote_json(a.code) + ",\"message\":" + quote_json(a.message) +
            ",\"duration_ms\":" + std::to_string(a.duration_ms) + "}";
    }
    return result + "]}";
}
std::uint64_t parse_hwnd(const std::wstring& text) {
    if (text.empty() || text[0] == L'-' || text[0] == L'+') throw Error("invalid_argument", "Invalid HWND.");
    try {
        std::size_t end = 0;
        const auto number = std::stoull(text, &end, text.starts_with(L"0x") || text.starts_with(L"0X") ? 16 : 10);
        if (end != text.size() || number == 0 || number > std::numeric_limits<std::uintptr_t>::max())
            throw Error("invalid_argument", "Invalid HWND.");
        return number;
    } catch (const std::invalid_argument&) { throw Error("invalid_argument", "Invalid HWND."); }
      catch (const std::out_of_range&) { throw Error("invalid_argument", "HWND is out of range."); }
}
void help() {
    std::cout << "stronghold-capture capture --output <directory> [--hwnd <decimal|0xhex>]\n"
        "    [--method auto|wgc]\n"
        "stronghold-capture --check-backend\n"
        "stronghold-capture --self-test\n"
        "stronghold-capture analyze-archive --archive <directory> --maa-root <MAA installation>\n"
        "    [--state-output <directory>]\n"
        "stronghold-capture inspect-state --record <file>\n"
        "stronghold-capture inspect-archive --archive <directory>\n"
        "stronghold-capture --self-test-recognition\n"
        "Captures an opaque, unobscured Arknights client area; focus is not required. No game inputs.\n";
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace stronghold;
    try {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        const auto executable = executable_path();
        if (argc >= 2 && std::wstring(argv[1]) == L"--test-worker") {
            if (argc != 3) return 7;
            if (std::wstring(argv[2]) == L"hang") { Sleep(60000); return 0; }
            return 7;
        }
        if (argc >= 2 && std::wstring(argv[1]) == L"--worker") {
            if (argc != 4) return 3;
            return worker_main(fs::absolute(argv[2]), fs::absolute(argv[3]));
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test-recognition") return run_recognition_tests();
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") return run_tests(executable);
        if (argc == 1 || (argc == 2 && (std::wstring(argv[1]) == L"--help" || std::wstring(argv[1]) == L"-h"))) {
            help(); return 0;
        }
        if (argc == 2 && std::wstring(argv[1]) == L"--check-backend") {
            check_wgc_support();
            std::cout << "{\"ok\":true,\"backend\":\"wgc\",\"implementation\":\"stronghold-wgc-v1\"}\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"analyze-archive") {
            if((argc != 6 && argc != 8) || std::wstring(argv[2]) != L"--archive" || std::wstring(argv[4]) != L"--maa-root" || (argc==8 && std::wstring(argv[6])!=L"--state-output"))
                throw Error("invalid_argument", "analyze-archive requires --archive <directory> --maa-root <installation>.");
            const auto result = analyze_capture_archive(argv[3],argv[5]);
            const auto state_file=argc==8?save_state_record(result,argv[7]):fs::path{};
            std::cout << "{\"ok\":true,\"frame_id\":" << quote_json(result.source.id)
                << ",\"state_record\":" << (state_file.empty()?"null":quote_json(utf8(state_file.wstring())))
                << ",\"directory\":" << quote_json(utf8(result.source.directory.wstring()))
                << ",\"observation_status\":\"historical\",\"category\":" << quote_json(result.category)
                << ",\"special_view\":" << quote_json(result.special_view)
                << ",\"reason\":" << quote_json(result.reason)
                << ",\"recognizer_version\":" << quote_json(result.recognizer_version)
                << ",\"runtime_version\":" << quote_json(result.runtime_version)
                << ",\"model_sha256\":" << quote_json(result.model_sha256)
                << ",\"dictionary_sha256\":" << quote_json(result.dictionary_sha256) << ",\"evidence\":[";
            for(size_t i=0;i<result.evidence.size();++i) {
                if(i) std::cout << ','; std::cout << quote_json(result.evidence[i]);
            }
            std::cout << "],\"funds\":{\"status\":" << quote_json(result.funds.status)
                << ",\"value\":" << (result.funds.value ? std::to_string(*result.funds.value) : "null")
                << ",\"confidence\":" << result.funds.confidence
                << ",\"reason\":" << quote_json(result.funds.reason)
                << ",\"raw_text\":" << quote_json(result.funds.raw_text)
                << ",\"model_sha256\":" << quote_json(result.funds.model_sha256)
                << ",\"dictionary_sha256\":" << quote_json(result.funds.dictionary_sha256)
                << "},\"stage\":{\"status\":" << quote_json(result.stage.status)
                << ",\"value\":" << (result.stage.value?quote_json(*result.stage.value):"null")
                << ",\"confidence\":" << result.stage.confidence
                << ",\"reason\":" << quote_json(result.stage.reason)
                << ",\"raw_text\":" << quote_json(result.stage.raw_text)
                << ",\"model_sha256\":" << quote_json(result.stage.model_sha256)
                << ",\"dictionary_sha256\":" << quote_json(result.stage.dictionary_sha256)
                << "},\"remaining_time_unit\":\"seconds\",\"remaining_time\":{\"status\":" << quote_json(result.remaining_time.status)
                << ",\"value\":" << (result.remaining_time.value?std::to_string(*result.remaining_time.value):"null")
                << ",\"confidence\":" << result.remaining_time.confidence
                << ",\"reason\":" << quote_json(result.remaining_time.reason)
                << ",\"raw_text\":" << quote_json(result.remaining_time.raw_text)
                << ",\"model_sha256\":" << quote_json(result.remaining_time.model_sha256)
                << ",\"dictionary_sha256\":" << quote_json(result.remaining_time.dictionary_sha256)
                << "},\"metadata\":" << result.source.metadata_json << "}\n";
            return 0;
        }
        if (std::wstring(argv[1]) == L"inspect-state") {
            if(argc!=4 || std::wstring(argv[2])!=L"--record") throw Error("invalid_argument","inspect-state requires --record <file>.");
            const auto state_json=state_record_json(load_state_record(argv[3]));
            std::cout << "{\"ok\":true,\"state\":" << state_json << "}\n"; return 0;
        }
        if (std::wstring(argv[1]) == L"inspect-archive") {
            if(argc != 4 || std::wstring(argv[2]) != L"--archive" || std::wstring(argv[3]).empty())
                throw Error("invalid_argument", "inspect-archive requires --archive <directory>.");
            const auto loaded = load_capture_archive(argv[3]);
            const auto& c = loaded.capture;
            std::cout << "{\"ok\":true,\"frame_id\":" << quote_json(loaded.id)
                << ",\"directory\":" << quote_json(utf8(loaded.directory.wstring()))
                << ",\"observation_status\":\"historical\",\"width\":" << c.frame.width << ",\"height\":" << c.frame.height
                << ",\"rgb_bytes\":" << c.frame.rgb.size() << ",\"metadata\":" << loaded.metadata_json << "}\n";
            return 0;
        }
        if (std::wstring(argv[1]) != L"capture") throw Error("invalid_argument", "Unknown command; see --help.");
        fs::path output;
        std::optional<std::uint64_t> hwnd;
        std::string method = "auto";
        bool output_set = false, method_set = false;
        for (int i = 2; i < argc; ++i) {
            const std::wstring key(argv[i]);
            if (i + 1 >= argc) throw Error("invalid_argument", "Missing option value.");
            const std::wstring value(argv[++i]);
            if (key == L"--output" && !output_set) { output = fs::absolute(value); output_set = true; }
            else if (key == L"--method" && !method_set) { method = utf8(value); method_set = true; }
            else if (key == L"--hwnd" && !hwnd) hwnd = parse_hwnd(value);
            else throw Error("invalid_argument", "Unknown or duplicate option: " + utf8(key));
        }
        if (!output_set || output.empty()) throw Error("invalid_argument", "--output is required.");
        if (method != "auto" && method != "wgc")
            throw Error("invalid_argument", "Only auto or wgc is supported.");
        const auto request_started = std::chrono::steady_clock::now();
        auto window = select_window(hwnd);
        if (!window.identity_verified) std::cerr << "Limited window identity: " << window.identity_warning << '\n';
        const auto saved = capture_and_save(window, output, executable);
        const auto total_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - request_started).count();
        std::cout << "{\"ok\":true,\"frame_id\":" << quote_json(saved.id)
                  << ",\"directory\":" << quote_json(utf8(saved.directory.wstring()))
                  << ",\"raw_path\":" << quote_json(utf8((saved.directory / L"raw.png").wstring()))
                  << ",\"metadata_path\":" << quote_json(utf8((saved.directory / L"metadata.json").wstring()))
                  << ",\"frame_freshness_at_delivery_check\":" << quote_json(saved.freshness)
                  << ",\"total_duration_ms\":" << total_ms
                  << ",\"frame_age_ms_at_delivery_check\":" << (saved.delivery_age_ms >= 0 ? std::to_string(saved.delivery_age_ms) : "null") << "}\n";
        return 0;
    } catch (const AdmissionError& e) {
        std::cout << "{\"ok\":false,\"error\":{\"code\":" << quote_json(e.code)
                  << ",\"message\":" << quote_json(e.what()) << "},\"retry_after_ms\":" << e.retry_after.count()
                  << ",\"attempts\":[]}\n";
        return 2;
    } catch (const CaptureError& e) {
        std::cout << error_json(e, e.attempts) << '\n'; return 2;
    } catch (const Error& e) {
        std::cout << error_json(e) << '\n'; return 2;
    } catch (const std::exception&) {
        // std::filesystem/system messages may be locale-encoded; keep public JSON valid UTF-8.
        std::cout << "{\"ok\":false,\"error\":{\"code\":\"internal_error\",\"message\":\"Unexpected local system failure.\"},\"attempts\":[]}\n";
        return 3;
    }
}
