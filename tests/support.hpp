#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

namespace test {
namespace fs = std::filesystem;
using Env = std::map<std::string, std::string>;
inline void check(bool value, const char* expression, int line) {
    if (!value) throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(expr) ::test::check(static_cast<bool>(expr), #expr, __LINE__)
inline std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline void write(const fs::path& path, std::string_view data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
}
inline bool contains(std::string_view text, std::string_view value) { return text.find(value) != std::string_view::npos; }
inline std::string getenv(const char* name) { const char* value = std::getenv(name); return value ? value : ""; }
struct Temp {
    fs::path path;
    Temp() {
        std::string pattern = (fs::temp_directory_path() / "transcribe-test-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp");
        path = pattern;
    }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
    Temp(const Temp&) = delete;
    Temp& operator=(const Temp&) = delete;
};
struct Capture { int code; std::string out, err; };
// Independent fork/exec harness: production process code is never used to
// decide whether transcribe terminates, leaks children or writes to a terminal.
class Child {
public:
    Temp temp;
    pid_t pid = -1;
    int status = 0;
    bool reaped = false;
    // An optional stdout fd is consumed; normally capture to regular files.
    Child(const std::vector<std::string>& args, const Env& env = {}, const fs::path& cwd = {}, int output_fd = -1) {
        const int capture = open((temp.path / "stdout").c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        const int out = output_fd >= 0 ? output_fd : capture;
        if (output_fd >= 0) close(capture);
        const int err = open((temp.path / "stderr").c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (out < 0 || err < 0) throw std::runtime_error("capture files");
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        pid = fork();
        if (pid == 0) {
            setpgid(0, 0);
            if (dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0) _exit(126);
            close(out); close(err);
            if (!cwd.empty() && chdir(cwd.c_str())) _exit(126);
            for (const auto& [key, value] : env) {
                if (value.empty()) unsetenv(key.c_str()); else setenv(key.c_str(), value.c_str(), 1);
            }
            if (const char* limit = std::getenv("TEST_FILE_LIMIT")) {
                const rlim_t size = static_cast<rlim_t>(std::strtoull(limit, nullptr, 10));
                const rlimit limits{size, size};
                std::signal(SIGXFSZ, SIG_IGN);
                if (setrlimit(RLIMIT_FSIZE, &limits)) _exit(126);
            }
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(out); close(err);
        if (pid < 0) throw std::runtime_error("fork");
        setpgid(pid, pid);
    }
    bool running() {
        if (reaped) return false;
        const auto value = waitpid(pid, &status, WNOHANG);
        if (value == pid) reaped = true;
        else if (value < 0 && errno != EINTR) throw std::runtime_error("waitpid test child");
        return !reaped;
    }
    Capture wait(std::chrono::seconds timeout = std::chrono::seconds(15), const std::function<void()>& checkpoint = {}) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (running()) {
            if (checkpoint) checkpoint();
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Test child timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return {WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status),
                read(temp.path / "stdout"), read(temp.path / "stderr")};
    }
    ~Child() {
        if (pid > 0 && !reaped) {
            kill(pid, SIGTERM);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (waitpid(pid, &status, WNOHANG) == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }
    }
};
inline Capture invoke(const std::vector<std::string>& args, const Env& env = {}, const fs::path& cwd = {}) {
    Child child(args, env, cwd);
    return child.wait();
}
inline void success(const Capture& result) {
    if (result.code) throw std::runtime_error("Exit " + std::to_string(result.code) + ": " + result.err);
}
inline void until(const std::function<bool()>& predicate, std::chrono::seconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Readiness timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
inline fs::path which(std::string_view tool) {
    std::istringstream path(getenv("PATH"));
    std::string dir;
    while (std::getline(path, dir, ':')) {
        const auto candidate = fs::path(dir) / tool;
        if (access(candidate.c_str(), X_OK) == 0) return fs::absolute(candidate);
    }
    throw std::runtime_error("Missing tool " + std::string(tool));
}
inline void link(const fs::path& target, const fs::path& destination) {
    fs::create_directories(destination.parent_path()); fs::create_symlink(target, destination);
}
inline size_t entries(const fs::path& directory) { return static_cast<size_t>(std::distance(fs::directory_iterator(directory), fs::directory_iterator())); }
inline fs::path single(const fs::path& directory) { CHECK(entries(directory) == 1); return fs::directory_iterator(directory)->path(); }
inline void wav(const fs::path& file, double seconds = 0.5, int rate = 44100, int channels = 2,
                const std::function<bool(double)>& silent = {}) {
    const auto samples = static_cast<uint32_t>(seconds * rate);
    std::ostringstream out(std::ios::binary);
    const auto le = [&](uint32_t value, int bytes) { for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (8 * i)) & 255)); };
    out << "RIFF"; le(36 + samples * static_cast<uint32_t>(channels) * 2, 4); out << "WAVEfmt "; le(16, 4);
    le(1, 2); le(static_cast<uint32_t>(channels), 2); le(static_cast<uint32_t>(rate), 4);
    le(static_cast<uint32_t>(rate * channels * 2), 4); le(static_cast<uint32_t>(channels * 2), 2); le(16, 2);
    out << "data"; le(samples * static_cast<uint32_t>(channels) * 2, 4);
    for (uint32_t i = 0; i < samples; ++i) {
        const double time = static_cast<double>(i) / rate;
        const auto value = static_cast<int16_t>(silent && silent(time) ? 0 : 10000 * std::sin(time * 2 * 3.141592653589793 * 440));
        for (int ch = 0; ch < channels; ++ch) le(static_cast<uint16_t>(value), 2);
    }
    write(file, out.str());
}
struct Call { std::string kind; std::vector<std::string> args; };
inline std::vector<Call> calls(const fs::path& file) {
    if (!fs::exists(file)) return {};
    std::ifstream in(file, std::ios::binary);
    std::vector<Call> result;
    std::string line;
    while (std::getline(in, line)) {
        Call call{line, {}};
        CHECK(static_cast<bool>(std::getline(in, line)));
        const int count = std::stoi(line);
        for (int i = 0; i < count; ++i) {
            CHECK(static_cast<bool>(std::getline(in, line)));
            std::string arg(static_cast<size_t>(std::stoul(line)), '\0');
            CHECK(static_cast<bool>(in.read(arg.data(), static_cast<std::streamsize>(arg.size()))));
            CHECK(in.get() == '\n'); call.args.push_back(std::move(arg));
        }
        result.push_back(std::move(call));
    }
    return result;
}
inline std::string value(const std::vector<std::string>& args, std::string_view flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    CHECK(it != args.end() && it + 1 != args.end());
    return *(it + 1);
}
inline bool has(const std::vector<std::string>& args, std::string_view flag) { return std::find(args.begin(), args.end(), flag) != args.end(); }
inline void manifest(const fs::path& source, const fs::path& destination, std::string_view payload) {
    Temp temp;
    write(temp.path / "model", payload);
    const auto hash = invoke({which("sha256sum").string(), (temp.path / "model").string()}); success(hash);
    const std::string digest = hash.out.substr(0, 64);
    std::istringstream in(read(source)); std::ostringstream out; std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.front() != '#') {
            std::istringstream row(line); std::string key, file, repo, rev, old;
            CHECK(static_cast<bool>(row >> key >> file >> repo >> rev >> old));
            out << key << ' ' << file << ' ' << repo << ' ' << rev << ' ' << digest << '\n';
        } else out << line << '\n';
    }
    write(destination, out.str());
}
struct Suite {
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    void add(std::string name, std::function<void()> run) { cases.emplace_back(std::move(name), std::move(run)); }
    int run() {
        size_t failed = 0;
        for (const auto& [name, run] : cases) {
            try { run(); std::cout << "PASS " << name << '\n'; }
            catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        }
        std::cout << cases.size() - failed << '/' << cases.size() << " cases passed\n";
        return failed ? 1 : 0;
    }
};
}
