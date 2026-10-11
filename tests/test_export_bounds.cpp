#include "audio.hpp"
#include "platform.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
void put(const std::filesystem::path& path, std::string_view value) {
    std::ofstream file(path, std::ios::binary); file.exceptions(std::ios::failbit | std::ios::badbit);
    file << value; file.close();
}
void require(bool value) { if (!value) throw std::runtime_error("Whisper timestamp boundary regression"); }
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); require(file.good());
    return {std::istreambuf_iterator<char>(file), {}};
}
struct Temporary {
    std::filesystem::path path = transcribe::temporary_directory(std::filesystem::temp_directory_path(), "transcribe-export-");
    ~Temporary() { std::error_code error; std::filesystem::remove_all(path, error); }
};
}
int main() {
    try {
        for (const bool web : {false, true}) {
            Temporary temporary;
            const auto prefix = temporary.path / "part";
            put(transcribe::utf8_path(transcribe::path_utf8(prefix) + ".txt"), "speech\n");
            put(transcribe::utf8_path(transcribe::path_utf8(prefix) + ".srt"), "1\n00:00:00,000 --> 00:00:12,160\nspeech\n\n");
            put(transcribe::utf8_path(transcribe::path_utf8(prefix) + ".vtt"), "WEBVTT\n\n00:00:00.000 --> 00:00:12.160\nspeech\n\n");
            unsigned warnings = 0;
            if (web) {
                put(transcribe::utf8_path(transcribe::path_utf8(prefix) + ".srt"), "1\n00:00:06,960 --> 00:00:12,580\nspeech\n\n");
                put(transcribe::utf8_path(transcribe::path_utf8(prefix) + ".vtt"), "WEBVTT\n\n00:00:06.960 --> 00:00:12.580\nspeech\n\n");
            }
            const std::vector<transcribe::Chunk> chunks{{0, 194259, {}, prefix}}; // Real 12.1411875 s Whisper example.
            const auto result = temporary.path / "result"; std::filesystem::create_directory(result);
            transcribe::merge_exports(chunks, result, temporary.path / "final", [&](std::string_view) { ++warnings; });
            require(warnings == (web ? 1U : 0U));
            require(read(result / "transcript.srt").find("00:00:12,141") != std::string::npos);
            require(read(result / "transcript.vtt").find("00:00:12.141") != std::string::npos);
            put(transcribe::utf8_path(transcribe::path_utf8(prefix) + (web ? ".vtt" : ".srt")), web
                ? "WEBVTT\n\n00:00:12.162 --> 00:00:12.162\nspeech\n\n"
                : "1\n00:00:12,162 --> 00:00:12,162\nspeech\n\n");
            bool rejected = false;
            try { transcribe::merge_exports(chunks, result, temporary.path / "rejected"); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected);
            require(read(result / "transcript.srt").find("00:00:12,141") != std::string::npos);
        }
        std::cout << "Whisper overlapping tails: clamped; wholly out-of-range segments: rejected\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
