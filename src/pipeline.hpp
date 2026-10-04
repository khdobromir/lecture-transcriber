#pragma once
#include <cstdint>
#include "cli.hpp"
#include "cancellation.hpp"
#include <functional>
#include <optional>

namespace transcribe {
enum class EventType : std::uint8_t { stage, result, progress, warning, finalizing, completed, failed };
struct Event {
    EventType type = EventType::stage;
    std::string stage{}, message{}, status{};
    std::filesystem::path result{};
    double fraction = 0;
    size_t finished = 0, total = 0;
    std::optional<double> eta{};
    int code = 0;
};
using EventSink = std::function<void(const Event&)>;
struct RunResult {
    std::filesystem::path directory;
    std::string status, error;
    int code = 0;
};
struct ToolPaths {
    std::filesystem::path root, cwd;
};
// One run per backend process. All UI interaction goes through the event sink.
class Pipeline {
public:
    explicit Pipeline(EventSink sink) : sink_(std::move(sink)) {}
    RunResult run(Options options, const ToolPaths& paths);
private:
    EventSink sink_;
};
}
