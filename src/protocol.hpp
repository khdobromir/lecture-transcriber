#pragma once
#include "pipeline.hpp"
#include <nlohmann/json.hpp>

namespace transcribe {
inline constexpr int protocol_version = 1;
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
