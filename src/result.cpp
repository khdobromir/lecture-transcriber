#include "result.hpp"
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace transcribe {
fs::path default_output() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("Не задан HOME; укажи --out КАТАЛОГ");
    return fs::absolute(fs::path(home) / "Transcriptions");
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
            (valid && length == 2 && ch == 0xc2 && static_cast<unsigned char>(title[i + 1]) <= 0x9f);
        if (result.size() + (replace ? 1 : length) > 180) break;
        if (replace) result += '_'; else result.append(title.substr(i, length));
        i += length;
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
    const auto start = result.find_first_not_of(" .");
    if (start == std::string::npos) return "video";
    result.erase(0, start);
    return result;
}
ResultPaths ResultPaths::create(const fs::path& parent, std::string_view title, std::chrono::system_clock::time_point now) {
    fs::create_directories(parent);
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (!localtime_r(&time, &local)) throw std::runtime_error("Не удалось определить дату запуска");
    std::ostringstream date;
    date << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
    ResultPaths paths{{}, date.str()};
    const auto base = safe_title(title) + '_' + paths.date;
    for (unsigned suffix = 1;; ++suffix) {
        const auto candidate = parent / (base + (suffix == 1 ? "" : '_' + std::to_string(suffix)));
        if (mkdir(candidate.c_str(), 0700) == 0) { paths.root = candidate; break; }
        if (errno != EEXIST) throw std::runtime_error("Не удалось создать каталог результата: " + candidate.string());
    }
    for (const auto& dir : {paths.logs(), paths.transcripts(), paths.work()}) fs::create_directory(dir);
    return paths;
}
void ResultPaths::name(std::string_view title) {
    const auto base = safe_title(title) + '_' + date;
    for (unsigned suffix = 1;; ++suffix) {
        const auto candidate = root.parent_path() / (base + (suffix == 1 ? "" : '_' + std::to_string(suffix)));
        if (candidate == root) return;
        // Linux no-replace rename also protects against a concurrently created empty directory.
        if (renameat2(AT_FDCWD, root.c_str(), AT_FDCWD, candidate.c_str(), RENAME_NOREPLACE) == 0) {
            root = candidate; return;
        }
        if (errno != EEXIST) throw std::runtime_error("Не удалось переименовать каталог результата: " + candidate.string());
    }
}
}
