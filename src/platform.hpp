#pragma once
#include <filesystem>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace transcribe {
std::filesystem::path utf8_path(std::string_view value);
std::string path_utf8(const std::filesystem::path& value);
std::string environment_utf8(std::string_view name);
std::filesystem::path user_home();
std::filesystem::path app_home();
std::filesystem::path executable_directory();
bool portable_bundle(const std::filesystem::path& tools);
std::string tool_path(std::string_view name, const std::filesystem::path& tools = {});
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
void replace_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                  const std::function<void()>& cancellation = {});
// A bounded reader whose open handle permits atomic replacement by publishers.
class SharedReader {
public:
    explicit SharedReader(const std::filesystem::path& path);
    ~SharedReader();
    SharedReader(const SharedReader&) = delete;
    SharedReader& operator=(const SharedReader&) = delete;
    std::uint64_t size() const;
    std::string read(std::uint64_t offset, std::size_t limit);
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
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
