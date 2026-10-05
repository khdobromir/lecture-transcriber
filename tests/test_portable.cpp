#include "audio.hpp"
#include "cancellation.hpp"
#include "platform.hpp"
#include "process.hpp"
#include "result.hpp"
#include "windows.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
using namespace transcribe;
namespace {
constexpr std::size_t floodBytes = std::size_t{1024} * 1024;
void require(bool value) { if (!value) throw std::runtime_error("portable regression failed"); }
void require_dead(unsigned long pid) {
#ifdef _WIN32
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (child) { require(WaitForSingleObject(child, 0) == WAIT_OBJECT_0); CloseHandle(child); }
    else require(GetLastError() == ERROR_INVALID_PARAMETER);
#else
    require(kill(static_cast<pid_t>(pid), 0) < 0 && errno == ESRCH);
#endif
}
void put(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary); out.exceptions(std::ios::failbit | std::ios::badbit);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); out.close();
}
struct Temp {
    fs::path path = temporary_directory(fs::temp_directory_path(), "transcribe-portable-");
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
int execute(const std::vector<std::string>& args) {
    if (!args.empty() && args[0] == "--echo") {
        for (size_t i = 1; i < args.size(); ++i) std::cout << args[i].size() << ':' << args[i] << '\n';
        std::cerr << "diagnostic\n"; return 0;
    }
    if (!args.empty() && (args[0] == "--wait" || args[0] == "--silent-wait")) {
        if (args[0] == "--wait") std::cout << "ready\n" << std::flush;
        for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!args.empty() && args[0] == "--pid-wait") {
#ifdef _WIN32
        std::cout << GetCurrentProcessId() << '\n' << std::flush;
#else
        std::cout << getpid() << '\n' << std::flush;
#endif
        for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!args.empty() && args[0] == "--flood") {
        std::cout << std::string(floodBytes, 'o') << std::flush;
        std::cerr << std::string(floodBytes, 'e') << std::flush;
        return 17;
    }
    if (!args.empty() && args[0] == "--grandchild") {
#ifdef _WIN32
        // Independent native launch, with no production Process/quoting helper.
        const auto binary = executable_directory() / "test_portable.exe";
        std::wstring command = L"\"" + binary.native() + L"\" --silent-wait";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        require(CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &child) != 0);
        CloseHandle(child.hThread);
        std::cout << child.dwProcessId << '\n' << std::flush;
        CloseHandle(child.hProcess);
#else
        const auto child = fork(); require(child >= 0);
        if (!child) { for (;;) pause(); }
        std::cout << child << '\n' << std::flush;
#endif
        // Exit before the helper; the production job must reap the descendant.
        return 0;
    }
    Temp temp;
    install_signal_handlers();
    const std::string environmentValue = "Лекция 😀 & spaces";
#ifdef _WIN32
    require(SetEnvironmentVariableW(L"TRANSCRIBE_PORTABLE_ENV", wide_utf8(environmentValue).c_str()) != 0);
#else
    require(setenv("TRANSCRIBE_PORTABLE_ENV", environmentValue.c_str(), 1) == 0);
#endif
    require(environment_utf8("TRANSCRIBE_PORTABLE_ENV") == environmentValue);
    require(environment_utf8("TRANSCRIBE_PORTABLE_MISSING_ENV").empty());
    std::cout << "PASS Unicode environment transport\n";
    const auto binary = path_utf8(executable_directory() / "test_portable"
#ifdef _WIN32
        ".exe"
#endif
    );
    const std::vector<std::string> values{"", "Лекция 😀", "space quote\" slash\\", "trailing\\", "$(touch PWNED) & | > %PATH%"};
    std::vector<std::string> command{binary, "--echo"}; command.insert(command.end(), values.begin(), values.end());
    std::string output, expected;
    for (const auto& value : values) expected += std::to_string(value.size()) + ':' + value + '\n';
    run(command, temp.path / "process.log", [&](std::string_view bytes) { output += bytes; });
    require(output == expected && !fs::exists(temp.path / "PWNED"));
#ifdef _WIN32
    const std::string quotedValue = R"(😀 \" \)";
    const auto size = quote_windows(wide_utf8(binary)).size() + 1 + 1 + quote_windows(wide_utf8(quotedValue)).size();
    require(command_line_size({binary, quotedValue}) == size);
    require(!command_line_fits({binary, std::string(32767, 'x')}));
    const auto payloadLimit = 32767 - command_line_size({binary, ""});
    require(command_line_size({binary, std::string(payloadLimit, 'x')}) == 32767);
    require(command_line_fits({binary, std::string(payloadLimit, 'x')}));
    require(!command_line_fits({binary, std::string(payloadLimit + 1, 'x')}));
#endif
    std::cout << "PASS Unicode and literal argument transport\n";
    const auto unicode = temp.path / utf8_path("Файл 😀.txt");
    put(unicode, "old"); put(temp.path / "replacement", "new"); replace_file(temp.path / "replacement", unicode);
    std::ifstream in(unicode); std::string value; in >> value; require(value == "new"); in.close();
    auto first = ResultPaths::create(temp.path / "results", "CON <> ? Лекция 😀");
    auto second = ResultPaths::create(temp.path / "results", "CON <> ? Лекция 😀");
    require(first.root != second.root); require_private_directory(first.root);
    fs::create_directory(temp.path / "occupied");
    require(!rename_directory(first.root, temp.path / "occupied") && fs::exists(first.root));
#ifdef _WIN32
    require(safe_title("CON") == "_CON" && safe_title("lpt1.txt") == "_lpt1.txt");
    require(safe_title("a<>:\"/\\|?*.") == "a_________");
    bool longRejected = false;
    try { (void)ResultPaths::create(temp.path / std::wstring(175, L'x'), "short"); }
    catch (const std::runtime_error&) { longRejected = true; }
    require(longRejected);
#endif
    std::cout << "PASS Unicode paths, replacement, titles and collisions\n";
    for (const auto* name : {"transcript.txt", "result.json"}) {
        const auto target = temp.path / name;
        put(target, "previous snapshot");
        SharedReader reader(target);
        put(temp.path / "replacement", "published snapshot");
        replace_file(temp.path / "replacement", target);
        require(reader.size() == 17 && reader.read(0, 1024) == "previous snapshot");
        require(reader.read(9, 3) == "sna" && reader.read(100, 3).empty());
        SharedReader published(target);
        require(published.read(0, 1024) == "published snapshot");
    }
    std::cout << "PASS publication while own TXT/metadata readers remain open\n";
#ifdef _WIN32
    const auto blockedPath = temp.path / "blocked.txt";
    put(blockedPath, "original"); put(temp.path / "replacement", "updated");
    WinHandle blocker(CreateFileW(blockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    require(static_cast<bool>(blocker));
    unsigned checkpoints = 0;
    replace_file(temp.path / "replacement", blockedPath, [&] { if (++checkpoints == 2) blocker.reset(); });
    require(checkpoints == 2);
    blocker.reset(CreateFileW(blockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    put(temp.path / "replacement", "preserved");
    const auto before = std::chrono::steady_clock::now();
    bool locked = false;
    try { replace_file(temp.path / "replacement", blockedPath); } catch (const std::runtime_error&) { locked = true; }
    require(locked && fs::exists(temp.path / "replacement"));
    require(std::chrono::steady_clock::now() - before < std::chrono::seconds(3));
    checkpoints = 0; bool stopped = false;
    try { replace_file(temp.path / "replacement", blockedPath, [&] { if (++checkpoints == 2) throw std::runtime_error("cancel"); }); }
    catch (const std::runtime_error&) { stopped = true; }
    require(stopped && checkpoints == 2 && fs::exists(temp.path / "replacement"));
    blocker.reset();
    std::cout << "PASS bounded replacement retry, synchronized release and cancellation\n";
#endif
    {
        FileLock owner(temp.path / "lock");
        bool blocked = false, cancelled = false;
        try { FileLock waiter(temp.path / "lock", [&] { blocked = true; throw std::runtime_error("cancel lock"); }); }
        catch (const std::runtime_error&) { cancelled = true; }
        require(blocked && cancelled);
    }
    { FileLock available(temp.path / "lock"); }
    std::cout << "PASS cancellable file locking\n";
    bool ready = false;
    std::vector<std::unique_ptr<Process>> children;
    children.push_back(std::make_unique<Process>(std::vector<std::string>{binary, "--wait"}, temp.path / "wait.log",
        [&](std::string_view bytes) { ready = ready || bytes.find("ready") != bytes.npos; }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready && std::chrono::steady_clock::now() < deadline) { children[0]->tick(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    require(ready); cancellation_token().request(2); stop_all(children, 2);
    require(children[0]->done()); children.clear(); cancellation_token().reset();
    std::cout << "PASS worker cancellation\n";
    output.clear(); run({binary, "--grandchild"}, temp.path / "grandchild.log", [&](std::string_view bytes) { output += bytes; });
    require_dead(std::stoul(output));
    std::cout << "PASS exited parent, retained pipe and descendant cleanup\n";
    unsigned long callbackPid = 0; bool threw = false;
    output.clear();
    try {
        run({binary, "--pid-wait"}, temp.path / "callback.log", [&](std::string_view bytes) {
            output += bytes;
            if (output.find('\n') != output.npos) { callbackPid = std::stoul(output); throw std::runtime_error("callback failed"); }
        });
    } catch (const std::runtime_error& error) { threw = std::string_view(error.what()) == "callback failed"; }
    require(threw && callbackPid > 0); require_dead(callbackPid);
    std::cout << "PASS callback failure cleans worker\n";
    std::size_t stdoutBytes = 0, stderrBytes = 0; int floodCode = 0;
    try {
        run({binary, "--flood"}, temp.path / "flood.log", [&](std::string_view bytes) { stdoutBytes += bytes.size(); },
            [&](std::string_view bytes) { stderrBytes += bytes.size(); });
    } catch (const ProcessError& error) { floodCode = error.code; }
    require(floodCode == 17 && stdoutBytes == floodBytes && stderrBytes == floodBytes);
    std::cout << "PASS stdout/stderr flood, final drain and primary exit code\n";
    cancellation_token().commit(); cancellation_token().request(2); require(cancellation_token().completed());
    cancellation_token().reset(); cancellation_token().request(2);
    bool rejected = false; try { cancellation_token().commit(); } catch (const ProcessError&) { rejected = true; }
    require(rejected); cancellation_token().reset();
    std::cout << "PASS completion and cancellation arbitration\n";
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args; for (int i = 1; i < argc; ++i) args.push_back(narrow_utf8(argv[i]));
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
#endif
    try { return execute(args); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
