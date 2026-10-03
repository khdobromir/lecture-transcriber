#pragma once
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace transcribe {
struct Options {
    std::string input, model, browser, cookies, prompt;
    std::filesystem::path output, cache_dir;
    uint64_t cache_limit = uint64_t{10} * 1024 * 1024 * 1024;
    int threads = 0, chunks = 1, jobs = 0;
    bool vad = true, keep = false, progress = true, cache = true, refresh_cache = false;
};
enum class CliAction : std::uint8_t { run, help, version, usage };
// args excludes argv[0]. This scan preserves help/version precedence and skips values.
CliAction cli_action(std::span<const std::string_view> args);
Options parse_arguments(std::span<const std::string_view> args, int physical_cpus);
bool oversubscribed(const Options& options, int logical_cpus);
struct Inputs {
    std::filesystem::path engine, model, vad_model, input;
    std::string cookies;
    bool url = false;
};
// Read-only preflight; relative paths are resolved against cwd, never process-wide chdir.
struct ValidationPaths { std::filesystem::path root, cwd; };
Inputs validate_inputs(const Options& options, const ValidationPaths& paths);
std::string read_line(const std::filesystem::path& path);
void require_file(const std::filesystem::path& path, const std::string& hint);
}
