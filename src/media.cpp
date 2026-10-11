#include "media.hpp"
#include "process.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
namespace transcribe {
namespace {
bool regular(const fs::path& file) { return !indirect_path(file) && fs::is_regular_file(fs::symlink_status(file)); }
void copy_media(const fs::path& source, const fs::path& destination) {
    if (!regular(source) || fs::exists(fs::symlink_status(destination))) throw std::runtime_error("Некорректный путь копии аудио");
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("Не удалось прочитать аудио");
    std::ofstream out(destination, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    std::array<char, 65536> buffer{};
    while (in.read(buffer.data(), buffer.size()) || in.gcount()) {
        check_cancelled();
        out.write(buffer.data(), in.gcount());
    }
    if (in.bad()) throw std::runtime_error("Ошибка чтения аудио");
    out.close();
    check_cancelled();
}
std::string field(const fs::path& file) {
    if (!regular(file) || fs::file_size(file) > 16384) throw std::runtime_error("Некорректные метаданные yt-dlp: " + path_utf8(file));
    std::ifstream stream(file, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad()) throw std::runtime_error("Не удалось прочитать метаданные yt-dlp");
    if (!text.empty() && text.back() == '\n') text.pop_back();
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}
// yt-dlp interprets output *paths* as templates as well. Escape literal percent signs.
std::string template_path(const fs::path& path) {
    std::string escaped;
    for (char ch : path_utf8(path)) { escaped += ch; if (ch == '%') escaped += '%'; }
    return escaped;
}
std::vector<std::string> downloader(const Options& o) {
    std::vector<std::string> args{tool_path("yt-dlp"), "--ignore-config", "--no-cache-dir", "--no-playlist", "-f", "bestaudio/best"};
    if (!o.browser.empty()) args.insert(args.end(), {"--cookies-from-browser", o.browser});
    if (!o.cookies.empty()) args.insert(args.end(), {"--cookies", o.cookies});
    return args;
}
void private_directory(const fs::path& path) {
    fs::create_directories(path.parent_path());
    (void)create_private_directory(path);
    require_private_directory(path);
}
bool managed_directory(const fs::path& directory) {
    if (indirect_path(directory) || !fs::is_directory(fs::symlink_status(directory))) return false;
    for (const auto& file : fs::directory_iterator(directory)) {
        const auto name = file.path().filename();
        if ((name != "media" && name != "meta" && name != ".meta.tmp") || !regular(file.path())) return false;
    }
    return true;
}
}
fs::path default_cache() {
#ifdef _WIN32
    return app_home() / "cache/media";
#else
    if (const char* value = std::getenv("XDG_CACHE_HOME"); value && *value && fs::path(value).is_absolute())
        return fs::path(value) / "transcribe/media";
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("Не задан HOME; укажи --cache-dir или --no-cache");
    return fs::absolute(fs::path(home) / ".cache/transcribe/media");
#endif
}
std::string authentication_context(const Options& o) {
    if (!o.cookies.empty()) {
        // Cookie contents are never copied to the cache. Changing the cookie file partitions hits.
        const auto stamp = fs::last_write_time(utf8_path(o.cookies)).time_since_epoch();
        // libc++ may use a 128-bit filesystem clock. Split the timestamp without
        // narrowing the full duration or losing subsecond cache partitioning.
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(stamp);
        const auto remainder = std::chrono::duration_cast<std::chrono::nanoseconds>(stamp - seconds);
        return "cookies:" + o.cookies + ':' + std::to_string(fs::file_size(utf8_path(o.cookies))) + ':' +
            std::to_string(seconds.count()) + ':' + std::to_string(remainder.count());
    }
    return o.browser.empty() ? "anonymous" : "browser:" + o.browser;
}
SourceInfo probe_source(const Options& o, const ResultPaths& paths) {
    auto args = downloader(o);
    args.emplace_back("--simulate");
    for (const auto* key : {"title", "extractor_key", "id", "is_live"})
        args.insert(args.end(), {"--print-to-file", std::string("%(") + key + ")s", template_path(paths.work() / key)});
    args.insert(args.end(), {"--", o.input});
    run(args, paths.logs() / "metadata.log");
    return {field(paths.work() / "title"), field(paths.work() / "extractor_key"), field(paths.work() / "id"),
            field(paths.work() / "is_live") == "True"};
}
fs::path download_source(const Options& o, const ResultPaths& paths) {
    auto args = downloader(o);
    args.insert(args.end(), {"--no-simulate", "--restrict-filenames", "-o",
        template_path(paths.work() / "source.") + "%(ext)s", "--print-to-file",
        "after_move:%(filepath)s", template_path(paths.work() / "download.path"), "--", o.input});
    run(args, paths.logs() / "download.log");
    const fs::path file = utf8_path(field(paths.work() / "download.path"));
    if (!regular(file) || fs::file_size(file) == 0 || fs::canonical(file).parent_path() != fs::canonical(paths.work()))
        throw std::runtime_error("yt-dlp не сохранил медиа внутри рабочего каталога");
    return file;
}
MediaCache::MediaCache(const fs::path& root, uint64_t capacity) : root_(fs::absolute(root).lexically_normal()), capacity_(capacity) {
    // Refuse indirect paths before creating anything under a user-selected cache root.
    fs::path prefix;
    for (const auto& component : root_) {
        prefix /= component;
        if (indirect_path(prefix)) throw std::runtime_error("Symlink в пути кэша: " + path_utf8(prefix));
    }
    private_directory(root_);
    lock_ = std::make_unique<FileLock>(root_ / "lock");
    private_directory(root_ / "entries");
    prune({});
}
std::vector<MediaCache::Entry> MediaCache::entries() const {
    std::vector<Entry> result;
    for (const auto& directory : fs::directory_iterator(root_ / "entries")) {
        if (!path_utf8(directory.path().filename()).starts_with("entry-") || !managed_directory(directory.path())) continue;
        check_cancelled();
        Entry entry;
        entry.directory = directory.path();
        const auto meta = entry.directory / "meta";
        if (!regular(meta) || fs::file_size(meta) > 65536 || !regular(entry.directory / "media")) continue;
        std::ifstream in(meta, std::ios::binary);
        std::string version;
        size_t aliases{};
        if (!(in >> version >> entry.bytes >> std::quoted(entry.source.title) >> std::quoted(entry.source.extractor)
                 >> std::quoted(entry.source.id) >> std::quoted(entry.context) >> aliases) ||
            version != "transcribe-media-v1" || aliases > 64 || !entry.bytes || entry.bytes != fs::file_size(entry.directory / "media")) continue;
        for (size_t i = 0; i < aliases && in; ++i) { std::string url; in >> std::quoted(url); entry.urls.push_back(std::move(url)); }
        if (in) result.push_back(std::move(entry));
    }
    return result;
}
std::optional<MediaCache::Entry> MediaCache::lookup_url(const std::string& url, const std::string& context) const {
    for (const auto& entry : entries()) if (entry.context == context && std::ranges::find(entry.urls, url) != entry.urls.end()) return entry;
    return {};
}
std::optional<MediaCache::Entry> MediaCache::lookup_source(const SourceInfo& source, const std::string& context) const {
    if (source.live || source.id.empty() || source.extractor.empty() || source.id == "NA" || source.extractor == "NA" || source.extractor == "Generic") return {};
    for (const auto& entry : entries()) if (entry.context == context && entry.source.id == source.id && entry.source.extractor == source.extractor) return entry;
    return {};
}
void MediaCache::write_entry(const Entry& entry) const {
    std::ostringstream text;
    text << "transcribe-media-v1\n" << entry.bytes << '\n' << std::quoted(entry.source.title) << '\n'
         << std::quoted(entry.source.extractor) << '\n' << std::quoted(entry.source.id) << '\n' << std::quoted(entry.context) << '\n' << entry.urls.size() << '\n';
    for (const auto& url : entry.urls) text << std::quoted(url) << '\n';
    if (text.str().size() > 65536) throw std::runtime_error("Слишком большие метаданные кэша");
    const auto temp = entry.directory / ".meta.tmp";
    create_private_file(temp);
    try {
        std::ofstream out(temp, std::ios::binary);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << text.str(); out.close();
        replace_file(temp, entry.directory / "meta");
    } catch (...) { std::error_code error; fs::remove(temp, error); throw; }
}
void MediaCache::copy(Entry& entry, const fs::path& destination, const std::string& url) {
    check_cancelled();
    copy_media(entry.directory / "media", destination);
    if (std::ranges::find(entry.urls, url) == entry.urls.end()) {
        if (entry.urls.size() == 64) entry.urls.erase(entry.urls.begin());
        entry.urls.push_back(url);
    }
    write_entry(entry); // Atomic rewrite is also the last-access timestamp.
    check_cancelled();
}
void MediaCache::invalidate(const Entry& entry) const {
    if (entry.directory.parent_path() != root_ / "entries" || !path_utf8(entry.directory.filename()).starts_with("entry-") || !managed_directory(entry.directory))
        throw std::runtime_error("Некорректный путь записи кэша");
    fs::remove_all(entry.directory);
}
void MediaCache::prune(const fs::path& keep) const {
    std::vector<Entry> records;
    // Count incomplete/corrupt managed entries too: they must not bypass the capacity.
    for (const auto& directory : fs::directory_iterator(root_ / "entries")) {
        const auto name = path_utf8(directory.path().filename());
        if (!managed_directory(directory.path())) continue;
        if (name.starts_with(".pending-")) { fs::remove_all(directory.path()); continue; }
        if (name.starts_with("entry-") && regular(directory.path() / "media")) {
            if (regular(directory.path() / ".meta.tmp")) fs::remove(directory.path() / ".meta.tmp");
            Entry entry;
            entry.directory = directory.path(); entry.bytes = fs::file_size(directory.path() / "media");
            records.push_back(std::move(entry));
        }
    }
    uint64_t total = 0;
    for (const auto& entry : records) {
        if (entry.bytes > UINT64_MAX - total) throw std::runtime_error("Переполнение размера кэша");
        total += entry.bytes;
    }
    const auto accessed = [](const Entry& entry) { return fs::last_write_time(entry.directory / (regular(entry.directory / "meta") ? "meta" : "media")); };
    std::ranges::sort(records, [&](const Entry& a, const Entry& b) { return accessed(a) < accessed(b); });
    for (const auto& entry : records) if (total > capacity_ && entry.directory != keep) {
        check_cancelled(); invalidate(entry); total -= entry.bytes;
    }
}
bool MediaCache::publish(const SourceInfo& source, const std::string& context, const std::string& url, const fs::path& media) { // NOLINT(bugprone-easily-swappable-parameters): explicit cache context and source URL, matched to Entry fields.
    const auto bytes = fs::file_size(media);
    auto previous = lookup_source(source, context);
    if (!previous) previous = lookup_url(url, context);
    if (!bytes || bytes > capacity_ || source.live) {
        // A successful refresh must not leave an older snapshot as the next URL hit.
        if (previous) invalidate(*previous);
        return false;
    }
    const fs::path pending = temporary_directory(root_ / "entries", ".pending-");
    Entry entry{pending, source, context, previous ? previous->urls : std::vector<std::string>{}, bytes};
    if (std::ranges::find(entry.urls, url) == entry.urls.end()) {
        if (entry.urls.size() == 64) entry.urls.erase(entry.urls.begin());
        entry.urls.push_back(url);
    }
    try {
        check_cancelled();
        copy_media(media, pending / "media");
        write_entry(entry);
        check_cancelled();
        const auto destination = pending.parent_path() / ("entry-" + path_utf8(pending.filename()).substr(9));
        fs::rename(pending, destination);
        entry.directory = destination;
    } catch (...) { std::error_code error; fs::remove_all(pending, error); throw; }
    if (previous) invalidate(*previous);
    prune(entry.directory);
    return true;
}
}
