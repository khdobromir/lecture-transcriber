#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace transcribe {
struct ProcessError : std::runtime_error {
    int code;
    ProcessError(const std::string& message, int value) : std::runtime_error(message), code(value) {}
};
using Output = std::function<void(std::string_view)>;
void install_signal_handlers();
int cancellation_signal();
void check_cancelled();
// After exports and success metadata are saved, signals cannot cancel cleanup.
// A cancellation received before this transition still throws.
void commit_completion();
int physical_cpus();
// Includes resolved executable, quoting and terminating NUL on Windows.
std::size_t command_line_size(const std::vector<std::string>& args);
bool command_line_fits(const std::vector<std::string>& args);

// Each child owns a process group, two drained pipes and a diagnostic log.
// Callbacks receive bytes, not necessarily complete UTF-8 characters or lines.
class Process {
public:
    Process(const std::vector<std::string>& args, const std::filesystem::path& log,
            Output output = {}, Output errors = {});
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    void tick(bool callbacks = true);
    void request_stop(int signal);
    bool done() const;
    int exit_code() const;
    void require_success() const;
private:
#ifdef _WIN32
    struct Native;
    std::unique_ptr<Native> native_;
#else
    void drain(int& fd, const Output& callback, bool callbacks);
    bool group_alive() const;
    int pid_ = -1;
    int group_ = -1, lease_ = -1;
    int out_ = -1, err_ = -1, status_ = 0;
    bool reaped_ = false, stopping_ = false, killed_ = false, released_ = false;
    std::chrono::steady_clock::time_point deadline_{};
#endif
    std::string name_;
    std::filesystem::path log_path_;
    std::ofstream log_;
    Output output_, errors_;
};
void stop_all(std::vector<std::unique_ptr<Process>>& processes, int signal);
void run(const std::vector<std::string>& args, const std::filesystem::path& log,
         Output output = {}, Output errors = {}, std::chrono::seconds timeout = {});
}
