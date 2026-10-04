#pragma once
#include "cli.hpp"
#include "result.hpp"
#include "platform.hpp"
#include <optional>
#include <vector>

namespace transcribe {
struct SourceInfo { std::string title, extractor, id; bool live = false; };
std::filesystem::path default_cache();
std::string authentication_context(const Options& options);
SourceInfo probe_source(const Options& options, const ResultPaths& paths);
std::filesystem::path download_source(const Options& options, const ResultPaths& paths);
class MediaCache {
public:
    struct Entry {
        std::filesystem::path directory;
        SourceInfo source;
        std::string context;
        std::vector<std::string> urls;
        uint64_t bytes = 0;
    };
    MediaCache(const std::filesystem::path& root, uint64_t capacity);
    MediaCache(const MediaCache&) = delete;
    MediaCache& operator=(const MediaCache&) = delete;
    std::optional<Entry> lookup_url(const std::string& url, const std::string& context) const;
    std::optional<Entry> lookup_source(const SourceInfo& source, const std::string& context) const;
    void copy(Entry& entry, const std::filesystem::path& destination, const std::string& url);
    // Caller has validated the downloaded audio using FFmpeg before publication.
    bool publish(const SourceInfo& source, const std::string& context, const std::string& url,
                 const std::filesystem::path& media);
    void invalidate(const Entry& entry) const;
private:
    std::unique_ptr<FileLock> lock_;
    std::filesystem::path root_;
    uint64_t capacity_;
    std::vector<Entry> entries() const;
    void write_entry(const Entry& entry) const;
    void prune(const std::filesystem::path& keep) const;
};
}
