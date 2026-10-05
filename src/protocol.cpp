#include "protocol.hpp"
#include "platform.hpp"
#include "process.hpp"
#include <iostream>
#include <cmath>
#include <limits>

namespace transcribe {
namespace {
bool known_stage(std::string_view stage) {
    return stage == "local" || stage == "probe" || stage == "cache" || stage == "download" ||
        stage == "prepare" || stage == "split" || stage == "recognize" || stage == "merge";
}
bool count(const nlohmann::json& value) {
    return value.is_number_integer() && (value.is_number_unsigned() || value.get<std::int64_t>() >= 0) &&
        value.get<std::uint64_t>() <= std::numeric_limits<std::size_t>::max();
}
bool exit_code(const nlohmann::json& value) {
    return value.is_number_integer() && value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max();
}
}
ProtocolResult ProtocolParser::accept(std::string_view line) {
    const auto fail = [](ProtocolFault fault, std::string message) -> ProtocolResult { return ProtocolProblem{fault, std::move(message)}; };
    if (line.size() > max_event_line) return fail(ProtocolFault::size, "Слишком большое событие backend");
    const auto json = nlohmann::json::parse(line, nullptr, false);
    if (!json.is_object()) return fail(ProtocolFault::json, "Некорректное JSON-событие backend");
    const auto version = json.find("protocol");
    if (version == json.end() || !version->is_number_integer() || *version != protocol_version)
        return fail(ProtocolFault::version, "Несовместимая версия протокола backend");
    const auto typeValue = json.find("type");
    if (typeValue == json.end() || !typeValue->is_string()) return fail(ProtocolFault::field, "Отсутствует тип события backend");
    const auto type = typeValue->get<std::string>();
    if (terminal_) return fail(ProtocolFault::sequence, "Событие backend после итогового статуса");
    if (!hello_) {
        if (type != "hello") return fail(ProtocolFault::sequence, "Backend не выполнил проверку протокола");
        hello_ = true; return ProtocolMessage{true, {}};
    }
    if (type == "hello") return fail(ProtocolFault::sequence, "Повторный hello backend");
    ProtocolMessage message;
    auto& event = message.event;
    if (type == "stage") event.type = EventType::stage;
    else if (type == "result") event.type = EventType::result;
    else if (type == "progress") event.type = EventType::progress;
    else if (type == "warning") event.type = EventType::warning;
    else if (type == "finalizing") event.type = EventType::finalizing;
    else if (type == "completed") event.type = EventType::completed;
    else if (type == "failed") event.type = EventType::failed;
    else return fail(ProtocolFault::field, "Неизвестный тип события backend");
    for (const auto* field : {"stage", "message", "result"}) {
        const auto value = json.find(field);
        if (value == json.end() || !value->is_string()) return fail(ProtocolFault::field, std::string("Некорректное поле backend: ") + field);
    }
    event.stage = json["stage"].get<std::string>(); event.message = json["message"].get<std::string>();
    const auto path = json["result"].get<std::string>();
    if (path.find('\0') != path.npos) return fail(ProtocolFault::directory, "Некорректный путь результата backend");
    try { event.result = utf8_path(path); }
    catch (const std::exception&) { return fail(ProtocolFault::directory, "Некорректный путь результата backend"); }
    if (!event.result.empty() && !event.result.is_absolute()) return fail(ProtocolFault::directory, "Backend передал относительный путь результата");
    const bool isTerminal = event.type == EventType::completed || event.type == EventType::failed;
    if (event.result.empty() && event.type != EventType::failed)
        return fail(ProtocolFault::directory, "Событие backend без каталога результата");
    if (event.type == EventType::result) {
        if (event.result.empty() || (event.stage != "" && event.stage != "temporary"))
            return fail(ProtocolFault::directory, "Некорректное объявление каталога результата");
        if ((final_directory_ && (event.result != directory_ || event.stage == "temporary")) ||
            (!directory_.empty() && !final_directory_ && event.stage == "temporary"))
            return fail(ProtocolFault::directory, "Недопустимая смена каталога результата");
    } else if (event.result != directory_ && !(event.type == EventType::failed && directory_.empty())) {
        return fail(ProtocolFault::directory, "Событие backend относится к другому результату");
    }
    if (event.type == EventType::stage && !known_stage(event.stage)) return fail(ProtocolFault::field, "Неизвестная стадия backend");
    if (event.type == EventType::progress) {
        if (event.stage != "recognize" || stage_ != "recognize") return fail(ProtocolFault::progress, "Прогресс вне стадии распознавания");
        const auto fraction = json.find("fraction"), finished = json.find("finished"), total = json.find("total"), eta = json.find("eta_seconds");
        if (fraction == json.end() || !fraction->is_number() || finished == json.end() || !count(*finished) ||
            total == json.end() || !count(*total) || eta == json.end()) return fail(ProtocolFault::progress, "Некорректные поля прогресса backend");
        event.fraction = fraction->get<double>(); event.finished = finished->get<std::size_t>(); event.total = total->get<std::size_t>();
        if (!std::isfinite(event.fraction) || event.fraction < 0 || event.fraction > 1 || !event.total || event.finished > event.total ||
            event.fraction < fraction_ || event.finished < finished_ || (total_ && event.total != total_))
            return fail(ProtocolFault::progress, "Несогласованный прогресс backend");
        if (!eta->is_null()) {
            if (!eta->is_number()) return fail(ProtocolFault::progress, "Некорректная оценка времени backend");
            event.eta = eta->get<double>();
            if (!std::isfinite(*event.eta) || *event.eta < 0) return fail(ProtocolFault::progress, "Некорректная оценка времени backend");
        }
    }
    if (isTerminal) {
        const auto status = json.find("status"), code = json.find("code");
        if (status == json.end() || !status->is_string() || code == json.end() || !exit_code(*code))
            return fail(ProtocolFault::terminal, "Некорректный итоговый статус backend");
        event.status = status->get<std::string>(); event.code = code->get<int>();
        if ((event.type == EventType::completed && (event.status != "completed" || event.code != 0 || !final_directory_)) ||
            (event.type == EventType::failed && (event.code == 0 || (event.status != "failed" && event.status != "interrupted") ||
                (event.status == "interrupted" && (event.code < 129 || event.code > 192))))) return fail(ProtocolFault::terminal, "Противоречивый итоговый статус backend");
    }
    // Commit parser state only after the whole event has passed validation.
    if (event.type == EventType::result) { directory_ = event.result; final_directory_ = event.stage.empty(); }
    if (event.type == EventType::stage) {
        if (event.stage != stage_) { fraction_ = -1; finished_ = total_ = 0; }
        stage_ = event.stage;
    }
    if (event.type == EventType::progress) { fraction_ = event.fraction; finished_ = event.finished; total_ = event.total; }
    terminal_ = isTerminal;
    return message;
}
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
