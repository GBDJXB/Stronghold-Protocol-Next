#include "internal.hpp"

#include <Windows.h>
#include <psapi.h>

#include <fstream>
#include <memory>
#include <thread>
#include <utility>

namespace stronghold {
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Scratch {
    fs::path directory;
    Scratch() {
        std::error_code error;
        const auto temporary_root = fs::temp_directory_path(error);
        if (error) throw Error("storage_failed", "Cannot locate temporary directory: " + std::to_string(error.value()));
        directory = temporary_root / (L"stronghold-" + wide(unique_id()));
        if (!fs::create_directory(directory, error) || error)
            throw Error("storage_failed", "Cannot create private worker directory: " + std::to_string(error.value()));
    }
    ~Scratch() { std::error_code ec; fs::remove_all(directory, ec); }
};

template<class T> void put(std::ostream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!out) throw Error("worker_protocol", "Cannot write worker payload.");
}
template<class T> T get(std::istream& in) {
    T value{};
    if (!in.read(reinterpret_cast<char*>(&value), sizeof(value))) throw Error("worker_protocol", "Truncated worker payload.");
    return value;
}
void put_text(std::ostream& out, const std::string& s) {
    if (s.size() > 65536) throw Error("worker_protocol", "Worker string exceeds limit.");
    put(out, static_cast<std::uint32_t>(s.size()));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out) throw Error("worker_protocol", "Cannot write worker string.");
}
std::string get_text(std::istream& in) {
    const auto size = get<std::uint32_t>(in);
    if (size > 65536) throw Error("worker_protocol", "Worker string exceeds limit.");
    std::string s(size, '\0');
    if (!in.read(s.data(), size)) throw Error("worker_protocol", "Truncated worker string.");
    return s;
}
constexpr std::uint32_t protocol_magic = 0x53484332; // SHC2, local x64 little-endian protocol.
void write_request(const fs::path& file, const Window& w, const std::string& method) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    put(out, protocol_magic);
    put(out, w.hwnd); put(out, w.pid); put(out, w.process_created);
    put(out, static_cast<std::uint32_t>(w.identity_verified));
    put(out, w.left); put(out, w.top); put(out, w.width); put(out, w.height);
    put_text(out, w.executable); put_text(out, w.class_name); put_text(out, method);
    out.close();
    if (!out) throw Error("storage_failed", "Cannot finish worker request.");
}
std::pair<Window, std::string> read_request(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (get<std::uint32_t>(in) != protocol_magic) throw Error("worker_protocol", "Unsupported worker protocol.");
    Window w;
    w.hwnd = get<std::uint64_t>(in); w.pid = get<std::uint32_t>(in); w.process_created = get<std::uint64_t>(in);
    const auto verified = get<std::uint32_t>(in);
    if (verified > 1) throw Error("worker_protocol", "Invalid identity verification flag.");
    w.identity_verified = verified != 0;
    w.left = get<std::int32_t>(in); w.top = get<std::int32_t>(in);
    w.width = get<std::int32_t>(in); w.height = get<std::int32_t>(in);
    w.executable = get_text(in); w.class_name = get_text(in);
    auto method = get_text(in);
    if (in.peek() != std::char_traits<char>::eof()) throw Error("worker_protocol", "Unexpected worker request data.");
    return {std::move(w), std::move(method)};
}

std::wstring argument(const std::wstring& s) {
    // CommandLineToArgvW quoting, including paths ending in a backslash.
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const auto c : s) {
        if (c == L'\\') { ++slashes; continue; }
        out.append(c == L'\"' ? 2 * slashes + 1 : slashes, L'\\');
        out += c; slashes = 0;
    }
    out.append(2 * slashes, L'\\'); out += L'\"';
    return out;
}

void run_process(const fs::path& executable, const std::vector<std::wstring>& args,
                 const fs::path& log, std::chrono::milliseconds timeout,
                 const std::function<void()>& heartbeat) {
    Handle job(CreateJobObjectW(nullptr, nullptr));
    if (!job.value) throw Error("worker_start_failed", "Cannot create worker job.");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw Error("worker_start_failed", "Cannot configure worker lifetime.");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle log_handle(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle null_input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (log_handle.value == INVALID_HANDLE_VALUE || null_input.value == INVALID_HANDLE_VALUE)
        throw Error("storage_failed", "Cannot create worker log handles.");
    // Inherit only these handles, never unrelated handles held by a future host application.
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<unsigned char> attr_storage(attr_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attr_size))
        throw Error("worker_start_failed", "Cannot initialize worker attributes.");
    struct AttributeGuard { LPPROC_THREAD_ATTRIBUTE_LIST p; ~AttributeGuard() { DeleteProcThreadAttributeList(p); } } guard{attributes};
    HANDLE inherited[] = {log_handle.value, null_input.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof(inherited), nullptr, nullptr))
        throw Error("worker_start_failed", "Cannot constrain handle inheritance.");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = null_input.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log_handle.value;
    startup.lpAttributeList = attributes;
    auto command = argument(executable.wstring());
    for (const auto& item : args) command += L" " + argument(item);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, log.parent_path().c_str(), &startup.StartupInfo, &info))
        throw Error("worker_start_failed", "Cannot launch worker: " + std::to_string(GetLastError()));
    Handle process(info.hProcess), thread(info.hThread);
    if (!AssignProcessToJobObject(job.value, process.value)) {
        TerminateProcess(process.value, 2); WaitForSingleObject(process.value, INFINITE);
        throw Error("worker_start_failed", "Cannot supervise worker process.");
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) {
        TerminateJobObject(job.value, 2); WaitForSingleObject(process.value, INFINITE);
        throw Error("worker_start_failed", "Cannot resume worker process.");
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto next_heartbeat = std::chrono::steady_clock::now();
    try {
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            if (heartbeat && now >= next_heartbeat) {
                heartbeat();
                next_heartbeat = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            const auto status = WaitForSingleObject(process.value, 20);
            if (status == WAIT_OBJECT_0) break;
            if (status == WAIT_FAILED) throw Error("worker_crashed", "Cannot wait for worker.");
            if (std::chrono::steady_clock::now() >= deadline)
                throw Error("capture_timeout", "Native capture exceeded the per-method deadline.");
        }
    } catch (...) {
        TerminateJobObject(job.value, 2); WaitForSingleObject(process.value, INFINITE); throw;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.value, &exit_code) || (exit_code != 0 && exit_code != 2))
        throw Error("worker_crashed", "Native worker exited unexpectedly: " + std::to_string(exit_code));
}

} // namespace

