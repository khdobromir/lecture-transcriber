#include "progress.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include "platform.hpp"

namespace transcribe {
double weighted_progress(const std::vector<PartProgress>& parts) {
    long double done = 0, total = 0;
    for (const auto& part : parts) if (part.samples > 0) {
        total += part.samples;
        done += static_cast<long double>(part.samples) * std::clamp(part.percent, 0, 100) / 100;
    }
    return total > 0 ? static_cast<double>(done / total) : 0;
}
std::optional<double> Eta::update(double fraction, Clock::time_point now) {
    if (!std::isfinite(fraction)) return {};
    fraction = std::clamp(fraction, fraction_, 1.0);
    if (fraction > fraction_) { ++updates_; fraction_ = fraction; }
    if (fraction >= 1) return 0;
    const double elapsed = std::chrono::duration<double>(now - start_).count();
    if (elapsed < 5 || updates_ < 2 || fraction <= 0) return {};
    const double remaining = elapsed * (1 - fraction) / fraction;
    estimate_ = estimate_ ? *estimate_ * 0.8 + remaining * 0.2 : remaining;
    return estimate_;
}
std::string eta_text(std::optional<double> seconds) {
    if (!seconds || !std::isfinite(*seconds)) return "ETA: расчёт…";
    const auto value = static_cast<int64_t>(std::clamp(std::ceil(*seconds), 0.0, 359999.0));
    std::ostringstream out;
    out << "ETA ≈ ";
    if (value >= 3600) out << value / 3600 << ':';
    out << std::setfill('0') << std::setw(2) << value / 60 % 60 << ':' << std::setw(2) << value % 60;
    return out.str();
}
Progress::Progress(bool enabled) : enabled_(enabled), terminal_(terminal_output()) {
    const char* term = std::getenv("TERM");
    if (term && std::string_view(term) == "dumb") terminal_ = false;
}
Progress::~Progress() { finish(); }
void Progress::finish() {
    if (line_) { std::cout << '\n' << std::flush; line_ = false; }
}
void Progress::update(double fraction, size_t finished, size_t total) { // NOLINT(bugprone-easily-swappable-parameters): progress fraction followed by completed/total counts.
    if (!enabled_) return;
    const auto now = Eta::Clock::now();
    const int percent = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 100);
    if (last_percent_ >= 0 && (terminal_ ? finished != total && now - last_ < std::chrono::milliseconds(200)
                                                        : percent <= last_percent_ && finished == last_finished_)) return;
    last_ = now; last_percent_ = percent; last_finished_ = finished;
    const auto remaining = eta_.update(fraction, now);
    if (terminal_) {
        const int columns = terminal_columns();
        const auto status = std::to_string(percent) + "% | " + std::to_string(finished) + '/' + std::to_string(total) + " | " + eta_text(remaining);
        std::cout << "\r\033[K";
        if (columns >= 65) {
            const int width = std::clamp(columns - 57, 5, 40), filled = width * percent / 100;
            std::cout << "Распознавание [" << std::string(static_cast<size_t>(filled), '#')
                      << std::string(static_cast<size_t>(width - filled), '-') << "] " << status;
        } else if (columns >= 42) std::cout << status;
        else std::cout << percent << '%';
        line_ = true;
    } else std::cout << "Распознавание: " << percent << "%; завершено частей " << finished << '/' << total << "; " << eta_text(remaining) << '\n';
    std::cout.flush();
}
}
