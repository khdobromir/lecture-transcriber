#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace transcribe {
std::filesystem::path default_output();
void validate_result_parent(const std::filesystem::path& parent);
std::string safe_title(std::string_view title);
struct ResultPaths {
    std::filesystem::path root;
    std::string date;
    std::int64_t created = 0;
    std::filesystem::path logs() const { return root / "logs"; }
    std::filesystem::path transcripts() const { return root / "transcripts"; }
    std::filesystem::path work() const { return root / "audio"; }
    std::filesystem::path staging() const { return work() / "final"; }
    static ResultPaths create(const std::filesystem::path& parent, std::string_view title,
                             std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
    void name(std::string_view title);
};
}
