#include "protocol.hpp"
#include "platform.hpp"
#include "process.hpp"
#include <iostream>

namespace transcribe {
nlohmann::json event_json(const Event& event) {
    constexpr const char* names[]{"stage", "result", "progress", "warning", "finalizing", "completed", "failed"};
    nlohmann::json result{{"protocol", protocol_version}, {"type", names[static_cast<unsigned>(event.type)]},
        {"stage", event.stage}, {"message", event.message}, {"result", path_utf8(event.result)}};
    if (event.type == EventType::progress) {
        result["fraction"] = event.fraction; result["finished"] = event.finished; result["total"] = event.total;
        result["eta_seconds"] = event.eta ? nlohmann::json(*event.eta) : nlohmann::json(nullptr);
    }
    if (event.type == EventType::completed || event.type == EventType::failed) { result["status"] = event.status; result["code"] = event.code; }
    return result;
}
void write_machine_event(const Event& event) {
    std::cout << event_json(event).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n' << std::flush;
    if (!std::cout && !cancellation_token().completed()) {
        cancellation_token().request(13); check_cancelled();
    }
}
MachineControl::MachineControl() {
    std::cout << nlohmann::json{{"type", "hello"}, {"protocol", protocol_version}}.dump() << '\n' << std::flush;
    cancellation_token().set_poll([this] { poll(); });
    if (!std::cout) { cancellation_token().request(13); check_cancelled(); }
}
MachineControl::~MachineControl() { cancellation_token().set_poll({}); }
void MachineControl::poll() {
    if (!read_control(pending_)) cancellation_token().request(13);
    // Cancel commands are tiny. Bound malformed input independently of newlines.
    if (pending_.size() > 8192) { cancellation_token().request(13); pending_.clear(); return; }
    for (auto end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n')) {
        const auto command = nlohmann::json::parse(pending_.substr(0, end), nullptr, false);
        pending_.erase(0, end + 1);
        if (command.is_object() && command.contains("type") && command["type"].is_string() && command["type"] == "cancel") cancellation_token().request(2);
        else { cancellation_token().request(13); return; }
    }
}
}
