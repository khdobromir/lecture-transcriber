#include "platform.hpp"
#include <atomic>
#include <cstdlib>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;
namespace transcribe {
fs::path utf8_path(std::string_view value) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size())); }
std::string path_utf8(const fs::path& value) {
    const auto bytes = value.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
fs::path app_home() {
#ifndef _WIN32
    if (const char* value = std::getenv("TRANSCRIBE_HOME"); value && *value) return fs::absolute(utf8_path(value));
    return user_home() / ".local/share/transcribe";
#else
    // getenv() is tied to the ANSI codepage; read environment paths through Win32.
    extern fs::path windows_environment_path(const wchar_t* name);
    const auto configured = windows_environment_path(L"TRANSCRIBE_HOME");
    if (!configured.empty()) return fs::absolute(configured);
    const auto local = windows_environment_path(L"LOCALAPPDATA");
    if (local.empty()) throw std::runtime_error("Не задан LOCALAPPDATA или TRANSCRIBE_HOME");
    return local / "Transcribe";
#endif
}
std::string tool_path(std::string_view name) {
#ifdef _WIN32
    const auto bundled = executable_directory() / "tools" / utf8_path(std::string(name) + ".exe");
    if (executable_file(bundled)) return path_utf8(bundled);
#endif
    return std::string(name);
}
fs::path temporary_directory(const fs::path& parent, std::string_view prefix) {
    static std::atomic<unsigned long long> sequence{0};
    std::random_device entropy;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const auto name = std::string(prefix) + std::to_string(entropy()) + '-' + std::to_string(sequence++);
        const auto candidate = parent / name;
        if (create_private_directory(candidate)) return candidate;
    }
    throw std::runtime_error("Не удалось создать уникальный временный каталог");
}
}
