#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace transcribe {
// Both variants are produced before presentation; no process-global locale or Qt in core.
struct Message {
    std::string ru, en;
    Message() = default;
    Message(std::string russian, std::string english) : ru(std::move(russian)), en(std::move(english)) {}
    Message(std::string raw) : ru(std::move(raw)), en(ru) {}
    Message(const char* raw) : Message(std::string(raw)) {}
    operator std::string_view() const { return ru; }
};
class LocalizedError : public std::runtime_error {
public:
    explicit LocalizedError(Message message) : std::runtime_error(message.ru), message_(std::move(message)) {}
    const Message& message() const { return message_; }

private:
    Message message_;
};
inline Message exception_message(const std::exception& error) {
    if (const auto* localized = dynamic_cast<const LocalizedError*>(&error)) return localized->message();
    // OS/library diagnostics are technical details and must remain verbatim.
    return {error.what(), error.what()};
}
} // namespace transcribe
