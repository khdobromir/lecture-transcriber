#pragma once
#include "process.hpp"
#include <cstdint>
#include <functional>
#include <string_view>

namespace transcribe {
class Lines {
public:
    explicit Lines(std::function<void(std::string_view)> line) : line_(std::move(line)) {}
    void feed(std::string_view bytes);
    void finish();
private:
    std::string buffer_;
    std::function<void(std::string_view)> line_;
};
struct Chunk { int64_t begin, end; std::filesystem::path wav, prefix; };
int64_t wav_samples(const std::filesystem::path& wav);
std::vector<Chunk> split_audio(const std::filesystem::path& wav, const std::filesystem::path& work, int count,
                               const std::filesystem::path& logs);
std::string segment_text(std::string_view line);
void merge_exports(const std::vector<Chunk>& chunks, const std::filesystem::path& transcripts,
                   const std::filesystem::path& staging);
}
