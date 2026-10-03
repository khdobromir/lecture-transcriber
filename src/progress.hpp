#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace transcribe {
struct PartProgress { int64_t samples; int percent; };
double weighted_progress(const std::vector<PartProgress>& parts);
class Eta {
public:
    using Clock = std::chrono::steady_clock;
    explicit Eta(Clock::time_point start) : start_(start) {}
    std::optional<double> update(double fraction, Clock::time_point now);
private:
    Clock::time_point start_;
    double fraction_ = 0;
    unsigned updates_ = 0;
    std::optional<double> estimate_;
};
std::string eta_text(std::optional<double> seconds);
class Progress {
public:
    explicit Progress(bool enabled);
    ~Progress();
    Progress(const Progress&) = delete;
    Progress& operator=(const Progress&) = delete;
    void update(double fraction, size_t finished, size_t total);
    void finish();
private:
    bool enabled_, terminal_, line_ = false;
    int last_percent_ = -1;
    size_t last_finished_ = 0;
    Eta::Clock::time_point last_ = Eta::Clock::time_point::min();
    Eta eta_{Eta::Clock::now()};
};
}
