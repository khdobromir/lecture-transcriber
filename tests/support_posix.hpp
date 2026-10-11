#pragma once
#include "support_portable.hpp"
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

namespace test {
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
        argv.reserve(args.size() + 1);
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

// Source/destination ordering matches filesystem copy operations.
inline void manifest(const fs::path& source, const fs::path& destination, std::string_view payload) { // NOLINT(bugprone-easily-swappable-parameters)
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
}
