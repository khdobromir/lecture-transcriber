#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace transcribe {
std::filesystem::path utf8_path(std::string_view value);
std::string path_utf8(const std::filesystem::path& value);
std::filesystem::path user_home();
std::filesystem::path app_home();
std::filesystem::path executable_directory();
std::string tool_path(std::string_view name);
bool executable_file(const std::filesystem::path& path);
int logical_cpus();
bool terminal_output();
int terminal_columns();
// Creation is exclusive; false means an existing directory, never a replaced one.
bool create_private_directory(const std::filesystem::path& path);
void require_private_directory(const std::filesystem::path& path);
std::filesystem::path temporary_directory(const std::filesystem::path& parent, std::string_view prefix);
bool indirect_path(const std::filesystem::path& path);
void create_private_file(const std::filesystem::path& path);
void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination);
bool rename_directory(const std::filesystem::path& source, const std::filesystem::path& destination);
class FileLock {
public:
    explicit FileLock(const std::filesystem::path& path, std::function<void()> cancellation = {});
    ~FileLock();
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
// Reads available bytes without waiting. Returns false on control-channel EOF.
bool read_control(std::string& pending);
}
