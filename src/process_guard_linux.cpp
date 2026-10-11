#include "process_guard_linux.hpp"
#include "process.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace transcribe {
namespace {
struct Pipe {
    int ends[2]{-1, -1};
    Pipe() { if (pipe2(ends, O_CLOEXEC)) throw ProcessError(std::string("pipe2: ") + std::strerror(errno), 127); }
    ~Pipe() { for (const auto fd : ends) if (fd >= 0) close(fd); }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    void closeEnd(unsigned end) { if (ends[end] >= 0) close(ends[end]); ends[end] = -1; }
};
struct Ready { pid_t group; int error; };
struct ChildChannels { int output, errors, failure, lease, ready; };
struct Launch { const char* const* programs; std::size_t count; char* const* argv; ChildChannels channels; unsigned limit; };
bool write_all(int fd, const void* source, std::size_t size) {
    const auto* bytes = static_cast<const char*>(source);
    while (size) {
        const auto n = write(fd, bytes, size);
        if (n > 0) { bytes += n; size -= static_cast<std::size_t>(n); }
        else if (n < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}
void close_extra(unsigned first, unsigned limit) {
    if (syscall(SYS_close_range, first, ~0U, 0) == 0) return;
    for (auto fd = first; fd < limit; ++fd) close(static_cast<int>(fd));
}
[[noreturn]] void exec_failed(int fd, int error) { write_all(fd, &error, sizeof(error)); _exit(127); }
// Everything below the fork boundary uses preallocated data and system calls;
// no allocation, logging, callbacks, Qt or C++ destructors run in either child.
[[noreturn]] void execute(const Launch& launch, pid_t owner) {
    const auto failure = launch.channels.failure;
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != owner) exec_failed(failure, ESRCH);
    if (setpgid(0, 0)) exec_failed(failure, errno);
    struct sigaction defaults{}; defaults.sa_handler = SIG_DFL; sigemptyset(&defaults.sa_mask);
    for (const auto signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) if (sigaction(signal, &defaults, nullptr)) exec_failed(failure, errno);
    sigset_t mask{}; sigemptyset(&mask); if (sigprocmask(SIG_SETMASK, &mask, nullptr)) exec_failed(failure, errno);
    const auto input = open("/dev/null", O_RDONLY);
    if (input < 0 || dup2(input, 0) < 0 || dup2(launch.channels.output, 1) < 0 || dup2(launch.channels.errors, 2) < 0) exec_failed(failure, errno);
    if (dup2(failure, 3) < 0) exec_failed(failure, errno);
    if (fcntl(3, F_SETFD, FD_CLOEXEC)) exec_failed(3, errno);
    close_extra(4, launch.limit);
    int error = ENOENT;
    for (std::size_t i = 0; i < launch.count; ++i) {
        execve(launch.programs[i], launch.argv, environ);
        if (errno == EACCES) error = EACCES;
        else if (errno != ENOENT && errno != ENOTDIR) exec_failed(3, errno);
    }
    exec_failed(3, error);
}
bool group_alive(pid_t group) { return kill(-group, 0) == 0 || errno == EPERM; }
[[noreturn]] void supervise(const Launch& launch) {
    const auto ready = launch.channels.ready;
    struct sigaction ignored{}; ignored.sa_handler = SIG_IGN; sigemptyset(&ignored.sa_mask);
    for (const auto signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) sigaction(signal, &ignored, nullptr);
    sigset_t mask{}; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, nullptr);
    if (setpgid(0, 0) || prctl(PR_SET_CHILD_SUBREAPER, 1)) {
        const Ready result{-1, errno}; write_all(ready, &result, sizeof(result)); _exit(127);
    }
    const auto owner = getpid();
    const auto tool = fork();
    if (!tool) execute(launch, owner);
    const Ready result{tool, tool < 0 ? errno : 0};
    if (!write_all(ready, &result, sizeof(result))) {
        if (tool > 0) { kill(-tool, SIGKILL); kill(tool, SIGKILL); }
    }
    if (tool < 0) _exit(127);
    if (dup2(launch.channels.lease, 0) < 0) { kill(-tool, SIGKILL); kill(tool, SIGKILL); _exit(127); }
    close_extra(1, launch.limit);
    bool reaped = false, stopping = false, killed = false;
    int mainStatus = 0;
    timespec deadline{};
    for (;;) {
        int status = 0;
        for (auto child = waitpid(-1, &status, WNOHANG); child > 0; child = waitpid(-1, &status, WNOHANG)) {
            if (child == tool) { reaped = true; mainStatus = status; }
        }
        if (reaped && !group_alive(tool)) _exit(WIFSIGNALED(mainStatus) ? 128 + WTERMSIG(mainStatus) : WEXITSTATUS(mainStatus));
        if (reaped && !stopping) {
            kill(-tool, SIGTERM); stopping = true;
            clock_gettime(CLOCK_MONOTONIC, &deadline); deadline.tv_sec += 2;
        }
        timespec now{}; clock_gettime(CLOCK_MONOTONIC, &now);
        if (stopping && !killed && (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))) {
            kill(-tool, SIGKILL); killed = true;
        }
        pollfd pipe{0, POLLIN, 0};
        if (poll(&pipe, 1, 10) > 0 && (static_cast<unsigned>(pipe.revents) & static_cast<unsigned>(POLLIN | POLLHUP | POLLERR))) {
            char byte = 0;
            const auto n = read(0, &byte, 1);
            if (!n || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                kill(-tool, SIGKILL); if (!reaped) kill(tool, SIGKILL); killed = true;
            }
        }
    }
}
}
GuardedChild spawn_guarded(const std::vector<std::string>& args, int output, int errors) {
    std::vector<std::string> paths;
    if (args.at(0).find('/') != std::string::npos) paths.push_back(args[0]);
    else {
        const auto* environment = std::getenv("PATH"); const std::string path = environment ? environment : "/bin:/usr/bin";
        for (std::size_t begin = 0;;) {
            const auto end = path.find(':', begin);
            const auto entry = path.substr(begin, end == path.npos ? end : end - begin);
            paths.push_back((entry.empty() ? "." : entry) + '/' + args[0]);
            if (end == path.npos) break;
            begin = end + 1;
        }
    }
    std::vector<const char*> programs; programs.reserve(paths.size()); for (const auto& path : paths) programs.push_back(path.c_str());
    std::vector<char*> argv; argv.reserve(args.size() + 1); for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str())); argv.push_back(nullptr);
    rlimit files{};
    if (getrlimit(RLIMIT_NOFILE, &files)) throw ProcessError("getrlimit failed", 127);
    const auto limit = static_cast<unsigned>(std::min<rlim_t>(files.rlim_max, static_cast<rlim_t>(INT_MAX)));
    Pipe ready, failure, lease;
    const Launch launch{programs.data(), programs.size(), argv.data(),
        {.output = output, .errors = errors, .failure = failure.ends[1], .lease = lease.ends[0], .ready = ready.ends[1]}, limit};
    const auto guardian = fork();
    if (!guardian) supervise(launch);
    if (guardian < 0) throw ProcessError(std::string("fork: ") + std::strerror(errno), 127);
    ready.closeEnd(1); failure.closeEnd(1); lease.closeEnd(0);
    try {
        Ready result{};
        ssize_t received;
        do { received = read(ready.ends[0], &result, sizeof(result)); } while (received < 0 && errno == EINTR);
        if (received != static_cast<ssize_t>(sizeof(result)) || result.error) throw ProcessError("Не удалось запустить " + args[0] + ": " + std::strerror(result.error ? result.error : EIO), 127);
        int error = 0;
        do { received = read(failure.ends[0], &error, sizeof(error)); } while (received < 0 && errno == EINTR);
        if (received != 0) throw ProcessError("Не удалось запустить " + args[0] + ": " + std::strerror(received == static_cast<ssize_t>(sizeof(error)) ? error : EIO), 127);
        const auto lifetime = lease.ends[1]; lease.ends[1] = -1;
        return {guardian, result.group, lifetime};
    } catch (...) {
        lease.closeEnd(1);
        while (waitpid(guardian, nullptr, 0) < 0 && errno == EINTR) {}
        throw;
    }
}
}
