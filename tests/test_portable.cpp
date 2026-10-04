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
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
using namespace transcribe;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("portable regression failed"); }
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
#ifdef _WIN32
    if (!args.empty() && args[0] == "--grandchild") {
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
        // Exit before the helper; the production job must reap the descendant.
        return 0;
    }
#endif
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
#ifdef _WIN32
    output.clear(); run({binary, "--grandchild"}, temp.path / "grandchild.log", [&](std::string_view bytes) { output += bytes; });
    const auto pid = static_cast<DWORD>(std::stoul(output));
    HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (child) { require(WaitForSingleObject(child, 0) == WAIT_OBJECT_0); CloseHandle(child); }
    else require(GetLastError() == ERROR_INVALID_PARAMETER);
    std::cout << "PASS job object descendant cleanup\n";
#endif
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
