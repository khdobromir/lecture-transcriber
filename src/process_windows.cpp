#include "process.hpp"
#include "cancellation.hpp"
#include "platform.hpp"
#include "windows.hpp"
#include <algorithm>
#include <array>
#include <csignal>
#include <thread>
#include <vector>

namespace transcribe {
namespace {
BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) { cancellation_token().request(2); return TRUE; }
    // Windows imposes a deadline on console-close handlers. Do not pretend
    // those forced shutdowns provide the same guarantees as an explicit cancel.
    return FALSE;
}
std::wstring executable(std::string_view name) {
    const auto path = utf8_path(name);
    if (path.has_parent_path()) return std::filesystem::absolute(path).native();
    std::wstring buffer(32768, L'\0');
    const auto wide = wide_utf8(name);
    const DWORD count = SearchPathW(nullptr, wide.c_str(), L".exe", static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (!count || count >= buffer.size()) throw ProcessError("Не удалось найти инструмент: " + std::string(name), 127);
    buffer.resize(count); return buffer;
}
}
void install_signal_handlers() {
    cancellation_token().reset();
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) windows_error("SetConsoleCtrlHandler");
    SetConsoleOutputCP(CP_UTF8);
    DWORD mode = 0;
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(output, &mode)) SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
int physical_cpus() {
    DWORD count = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &count);
    if (!count) return logical_cpus();
    std::vector<unsigned char> bytes(count);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(bytes.data()), &count)) return logical_cpus();
    DWORD_PTR process_mask = 0, system_mask = 0;
    WORD group_count = 64;
    std::array<USHORT, 64> groups{};
    if (!GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups.data())) group_count = 0;
    const bool affinity = group_count == 1 && GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) != 0 && process_mask != 0;
    int cores = 0;
    for (size_t offset = 0; offset < count;) {
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(bytes.data() + offset);
        if (!info->Size || offset + info->Size > count) break;
        bool usable = !affinity;
        for (WORD group = 0; group < info->Processor.GroupCount; ++group) {
            const auto& mask = info->Processor.GroupMask[group];
            if (affinity) usable = usable || (mask.Group == groups[0] && (mask.Mask & process_mask) != 0);
        }
        if (usable) ++cores;
        offset += info->Size;
    }
    return std::clamp(cores, 1, 256);
}
struct Process::Native {
    WinHandle job, process, out, err;
    DWORD status = STILL_ACTIVE;
    bool reaped = false, stopping = false, released = false;
};
Process::Process(const std::vector<std::string>& args, const std::filesystem::path& log, Output output, Output errors)
    : native_(std::make_unique<Native>()), name_(args.at(0)), log_path_(log), log_(log, std::ios::app | std::ios::binary),
      output_(std::move(output)), errors_(std::move(errors)) {
    log_.exceptions(std::ios::badbit | std::ios::failbit);
    check_cancelled();
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    WinHandle out_write, err_write;
    const auto pipe = [&](WinHandle& reader, WinHandle& writer) {
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, &security, 0)) windows_error("CreatePipe");
        reader.reset(read); writer.reset(write);
        if (!SetHandleInformation(reader.get(), HANDLE_FLAG_INHERIT, 0)) windows_error("SetHandleInformation");
    };
    pipe(native_->out, out_write); pipe(native_->err, err_write);
    WinHandle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (!input) windows_error("Open NUL");
    native_->job.reset(CreateJobObjectW(nullptr, nullptr));
    if (!native_->job) windows_error("CreateJobObject");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(native_->job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) windows_error("SetInformationJobObject");
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) windows_error("InitializeProcThreadAttributeList");
    struct AttributeGuard { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeGuard() { DeleteProcThreadAttributeList(value); } } guard{attributes};
    std::array<HANDLE, 3> inherited{input.get(), out_write.get(), err_write.get()};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), sizeof(inherited), nullptr, nullptr)) windows_error("UpdateProcThreadAttribute");
    const auto application = executable(args[0]);
    std::wstring command = quote_windows(application);
    for (size_t i = 1; i < args.size(); ++i) { command += L' '; command += quote_windows(wide_utf8(args[i])); }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get(); startup.StartupInfo.hStdOutput = out_write.get(); startup.StartupInfo.hStdError = err_write.get();
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION created{};
    if (!CreateProcessW(application.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &created))
        throw ProcessError("Не удалось запустить " + name_ + ": Windows error " + std::to_string(GetLastError()), 127);
    native_->process.reset(created.hProcess);
    WinHandle thread(created.hThread);
    if (!AssignProcessToJobObject(native_->job.get(), native_->process.get())) {
        const auto code = GetLastError();
        TerminateProcess(native_->process.get(), 1); WaitForSingleObject(native_->process.get(), INFINITE);
        throw std::runtime_error("AssignProcessToJobObject: " + std::to_string(code));
    }
    if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) windows_error("ResumeThread");
}
void Process::request_stop(int) {
    if (native_->released || native_->stopping) return;
    native_->stopping = true;
    if (!TerminateJobObject(native_->job.get(), 143)) windows_error("TerminateJobObject");
}
void Process::tick(bool callbacks) {
    const auto drain = [&](WinHandle& reader, const Output& callback, bool diagnostics) {
        std::array<char, 8192> bytes{};
        for (int turns = 0; reader && turns < 16; ++turns) {
            DWORD available = 0;
            if (!PeekNamedPipe(reader.get(), nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) { reader.reset(); break; }
                windows_error("PeekNamedPipe");
            }
            if (!available) break;
            DWORD count = 0;
            if (!ReadFile(reader.get(), bytes.data(), std::min(available, static_cast<DWORD>(bytes.size())), &count, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) { reader.reset(); break; }
                windows_error("ReadFile");
            }
            const std::string_view data(bytes.data(), count);
            if (callbacks && (!callback || diagnostics)) { log_.write(data.data(), static_cast<std::streamsize>(count)); log_.flush(); }
            if (callbacks && callback) callback(data);
        }
    };
    drain(native_->out, output_, false); drain(native_->err, errors_, true);
    if (!native_->reaped && WaitForSingleObject(native_->process.get(), 0) == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(native_->process.get(), &native_->status)) windows_error("GetExitCodeProcess");
        native_->reaped = true;
    }
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION information{};
    if (!QueryInformationJobObject(native_->job.get(), JobObjectBasicAccountingInformation, &information, sizeof(information), nullptr)) windows_error("QueryInformationJobObject");
    if (native_->reaped && information.ActiveProcesses && !native_->stopping) request_stop(15);
    if (native_->reaped && !information.ActiveProcesses && !native_->out && !native_->err) native_->released = true;
}
bool Process::done() const { return native_->released; }
int Process::exit_code() const {
    if (!native_->reaped) throw std::logic_error("Процесс ещё выполняется");
    return static_cast<int>(native_->status);
}
void Process::require_success() const {
    if (exit_code()) throw ProcessError(name_ + " завершился с кодом " + std::to_string(exit_code()) + ". Журнал: " + path_utf8(log_path_), exit_code());
}
Process::~Process() {
    try {
        if (!done()) request_stop(15);
        while (!done()) { tick(false); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    } catch (...) { TerminateJobObject(native_->job.get(), 1); }
}
void stop_all(std::vector<std::unique_ptr<Process>>& processes, int signal) {
    for (auto& process : processes) if (process && !process->done()) process->request_stop(signal);
    std::vector<bool> usable(processes.size(), true);
    bool waiting;
    do {
        waiting = false;
        for (size_t i = 0; i < processes.size(); ++i) if (auto& process = processes[i]; process && !process->done()) {
            try { process->tick(usable[i]); } catch (...) { usable[i] = false; process->request_stop(9); }
            waiting = true;
        }
        if (waiting) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (waiting);
}
void run(const std::vector<std::string>& args, const std::filesystem::path& log, Output output, Output errors, std::chrono::seconds timeout) {
    std::vector<std::unique_ptr<Process>> processes;
    processes.push_back(std::make_unique<Process>(args, log, std::move(output), std::move(errors)));
    const auto start = std::chrono::steady_clock::now();
    try {
        while (!processes[0]->done()) {
            check_cancelled();
            if (timeout.count() && std::chrono::steady_clock::now() - start >= timeout) throw ProcessError("Превышено время ожидания " + args[0], 1);
            processes[0]->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check_cancelled(); processes[0]->require_success();
    } catch (...) { stop_all(processes, cancellation_signal() ? cancellation_signal() : 15); throw; }
}
}
