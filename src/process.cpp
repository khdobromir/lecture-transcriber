#include "process.hpp"
#include "cancellation.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <set>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;
namespace transcribe {
std::size_t command_line_size(const std::vector<std::string>& args) {
    std::size_t size = 0;
    for (const auto& argument : args) size += argument.size() + 1;
    return size;
}
bool command_line_fits(const std::vector<std::string>& args) { return command_line_size(args) <= 32767; }
namespace {
void on_signal(int signal) {
    cancellation_token().request(signal);
}
void close_fd(int& fd) { if (fd >= 0) { close(fd); fd = -1; } }
void system_error(const char* name, int error) {
    throw std::runtime_error(std::string(name) + ": " + std::strerror(error));
}
}
void install_signal_handlers() {
    cancellation_token().reset();
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) sigaddset(&action.sa_mask, signal);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) {
        if (sigaction(signal, &action, nullptr) != 0) system_error("sigaction", errno);
    }
    // Reap grandchildren when a tool exits before its helpers.
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) system_error("prctl", errno);
}
int physical_cpus() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0)
        return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 256u));
    std::set<std::pair<int, int>> cores;
    bool topology = true;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &mask)) continue;
        const auto root = std::filesystem::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu)) / "topology";
        int package{}, core{};
        if (!(std::ifstream(root / "physical_package_id") >> package) ||
            !(std::ifstream(root / "core_id") >> core)) topology = false;
        else cores.emplace(package, core);
    }
    return std::clamp(topology ? static_cast<int>(cores.size()) : CPU_COUNT(&mask), 1, 256);
}

