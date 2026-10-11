#pragma once
#include "pipeline.hpp"
#include <nlohmann/json.hpp>
#include <variant>

namespace transcribe {
inline constexpr int protocol_version = 1;
inline constexpr std::size_t max_event_line = std::size_t{256} * 1024;
enum class ProtocolFault : std::uint8_t { size, json, version, field, sequence, directory, progress, terminal };
struct ProtocolProblem { ProtocolFault fault; std::string message; };
struct ProtocolMessage { bool hello = false; Event event{}; };
using ProtocolResult = std::variant<ProtocolMessage, ProtocolProblem>;
// The serializer and all consumers share this protocol-v1 validation contract.
class ProtocolParser {
public:
    ProtocolResult accept(std::string_view line);
    bool greeted() const { return hello_; }
    bool terminal() const { return terminal_; }
private:
    bool hello_ = false, terminal_ = false, final_directory_ = false;
    std::filesystem::path directory_{};
    std::string stage_{};
    double fraction_ = -1;
    std::size_t finished_ = 0, total_ = 0;
};
nlohmann::json event_json(const Event& event);
void write_machine_event(const Event& event);
class MachineControl {
public:
    MachineControl();
    ~MachineControl();
    MachineControl(const MachineControl&) = delete;
    MachineControl& operator=(const MachineControl&) = delete;
private:
    std::string pending_;
    void poll();
};
}
