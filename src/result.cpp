#include "result.hpp"
#include "platform.hpp"
#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace transcribe {
fs::path default_output() {
    return user_home() / "Transcriptions";
}
std::string safe_title(std::string_view title) {
    std::string result;
    // Keep valid UTF-8 code points, without splitting the 180-byte title budget.
    for (size_t i = 0; i < title.size();) {
        const auto ch = static_cast<unsigned char>(title[i]);
        size_t length = ch < 0x80 ? 1 : ch >= 0xc2 && ch <= 0xdf ? 2 : ch >= 0xe0 && ch <= 0xef ? 3 : ch >= 0xf0 && ch <= 0xf4 ? 4 : 0;
        bool valid = length && i + length <= title.size();
        for (size_t j = 1; valid && j < length; ++j)
            valid = (static_cast<unsigned char>(title[i + j]) & 0xc0U) == 0x80;
        if (valid && length >= 3) {
            const auto next = static_cast<unsigned char>(title[i + 1]);
            valid = !(ch == 0xe0 && next < 0xa0) && !(ch == 0xed && next >= 0xa0) &&
                    !(ch == 0xf0 && next < 0x90) && !(ch == 0xf4 && next >= 0x90);
        }
        if (!valid) length = 1;
        const bool replace = !valid || ch < 32 || ch == 127 || ch == '/' || ch == '\\' ||
#ifdef _WIN32
            ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*' ||
#endif
            (valid && length == 2 && ch == 0xc2 && static_cast<unsigned char>(title[i + 1]) <= 0x9f);
        if (result.size() + (replace ? 1 : length) > 180) break;
        if (replace) result += '_'; else result.append(title.substr(i, length));
        i += length;
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
    const auto start = result.find_first_not_of(" .");
    if (start == std::string::npos) return "video";
    result.erase(0, start);
#ifdef _WIN32
    auto stem = result.substr(0, result.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' && stem[3] <= '9')) result.insert(0, "_");
#endif
    return result;
}
void validate_result_parent(const fs::path& parent) {
#ifdef _WIN32
    if (fs::absolute(parent).native().size() > 169)
        throw std::runtime_error("Каталог результатов слишком длинный: выберите более короткий путь (до 169 UTF-16 единиц)");
#else
    (void)parent;
#endif
}
namespace {
std::string result_title(const fs::path& parent, std::string_view title) {
    validate_result_parent(parent);
    auto result = safe_title(title);
#ifdef _WIN32
    // Keep space for timestamps, collision suffixes and the deepest tool export.
    // Some bundled tool paths still have stricter limits than our long-path manifest.
    const auto parentLength = fs::absolute(parent).native().size();
    const auto budget = size_t{210} - parentLength - 1 - 20 - 12;
    while (utf8_path(result).native().size() > budget) {
        auto start = result.size() - 1;
        while (start && (static_cast<unsigned char>(result[start]) & 0xc0U) == 0x80) --start;
        result.resize(start);
    }
#else
    (void)parent;
#endif
    return result.empty() ? "video" : result;
}
}
ResultPaths ResultPaths::create(const fs::path& parent, std::string_view title, std::chrono::system_clock::time_point now) {
    const auto titlePart = result_title(parent, title);
    fs::create_directories(parent);
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time) != 0)
#else
    if (!localtime_r(&time, &local))
#endif
        throw std::runtime_error("Не удалось определить дату запуска");
    std::ostringstream date;
    date << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
    ResultPaths paths{{}, date.str()};
    const auto base = titlePart + '_' + paths.date;
    for (unsigned suffix = 1;; ++suffix) {
        const auto candidate = parent / utf8_path(base + (suffix == 1 ? "" : '_' + std::to_string(suffix)));
        if (create_private_directory(candidate)) { paths.root = candidate; break; }
    }
    for (const auto& dir : {paths.logs(), paths.transcripts(), paths.work()}) fs::create_directory(dir);
    return paths;
}
void ResultPaths::name(std::string_view title) {
    const auto base = result_title(root.parent_path(), title) + '_' + date;
    for (unsigned suffix = 1;; ++suffix) {
        const auto candidate = root.parent_path() / utf8_path(base + (suffix == 1 ? "" : '_' + std::to_string(suffix)));
        if (candidate == root) return;
        // Linux no-replace rename also protects against a concurrently created empty directory.
        if (rename_directory(root, candidate)) {
            root = candidate; return;
        }
    }
}
}