Process::Process(const std::vector<std::string>& args, const std::filesystem::path& log,
                 Output output, Output errors)
    : name_(args.at(0)), log_path_(log), log_(log, std::ios::app),
      output_(std::move(output)), errors_(std::move(errors)) {
    log_.exceptions(std::ios::badbit | std::ios::failbit);
    check_cancelled();
    int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1};
    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    bool have_actions = false, have_attributes = false, blocked = false;
    sigset_t previous{}, signals{};
    const auto checked = [](int result) { if (result) system_error("posix_spawn", result); };
    try {
        if (pipe2(out_pipe, O_CLOEXEC) || pipe2(err_pipe, O_CLOEXEC)) system_error("pipe2", errno);
        checked(posix_spawn_file_actions_init(&actions)); have_actions = true;
        checked(posix_spawnattr_init(&attributes)); have_attributes = true;
        checked(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0));
        checked(posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO));
        checked(posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO));
        // Tools must not inherit other workers' logs or partial-result streams.
        // Available on supported Arch and Ubuntu glibc versions.
        checked(posix_spawn_file_actions_addclosefrom_np(&actions, 3));
        sigemptyset(&signals);
        for (int signal : {SIGINT, SIGTERM, SIGHUP}) sigaddset(&signals, signal);
        if (sigprocmask(SIG_BLOCK, &signals, &previous)) system_error("sigprocmask", errno);
        blocked = true;
        checked(posix_spawnattr_setsigmask(&attributes, &previous));
        sigaddset(&signals, SIGPIPE);
        checked(posix_spawnattr_setsigdefault(&attributes, &signals));
        checked(posix_spawnattr_setpgroup(&attributes, 0));
        checked(posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF));
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        const int error = posix_spawnp(&pid_, argv[0], &actions, &attributes, argv.data(), environ);
        if (error) {
            pid_ = -1;
            throw ProcessError("Не удалось запустить " + name_ + ": " + std::strerror(error), 127);
        }
        out_ = out_pipe[0]; out_pipe[0] = -1;
        err_ = err_pipe[0]; err_pipe[0] = -1;
        if (fcntl(out_, F_SETFL, O_NONBLOCK) || fcntl(err_, F_SETFL, O_NONBLOCK)) system_error("fcntl", errno);
        checked(sigprocmask(SIG_SETMASK, &previous, nullptr)); blocked = false;
        posix_spawnattr_destroy(&attributes); have_attributes = false;
        posix_spawn_file_actions_destroy(&actions); have_actions = false;
        close_fd(out_pipe[1]); close_fd(err_pipe[1]);
    } catch (...) {
        if (pid_ > 0) { kill(-pid_, SIGKILL); while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {} }
        if (blocked) sigprocmask(SIG_SETMASK, &previous, nullptr);
        if (have_attributes) posix_spawnattr_destroy(&attributes);
        if (have_actions) posix_spawn_file_actions_destroy(&actions);
        for (int& fd : out_pipe) close_fd(fd);
        for (int& fd : err_pipe) close_fd(fd);
        close_fd(out_); close_fd(err_);
        throw;
    }
}
bool Process::group_alive() const { return !released_ && pid_ > 0 && (kill(-pid_, 0) == 0 || errno == EPERM); }
void Process::request_stop(int signal) {
    if (released_ || pid_ < 0) return;
    if (!stopping_) {
        stopping_ = true;
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        if (group_alive()) kill(-pid_, signal);
    }
    if (cancellation_token().repeated() || signal == SIGKILL) { kill(-pid_, SIGKILL); killed_ = true; }
}
void Process::drain(int& fd, const Output& callback, bool callbacks) {
    std::array<char, 8192> bytes{};
    // Bound each turn so a noisy worker cannot starve cancellation or others.
    for (int count = 0; fd >= 0 && count < 16; ++count) {
        const ssize_t n = read(fd, bytes.data(), bytes.size());
        if (n > 0) {
            const std::string_view text(bytes.data(), static_cast<size_t>(n));
            if (callbacks && (!callback || fd == err_)) { log_.write(text.data(), static_cast<std::streamsize>(text.size())); log_.flush(); }
            if (callback && callbacks) callback(text);
        } else if (n == 0) close_fd(fd);
        else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) system_error("read", errno);
        else break;
    }
}
void Process::tick(bool callbacks) {
    drain(out_, output_, callbacks);
    // Diagnostics always go to disk, even when a progress callback is present.
    drain(err_, errors_, callbacks);
    if (!reaped_) {
        const pid_t result = waitpid(pid_, &status_, WNOHANG);
        if (result == pid_) reaped_ = true;
        else if (result < 0 && errno != EINTR) system_error("waitpid", errno);
    }
    if (reaped_) {
        // A remaining helper must not keep pipes or a process group alive forever.
        while (waitpid(-pid_, nullptr, WNOHANG) > 0) {}
        if (group_alive() && !stopping_) request_stop(SIGTERM);
    }
    if (stopping_ && !killed_ && (cancellation_token().repeated() || std::chrono::steady_clock::now() >= deadline_)) {
        kill(-pid_, SIGKILL); killed_ = true;
    }
    if (reaped_ && out_ < 0 && err_ < 0 && !group_alive()) released_ = true;
}
bool Process::done() const { return released_; }
int Process::exit_code() const {
    if (!reaped_) throw std::logic_error("Процесс ещё выполняется");
    return WIFSIGNALED(status_) ? 128 + WTERMSIG(status_) : (WIFEXITED(status_) ? WEXITSTATUS(status_) : 1);
}
void Process::require_success() const {
    if (exit_code()) throw ProcessError(name_ + " завершился с кодом " + std::to_string(exit_code()) +
                                       ". Журнал: " + log_path_.string(), exit_code());
}
Process::~Process() {
    if (pid_ > 0) {
        // Destruction is a last-resort cleanup; never invoke throwing callbacks.
        try {
            if (!done()) request_stop(SIGTERM);
            while (!done()) { tick(false); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        } catch (...) {
            kill(-pid_, SIGKILL);
            if (!reaped_) while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {}
            while (waitpid(-pid_, nullptr, WNOHANG) > 0) {}
        }
    }
    close_fd(out_); close_fd(err_);
}
void stop_all(std::vector<std::unique_ptr<Process>>& processes, int signal) {
    for (auto& process : processes) if (process && !process->done()) process->request_stop(signal);
    std::vector<bool> usable(processes.size(), true);
    bool waiting;
    do {
        waiting = false;
        for (size_t i = 0; i < processes.size(); ++i) if (auto& process = processes[i]; process && !process->done()) {
            // Preserve callbacks (and already received text) when possible.
            try { process->tick(usable[i]); } catch (...) { usable[i] = false; process->request_stop(SIGKILL); }
            waiting = true;
        }
        if (waiting) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (waiting);
}
void run(const std::vector<std::string>& args, const std::filesystem::path& log,
         Output output, Output errors, std::chrono::seconds timeout) {
    std::vector<std::unique_ptr<Process>> processes;
    processes.push_back(std::make_unique<Process>(args, log, std::move(output), std::move(errors)));
    const auto start = std::chrono::steady_clock::now();
    try {
        while (!processes[0]->done()) {
            check_cancelled();
            if (timeout.count() && std::chrono::steady_clock::now() - start >= timeout)
                throw ProcessError("Превышено время ожидания " + args[0], 1);
            processes[0]->tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check_cancelled();
        processes[0]->require_success();
    } catch (...) {
        stop_all(processes, cancellation_signal() ? cancellation_signal() : SIGTERM);
        throw;
    }
}
}
