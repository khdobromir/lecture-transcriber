#include "protocol.hpp"
#include "platform.hpp"
#include <iostream>
#include <limits>

using namespace transcribe;
using Json = nlohmann::json;
namespace {
void require(bool ok) { if (!ok) throw std::runtime_error("protocol regression failed"); }
bool good(ProtocolParser& parser, const Json& value) { return std::holds_alternative<ProtocolMessage>(parser.accept(value.dump())); }
bool bad(ProtocolParser& parser, const Json& value) { return std::holds_alternative<ProtocolProblem>(parser.accept(value.dump())); }
Json greeting() { return Json{{"type", "hello"}, {"protocol", 1}}; }
const std::filesystem::path& result_directory() {
    static const auto path = std::filesystem::temp_directory_path() / utf8_path("Лекция 😀");
    return path;
}
Json serialized(EventType type, std::string stage = {}) { return event_json(Event{.type = type, .stage = std::move(stage), .result = result_directory()}); }
void ready(ProtocolParser& parser) {
    require(good(parser, greeting())); require(good(parser, serialized(EventType::result)));
    require(good(parser, serialized(EventType::stage, "recognize")));
}
Json progress() { return event_json(Event{.type = EventType::progress, .stage = "recognize", .result = result_directory(), .fraction = 0.5, .finished = 1, .total = 2, .eta = 30.0}); }
void vectors() {
    for (const auto* field : {"protocol", "type", "stage", "message", "result", "fraction", "finished", "total", "eta_seconds"}) {
        ProtocolParser parser; ready(parser); auto json = progress(); json.erase(field); require(bad(parser, json));
    }
    for (const auto* field : {"protocol", "type", "stage", "message", "result", "fraction", "finished", "total", "eta_seconds"}) {
        ProtocolParser parser; ready(parser); auto json = progress(); json[field] = Json::array(); require(bad(parser, json));
    }
    for (const auto& [field, value] : std::vector<std::pair<std::string, Json>>{
        {"fraction", -0.1}, {"fraction", 1.1}, {"fraction", nullptr}, {"fraction", "NaN"},
        {"finished", 3}, {"finished", -1}, {"finished", 0.5}, {"total", 0}, {"eta_seconds", -1.0},
        {"protocol", 1.1}, {"type", "unknown"}, {"stage", "merge"}, {"result", "relative"}}) {
        ProtocolParser parser; ready(parser); auto json = progress(); json[field] = value; require(bad(parser, json));
    }
    ProtocolParser parser; require(bad(parser, progress())); ready(parser);
    require(bad(parser, greeting())); require(good(parser, progress()));
    auto json = progress(); json["fraction"] = 0.4; require(bad(parser, json));
    json = progress(); json["finished"] = 0; require(bad(parser, json));
    json = progress(); json["total"] = 3; require(bad(parser, json));
    json = progress(); json["future_extension"] = Json{{"status", "failed"}, {"result", "untrusted"}}; require(good(parser, json));
    auto terminal = event_json(Event{.type = EventType::completed, .status = "completed", .result = result_directory()});
    json = terminal; json["type"] = "failed"; require(bad(parser, json));
    json = terminal; json["status"] = "failed"; require(bad(parser, json));
    json = terminal; json["code"] = 1; require(bad(parser, json));
    require(good(parser, terminal)); require(bad(parser, terminal)); require(bad(parser, progress()));
    ProtocolParser cancelled; ready(cancelled);
    json = event_json(Event{.type = EventType::failed, .status = "interrupted", .result = result_directory(), .code = 130});
    require(good(cancelled, json));
    ProtocolParser invalidCancel; ready(invalidCancel); json["code"] = 1; require(bad(invalidCancel, json));
    ProtocolParser preflight; require(good(preflight, greeting()));
    require(good(preflight, event_json(Event{.type = EventType::failed, .status = "failed", .code = 1})));
    ProtocolParser nativeCrash; ready(nativeCrash);
    require(good(nativeCrash, event_json(Event{.type = EventType::failed, .status = "failed", .result = result_directory(), .code = -1073741819})));
    std::cout << "PASS required fields, ranges, sequence, terminal and extensions\n";
}
void pathsAndProperties() {
    ProtocolParser parser; require(good(parser, greeting()));
    auto temporary = serialized(EventType::result, "temporary"); temporary["result"] = path_utf8(result_directory() / "temporary");
    require(good(parser, temporary));
    auto stage = serialized(EventType::stage, "probe"); stage["result"] = temporary["result"]; require(good(parser, stage));
    require(good(parser, serialized(EventType::result)));
    require(bad(parser, temporary));
    auto changed = serialized(EventType::result); changed["result"] = path_utf8(result_directory() / "other"); require(bad(parser, changed));
    for (std::size_t total = 1; total <= 256; ++total) {
        ProtocolParser monotonic; ready(monotonic);
        for (std::size_t finished = 0; finished <= total; ++finished) {
            auto json = progress(); json["total"] = total; json["finished"] = finished;
            json["fraction"] = static_cast<double>(finished) / static_cast<double>(total); require(good(monotonic, json));
        }
    }
    ProtocolParser bounded;
    auto oversized = greeting(); oversized["extension"] = std::string(max_event_line, 'x'); require(bad(bounded, oversized));
    require(std::holds_alternative<ProtocolProblem>(bounded.accept("{\"protocol\":1,\"type\":\"hello\",\"bad\":NaN}")));
    require(std::holds_alternative<ProtocolProblem>(bounded.accept(std::string("{\"type\":\"hello\",\"protocol\":1,\"x\":\"") + '\xff' + "\"}")));
    require(good(bounded, greeting()));
    for (const auto* field : {"code", "status"}) {
        ProtocolParser missing; ready(missing);
        auto terminal = event_json(Event{.type = EventType::completed, .status = "completed", .result = result_directory()});
        terminal.erase(field); require(bad(missing, terminal));
    }
    std::cout << "PASS temporary-to-final result, monotonic progress properties and size/UTF-8 limits\n";
}
}
int main() {
    try { vectors(); pathsAndProperties(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