Frame acquire_isolated(const Window& window, const std::string& method, const fs::path& executable,
                       std::chrono::milliseconds timeout) {
    Scratch scratch;
    const auto request = scratch.directory / L"request.bin", response = scratch.directory / L"response.bin";
    write_request(request, window, method);
    run_process(executable, {L"--worker", request.wstring(), response.wstring()},
                scratch.directory / L"worker.log", timeout, [&] { verify_window(window); });
    std::ifstream in(response, std::ios::binary);
    if (get<std::uint32_t>(in) != protocol_magic) throw Error("worker_protocol", "Invalid worker response header.");
    const auto status = get<std::uint32_t>(in);
    if (status == 1) {
        const auto code = get_text(in); const auto message = get_text(in);
        throw Error(code, message);
    }
    if (status != 0) throw Error("worker_protocol", "Invalid worker response status.");
    Frame frame;
    frame.width = get<std::int32_t>(in); frame.height = get<std::int32_t>(in);
    frame.source_time_100ns = get<std::int64_t>(in);
    frame.worker_peak_working_set_bytes = get<std::uint64_t>(in);
    if (frame_age_ms(frame, monotonic_100ns()) < 0)
        throw Error("capture_invalid_timestamp", "Worker returned no source frame timestamp.");
    frame.rgb.resize(pixel_bytes(frame.width, frame.height));
    if (!in.read(reinterpret_cast<char*>(frame.rgb.data()), static_cast<std::streamsize>(frame.rgb.size())) ||
        in.peek() != std::char_traits<char>::eof()) throw Error("worker_protocol", "Invalid worker image payload.");
    return frame;
}

int worker_main(const fs::path& request, const fs::path& response) {
    try {
        initialize_dpi();
        auto [window, method] = read_request(request);
        if (method != "wgc") throw Error("worker_protocol", "Only the native wgc backend is supported.");
        auto frame = capture_wgc(window, std::chrono::seconds(8));
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
            frame.worker_peak_working_set_bytes = memory.PeakWorkingSetSize;
        std::ofstream out(response, std::ios::binary | std::ios::trunc);
        put(out, protocol_magic); put(out, std::uint32_t{0});
        put(out, frame.width); put(out, frame.height);
        put(out, frame.source_time_100ns); put(out, frame.worker_peak_working_set_bytes);
        out.write(reinterpret_cast<const char*>(frame.rgb.data()), static_cast<std::streamsize>(frame.rgb.size()));
        out.close();
        return out ? 0 : 3;
    } catch (const std::exception& e) {
        try {
            const auto* error = dynamic_cast<const Error*>(&e);
            std::ofstream out(response, std::ios::binary | std::ios::trunc);
            put(out, protocol_magic); put(out, std::uint32_t{1});
            put_text(out, error ? error->code : "worker_failed");
            put_text(out, error ? e.what() : "Native worker encountered an unexpected local failure.");
            out.close();
            return out ? 2 : 3;
        } catch (...) { return 3; }
    }
}

Capture capture_window(const Window& window, const std::string& method,
                       const fs::path& worker_executable) {
    return capture_once(window, method, verify_window,
        [&](const Window& source, const std::string& backend) {
            return acquire_isolated(source, backend, worker_executable);
        });
}

// Exercises the actual Windows process/job boundary without screen or window access.
void test_worker_supervision(const fs::path& executable) {
    Scratch scratch;
    for (const auto& mode : {L"hang", L"crash"}) {
        bool expected = false;
        try {
            run_process(executable, {L"--test-worker", mode}, scratch.directory / (std::wstring(mode) + L".log"),
                        std::wstring(mode) == L"hang" ? std::chrono::milliseconds(150) : std::chrono::seconds(3), {});
        } catch (const Error& e) {
            expected = e.code == (std::wstring(mode) == L"hang" ? "capture_timeout" : "worker_crashed");
        }
        if (!expected) throw Error("test_failed", "Worker timeout/crash supervision test failed.");
    }
    bool stopped = false;
    int heartbeats = 0;
    std::chrono::steady_clock::time_point first_heartbeat;
    try {
        run_process(executable, {L"--test-worker", L"hang"}, scratch.directory / L"focus.log",
                    std::chrono::seconds(3), [&] {
                        const auto now = std::chrono::steady_clock::now();
                        if (++heartbeats == 1) { first_heartbeat = now; return; }
                        if (now - first_heartbeat < std::chrono::seconds(1))
                            throw Error("test_failed", "Window heartbeat ran more than once per second.");
                        throw Error("focus_failed", "Simulated foreground loss.");
                    });
    } catch (const Error& e) { stopped = e.code == "focus_failed"; }
    if (!stopped || heartbeats != 2)
        throw Error("test_failed", "Worker heartbeat scheduling/source-change cancellation failed.");
}

} // namespace stronghold
