#include "support.hpp"
#include "audio.hpp"
#include "exports.hpp"

using namespace test;
namespace {
template<class F> void rejected(F run) {
    try { run(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Expected audio validation error");
}
struct Exports {
    Temp temp;
    fs::path result = temp.path / "result";
    std::vector<transcribe::Chunk> chunks;
    Exports() { fs::create_directories(result / "audio"); }
    // Explicit TXT/SRT/VTT fixtures are positional to keep expected formats visible together.
    void add(int64_t begin, std::string_view text, std::string_view srt, std::string_view vtt) { // NOLINT(bugprone-easily-swappable-parameters)
        const auto prefix = result / "audio/parts" / std::to_string(chunks.size() + 1) / "transcript";
        chunks.push_back({begin, begin + 16000, {}, prefix});
        write(prefix.string() + ".txt", text); write(prefix.string() + ".srt", srt); write(prefix.string() + ".vtt", vtt);
    }
    void merge() { transcribe::merge_exports(chunks, result); }
};
}
int main() {
    Suite suite;
    suite.add("stream_crlf_utf8_brackets_and_incomplete_line", [] {
        std::vector<std::string> lines;
        transcribe::Lines reader([&](auto line) { lines.push_back(transcribe::segment_text(line)); });
        reader.feed("[00:00:01.000 --> 00:00:02.000]  Привет [термин]\r\n[буквальный текст]\nХво");
        reader.feed("ст"); reader.finish(); reader.finish();
        CHECK((lines == std::vector<std::string>{"Привет [термин]", "[буквальный текст]", "Хвост"}));
    });
    suite.add("wav_canonical_sample_count_and_empty_audio", [] {
        Temp temp; const auto path = temp.path / "audio.wav";
        wav(path, 0.5, 16000, 1); CHECK(transcribe::wav_samples(path) == 8000);
        wav(path, 0, 16000, 1); CHECK(transcribe::wav_samples(path) == 0);
        rejected([&] { (void)transcribe::split_audio(path, temp.path, 1); });
    });
    suite.add("wav_rejects_corruption_and_noncanonical_format", [] {
        Temp temp; const auto path = temp.path / "audio.wav";
        wav(path, 0.5, 16000, 1); const auto good = read(path);
        for (size_t offset : {size_t(0), size_t(8), size_t(20), size_t(22), size_t(24), size_t(32), size_t(34)}) {
            auto bad = good; bad[offset] = static_cast<char>(static_cast<unsigned char>(bad[offset]) ^ 1U); write(path, bad);
            rejected([&] { (void)transcribe::wav_samples(path); });
        }
        write(path, good.substr(0, good.size() - 1)); rejected([&] { (void)transcribe::wav_samples(path); });
        auto odd = good; odd[40] = static_cast<char>(static_cast<unsigned char>(odd[40]) | 1U); write(path, odd);
        rejected([&] { (void)transcribe::wav_samples(path); });
    });
    suite.add("single_chunk_exact_exports", [] {
        Exports f;
        f.add(0, "Привет\n", "7\n00:00:00,010 --> 00:00:00,250\nПривет\n\n", "WEBVTT\n\n00:00:00.010 --> 00:00:00.250\nПривет\n\n");
        f.merge(); CHECK(read(f.result / "transcript.txt") == "Привет\n");
        CHECK(read(f.result / "transcript.srt") == "1\n00:00:00,010 --> 00:00:00,250\nПривет\n\n");
        CHECK(read(f.result / "transcript.vtt") == "WEBVTT\n\n00:00:00.010 --> 00:00:00.250\nПривет\n\n");
    });
    suite.add("multiple_chunks_fractional_offsets_multiline_and_crlf", [] {
        Exports f;
        f.add(0, "Первая\n", "9\r\n00:00:00,000 --> 00:00:00,100\r\nПервая\r\n\r\n", "WEBVTT\r\n\r\n00:00:00.000 --> 00:00:00.100\r\nПервая\r\n\r\n");
        f.add(20008, "Вторая\nстрока\nТретья\n", "1\n00:00:00,010 --> 00:00:00,110\nВторая\nстрока\n\n2\n00:00:00,200 --> 00:00:00,300\nТретья\n\n",
              "WEBVTT\n\n00:00:00.010 --> 00:00:00.110\nВторая\nстрока\n\n00:00:00.200 --> 00:00:00.300\nТретья\n\n");
        f.merge(); CHECK(read(f.result / "transcript.txt") == "Первая\nВторая\nстрока\nТретья\n");
        CHECK(read(f.result / "transcript.srt") == "1\n00:00:00,000 --> 00:00:00,100\nПервая\n\n2\n00:00:01,260 --> 00:00:01,360\nВторая\nстрока\n\n3\n00:00:01,450 --> 00:00:01,550\nТретья\n\n");
        CHECK(read(f.result / "transcript.vtt") == "WEBVTT\n\n00:00:00.000 --> 00:00:00.100\nПервая\n\n00:00:01.260 --> 00:00:01.360\nВторая\nстрока\n\n00:00:01.450 --> 00:00:01.550\nТретья\n\n");
    });
    for (int empty : {0, 1, 2}) suite.add("empty_chunk_" + std::to_string(empty), [=] {
        Exports f;
        std::string expected;
        for (int i = 0; i < 3; ++i) {
            const std::string text = i == empty ? "" : "Часть " + std::to_string(i) + '\n'; expected += text;
            f.add(i * int64_t{16000}, text, text.empty() ? "" : "1\n00:00:00,000 --> 00:00:00,100\n" + text + '\n',
                  "WEBVTT\n\n" + (text.empty() ? "" : "00:00:00.000 --> 00:00:00.100\n" + text + '\n'));
        }
        f.merge(); CHECK(read(f.result / "transcript.txt") == expected);
        const auto srt = read(f.result / "transcript.srt"), vtt = read(f.result / "transcript.vtt");
        CHECK(contains(srt, "1\n") && contains(srt, "2\n") && !contains(srt, "3\n"));
        CHECK(vtt.starts_with("WEBVTT\n\n") && vtt.find("WEBVTT", 1) == std::string::npos);
    });
    for (const auto* ext : {"txt", "srt", "vtt"}) suite.add(std::string("missing_export_preserves_partial_") + ext, [=] {
        Exports f; f.add(0, "text", "", "WEBVTT\n\n"); write(f.result / "transcript.txt", "partial\n");
        fs::remove(f.chunks[0].prefix.string() + '.' + ext); rejected([&] { f.merge(); });
        CHECK(read(f.result / "transcript.txt") == "partial\n");
        CHECK(!fs::exists(f.result / "transcript.srt") && !fs::exists(f.result / "transcript.vtt"));
    });
    for (const auto* invalid : {"invalid", "00:00:00,900 --> 00:00:00,100", "00:60:00,000 --> 00:60:00,100"})
        suite.add(std::string("malformed_export_preserves_partial_") + invalid, [=] {
            Exports f; f.add(0, "text", std::string("1\n") + invalid + "\ntext\n\n", "WEBVTT\n\n");
            write(f.result / "transcript.txt", "partial\n"); rejected([&] { f.merge(); });
            CHECK(read(f.result / "transcript.txt") == "partial\n");
            CHECK(!fs::exists(f.result / "transcript.srt") && !fs::exists(f.result / "transcript.vtt"));
        });
    for (bool web : {false, true}) for (const auto* interval : {
            "00:00:00.000 --> 00:00:01.011", "00:00:01.011 --> 00:00:01.011",
            "00:00:00.000 --> 00:00:20.000"})
        suite.add(std::string("subtitle_bounds_rejects_") + (web ? "vtt_" : "srt_") + interval, [=] {
            Exports f; f.add(32000, "text\n", "1\n00:00:00,000 --> 00:00:00,100\ntext\n\n",
                "WEBVTT\n\n00:00:00.000 --> 00:00:00.100\ntext\n\n");
            std::string times = interval;
            if (!web) std::replace(times.begin(), times.end(), '.', ',');
            write(f.chunks[0].prefix.string() + (web ? ".vtt" : ".srt"),
                std::string(web ? "WEBVTT\n\n" : "1\n") + times + "\ntext\n\n");
            write(f.result / "transcript.txt", "partial\n"); rejected([&] { f.merge(); });
            CHECK(read(f.result / "transcript.txt") == "partial\n");
            CHECK(!fs::exists(f.result / "transcript.srt") && !fs::exists(f.result / "transcript.vtt"));
        });
    for (int64_t samples : {int64_t{16000}, int64_t{16008}})
        suite.add("subtitle_bounds_rounding_" + std::to_string(samples), [=] {
            Exports f; f.add(20008, "text\n", "1\n00:00:00,000 --> 00:00:01,010\ntext\n\n2\n00:00:01,010 --> 00:00:01,010\ntail\n\n",
                "WEBVTT\n\n00:00:00.000 --> 00:00:01.010\ntext\n\n00:00:01.010 --> 00:00:01.010\ntail\n\n");
            f.chunks[0].end = f.chunks[0].begin + samples;
            f.merge(); check_exports(f.result, 2250);
            CHECK(contains(read(f.result / "transcript.srt"), "00:00:02,250 --> 00:00:02,250"));
            CHECK(contains(read(f.result / "transcript.vtt"), "00:00:02.250 --> 00:00:02.250"));
            CHECK(!contains(read(f.result / "transcript.srt"), "02,260"));
        });
    suite.add("subtitle_bounds_exact_duration_is_valid", [] {
        Exports f; f.add(0, "text\n", "1\n00:00:00,000 --> 00:00:01,000\ntext\n\n",
            "WEBVTT\n\n00:00:00.000 --> 00:00:01.000\ntext\n\n");
        f.merge(); check_exports(f.result, 1000);
        CHECK(contains(read(f.result / "transcript.srt"), "00:00:01,000"));
    });
    suite.add("independent_oracle_rejects_invalid_exports", [] {
        Exports f; f.add(0, "text", "1\n00:00:00,000 --> 00:00:00,100\ntext\n\n", "WEBVTT\n\n00:00:00.000 --> 00:00:00.100\ntext\n\n");
        f.merge(); check_exports(f.result, 1000);
        for (const auto* srt : {"2\n00:00:00,000 --> 00:00:00,100\ntext\n\n", "1\n00:00:00,900 --> 00:00:00,100\ntext\n\n", "1\n00:00:00,000 --> 00:00:02,000\ntext\n\n", "1\ninvalid\ntext\n\n"}) {
            write(f.result / "transcript.srt", srt); rejected([&] { check_exports(f.result, 1000); });
        }
    });
    return suite.run();
}
