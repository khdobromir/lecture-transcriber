#include "message.hpp"
#include "audio.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;
namespace transcribe {
void Lines::feed(std::string_view bytes) {
    buffer_.append(bytes);
    size_t start = 0;
    while (true) {
        const size_t end = buffer_.find('\n', start);
        if (end == std::string::npos) break;
        std::string_view line = std::string_view(buffer_).substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        try {
            line_(line);
        } catch (...) {
            buffer_.erase(0, end + 1);
            throw;
        }
        start = end + 1;
    }
    buffer_.erase(0, start);
    if (buffer_.size() > size_t{16} * 1024 * 1024) throw transcribe::LocalizedError(transcribe::Message{"Слишком длинная строка вывода процесса", "Process output line is too long"});
}
void Lines::finish() {
    if (!buffer_.empty()) {
        line_(buffer_);
        buffer_.clear();
    }
}
namespace {
uint32_t little(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8U | uint32_t(p[2]) << 16U | uint32_t(p[3]) << 24U;
}
std::ofstream writer(const fs::path& file) {
    std::ofstream out(file, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    return out;
}
int64_t number(std::string_view value) {
    int64_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() || result < 0)
        throw transcribe::LocalizedError(transcribe::Message{"Некорректная временная метка субтитров", "Invalid subtitle timestamp"});
    return result;
}
int64_t timestamp(std::string_view text) {
    const size_t a = text.find(':'), b = text.find(':', a == std::string_view::npos ? 0 : a + 1);
    const size_t c = text.find_first_of(".,", b == std::string_view::npos ? 0 : b + 1);
    if (a == std::string_view::npos || b == std::string_view::npos || c == std::string_view::npos || text.size() - c != 4)
        throw transcribe::LocalizedError(transcribe::Message{"Некорректная временная метка субтитров", "Invalid subtitle timestamp"});
    const auto h = number(text.substr(0, a)), m = number(text.substr(a + 1, b - a - 1));
    const auto s = number(text.substr(b + 1, c - b - 1)), ms = number(text.substr(c + 1));
    if (m >= 60 || s >= 60 || h > 1000000) throw transcribe::LocalizedError(transcribe::Message{"Некорректная временная метка субтитров", "Invalid subtitle timestamp"});
    return ((h * 60 + m) * 60 + s) * 1000 + ms;
}
std::string time_string(int64_t ms, char separator) {
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << ms / 3600000 << ':' << std::setw(2) << ms / 60000 % 60
        << ':' << std::setw(2) << ms / 1000 % 60 << separator << std::setw(3) << ms % 1000;
    return out.str();
}
struct Cue {
    int64_t begin, end;
    std::string text;
};
std::vector<Cue> read_cues(const fs::path& file, bool vtt) {
    std::ifstream in(file);
    if (!in) throw transcribe::LocalizedError(transcribe::Message{"Не удалось прочитать " + path_utf8(file), "Could not read " + path_utf8(file)});
    std::vector<Cue> cues;
    std::string line;
    if (vtt && (!std::getline(in, line) || !line.starts_with("WEBVTT")))
        throw transcribe::LocalizedError(transcribe::Message{"Некорректный заголовок WebVTT", "Invalid WebVTT header"});
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.find(" --> ") == std::string::npos) {
            if (!vtt) (void)number(line);
            if (!std::getline(in, line)) throw transcribe::LocalizedError(transcribe::Message{"Незавершённый блок субтитров", "Incomplete subtitle block"});
            if (!line.empty() && line.back() == '\r') line.pop_back();
        }
        const size_t arrow = line.find(" --> ");
        if (arrow == std::string::npos) throw transcribe::LocalizedError(transcribe::Message{"Нет временных меток в блоке субтитров", "No timestamps in subtitle block"});
        Cue cue{timestamp(std::string_view(line).substr(0, arrow)), timestamp(std::string_view(line).substr(arrow + 5)), {}};
        if (cue.end < cue.begin) throw transcribe::LocalizedError(transcribe::Message{"Обратный интервал субтитров", "Reversed subtitle interval"});
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            cue.text += line + '\n';
        }
        cues.push_back(std::move(cue));
    }
    if (in.bad()) throw transcribe::LocalizedError(transcribe::Message{"Ошибка чтения субтитров", "Subtitle read error"});
    return cues;
}
} // namespace
int64_t wav_samples(const fs::path& wav) {
    std::ifstream in(wav, std::ios::binary);
    std::array<unsigned char, 12> header{};
    if (!in.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        std::string_view(reinterpret_cast<char*>(header.data()), 4) != "RIFF" ||
        std::string_view(reinterpret_cast<char*>(header.data() + 8), 4) != "WAVE")
        throw transcribe::LocalizedError(transcribe::Message{"Ожидался PCM WAV (RIFF): " + path_utf8(wav), "Expected PCM WAV (RIFF): " + path_utf8(wav)});
    bool format = false;
    const auto size = fs::file_size(wav);
    while (in.read(reinterpret_cast<char*>(header.data()), 8)) {
        const uint32_t length = little(header.data() + 4);
        const auto start = static_cast<uint64_t>(in.tellg());
        if (start > size || length > size - start) throw transcribe::LocalizedError(transcribe::Message{"Повреждённый WAV", "Corrupt WAV"});
        const std::string_view kind(reinterpret_cast<char*>(header.data()), 4);
        if (kind == "fmt ") {
            std::array<unsigned char, 16> fmt{};
            if (length < fmt.size() || !in.read(reinterpret_cast<char*>(fmt.data()), fmt.size()))
                throw transcribe::LocalizedError(transcribe::Message{"Нет формата WAV", "Missing WAV format"});
            format = fmt[0] == 1 && fmt[1] == 0 && fmt[2] == 1 && fmt[3] == 0 &&
                     little(fmt.data() + 4) == 16000 && fmt[12] == 2 && fmt[13] == 0 && fmt[14] == 16 && fmt[15] == 0;
        } else if (kind == "data") {
            if (!format || length % 2) throw transcribe::LocalizedError(transcribe::Message{"WAV должен быть моно, 16 кГц, PCM 16 бит", "WAV must be mono, 16 kHz, 16-bit PCM"});
            return length / 2;
        }
        in.seekg(static_cast<std::streamoff>(start + length + (length & 1U)));
    }
    throw transcribe::LocalizedError(transcribe::Message{"Нет аудиоданных в WAV", "No audio data in WAV"});
}
std::vector<Chunk> split_audio(const fs::path& wav, const fs::path& work, int count, const fs::path& logs, const std::function<void(const Message&, bool)>& notice) { // NOLINT(bugprone-easily-swappable-parameters): source WAV and destination work directory ordering.
    const int64_t samples = wav_samples(wav);
    if (samples < count) throw transcribe::LocalizedError(transcribe::Message{"Запись слишком короткая для выбранного числа частей", "The recording is too short for the selected number of chunks"});
    std::vector<int64_t> boundaries{0};
    std::vector<double> pauses;
    if (count > 1) {
        std::optional<double> start;
        Lines silence([&](std::string_view line) {
            const auto get = [&](std::string_view marker) -> std::optional<double> {
                const auto pos = line.find(marker);
                if (pos == std::string_view::npos) return {};
                const std::string text(line.substr(pos + marker.size()));
                char* end{};
                const double value = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || !std::isfinite(value) || value < 0) throw transcribe::LocalizedError(transcribe::Message{"Некорректный вывод silencedetect", "Invalid silencedetect output"});
                return value;
            };
            if (auto value = get("silence_start: ")) start = value;
            if (auto value = get("silence_end: "); value && start) {
                pauses.push_back((*start + *value) / 2);
                start.reset();
            }
        });
        if (notice) notice(Message{"Поиск пауз для границ частей...", "Finding pauses for chunk boundaries..."}, false);
        run({tool_path("ffmpeg"), "-nostdin", "-hide_banner", "-i", path_utf8(wav), "-af", "silencedetect=noise=-35dB:d=0.5", "-f", "null", "-"},
            logs / "silence.log", {}, [&](std::string_view bytes) { silence.feed(bytes); });
        silence.finish();
        int fallback = 0;
        const double window = std::min(10.0, static_cast<double>(samples) / 16000 / count / 4);
        for (int i = 1; i < count; ++i) {
            const int64_t equal = samples * i / count;
            const double target = static_cast<double>(equal) / 16000;
            std::optional<double> best;
            for (double pause : pauses)
                if (std::abs(pause - target) <= window &&
                    (!best || std::abs(pause - target) < std::abs(*best - target))) best = pause;
            const int64_t cut = best ? std::llround(*best * 16000) : equal;
            if (!best) ++fallback;
            if (cut <= boundaries.back() || cut >= samples) throw transcribe::LocalizedError(transcribe::Message{"Некорректная граница части", "Invalid chunk boundary"});
            boundaries.push_back(cut);
        }
        if (fallback && notice) notice(Message{"Для " + std::to_string(fallback) + " границ паузы не найдены; качество возле срезов может снизиться.", "No pauses found for " + std::to_string(fallback) + " boundaries; quality near cuts may be reduced."}, true);
    }
    boundaries.push_back(samples);
    std::vector<Chunk> chunks;
    const fs::path parts = work / "parts";
    fs::create_directory(parts);
    for (int i = 0; i < count; ++i) {
        const auto dir = parts / std::to_string(i + 1);
        fs::create_directory(dir);
        chunks.push_back({boundaries[static_cast<size_t>(i)], boundaries[static_cast<size_t>(i) + 1],
                          count == 1 ? wav : dir / "audio.wav", dir / "transcript"});
    }
    if (count > 1) {
        struct Group {
            std::size_t begin, end;
            fs::path graph;
            std::vector<std::string> args;
        };
        std::vector<Group> groups;
        bool legacyScript = false;
        Lines capabilities([&](std::string_view line) { legacyScript = legacyScript || line.find("-filter_complex_script") != line.npos; });
        run({tool_path("ffmpeg"), "-hide_banner", "-h", "long"}, logs / "ffmpeg-capabilities.log",
            [&](std::string_view bytes) { capabilities.feed(bytes); });
        capabilities.finish();
        const std::string graphOption = legacyScript ? "-filter_complex_script" : "-/filter_complex";
        // Plan all commands before any split group runs. The graph itself is never argv.
        for (std::size_t begin = 0; begin < chunks.size();) {
            Group group{begin, begin, work / ("split-" + std::to_string(groups.size() + 1) + ".ffgraph"), {}};
            group.args = {tool_path("ffmpeg"), "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", path_utf8(wav), graphOption, path_utf8(group.graph)};
            while (group.end < chunks.size() && group.end - group.begin < 64) {
                auto candidate = group.args;
                candidate.insert(candidate.end(), {"-map", "[o" + std::to_string(group.end) + "]", "-c:a", "pcm_s16le", path_utf8(chunks[group.end].wav)});
                if (!command_line_fits(candidate)) break;
                group.args = std::move(candidate);
                ++group.end;
            }
            if (group.end == begin) throw transcribe::LocalizedError(transcribe::Message{"Пути частей превышают поддерживаемую длину команды FFmpeg; сократите каталог результата", "Chunk paths exceed the supported FFmpeg command length; shorten the result directory"});
            begin = group.end;
            groups.push_back(std::move(group));
        }
        std::size_t groupNumber = 0;
        for (const auto& group : groups) {
            ++groupNumber;
            if (notice) notice(Message{"Нарезка группы " + std::to_string(groupNumber) + "...", "Splitting group " + std::to_string(groupNumber) + "..."}, false);
            check_cancelled();
            std::string graph = "[0:a]asplit=" + std::to_string(group.end - group.begin);
            for (auto i = group.begin; i < group.end; ++i) graph += "[a" + std::to_string(i) + "]";
            for (auto i = group.begin; i < group.end; ++i) {
                const auto& chunk = chunks[i];
                graph += ";[a" + std::to_string(i) + "]atrim=start_sample=" + std::to_string(chunk.begin) +
                         ":end_sample=" + std::to_string(chunk.end) + ",asetpts=PTS-STARTPTS[o" + std::to_string(i) + "]";
            }
            auto file = writer(group.graph);
            file << graph;
            file.close();
            run(group.args, logs / (group.graph.stem().string() + ".log"));
        }
        for (const auto& chunk : chunks)
            if (wav_samples(chunk.wav) != chunk.end - chunk.begin)
                throw transcribe::LocalizedError(transcribe::Message{"Нарезка изменила число семплов аудио", "Splitting changed the number of audio samples"});
    }
    return chunks;
}
std::string segment_text(std::string_view line) {
    if (line.starts_with('[')) {
        const auto end = line.find("]  ");
        const auto arrow = line.find(" --> ");
        if (end != std::string_view::npos && arrow != std::string_view::npos && arrow < end) {
            // Only remove a valid engine timestamp prefix, never literal brackets.
            try {
                (void)timestamp(line.substr(1, arrow - 1));
                (void)timestamp(line.substr(arrow + 5, end - arrow - 5));
                line.remove_prefix(end + 3);
            } catch (const std::runtime_error&) { // NOLINT(bugprone-empty-catch): invalid timestamp brackets are literal transcript text.
            }
        }
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    return std::string(line);
}
void merge_exports(const std::vector<Chunk>& chunks, const fs::path& transcripts, const fs::path& staging, const std::function<void(const Message&)>& warning) {
    for (const auto& chunk : chunks)
        for (const char* ext : {".txt", ".srt", ".vtt"})
            if (!fs::is_regular_file(utf8_path(path_utf8(chunk.prefix) + ext))) throw transcribe::LocalizedError(transcribe::Message{"whisper-cli не создал ожидаемый файл " + std::string(ext), "whisper-cli did not create the expected file " + std::string(ext)});
    fs::create_directory(staging);
    auto txt = writer(staging / "transcript.txt");
    auto srt = writer(staging / "transcript.srt");
    auto vtt = writer(staging / "transcript.vtt");
    vtt << "WEBVTT\n\n";
    size_t index = 0;
    bool warned = false;
    for (const auto& chunk : chunks) {
        std::ifstream text(utf8_path(path_utf8(chunk.prefix) + ".txt"), std::ios::binary);
        if (!text) throw transcribe::LocalizedError(transcribe::Message{"Ошибка чтения TXT", "TXT read error"});
        std::array<char, 8192> bytes{};
        while (text.read(bytes.data(), bytes.size()) || text.gcount()) {
            check_cancelled();
            txt.write(bytes.data(), text.gcount());
        }
        if (text.bad()) throw transcribe::LocalizedError(transcribe::Message{"Ошибка чтения TXT", "TXT read error"});
        const int64_t duration_ms = (chunk.end - chunk.begin) * 1000 / 16000;
        // Whisper timestamp tokens advance in 20 ms steps (whisper.cpp multiplies
        // token indices by two centiseconds). Allow one token of rounding,
        // but keep the published interval within the actual chunk.
        constexpr int64_t timestamp_resolution_ms = 20;
        for (bool web : {false, true}) {
            auto& output = web ? vtt : srt;
            for (const auto& cue : read_cues(utf8_path(path_utf8(chunk.prefix) + (web ? ".vtt" : ".srt")), web)) {
                check_cancelled();
                if (cue.begin > duration_ms + timestamp_resolution_ms)
                    throw transcribe::LocalizedError(transcribe::Message{"Субтитр выходит за границы части: " + path_utf8(chunk.prefix), "Subtitle exceeds chunk boundaries: " + path_utf8(chunk.prefix)});
                // Model-generated end timestamps are estimates: a segment that
                // overlaps real audio is clipped to that interval. Entirely
                // out-of-range segments still fail instead of inventing time.
                if (cue.end > duration_ms + timestamp_resolution_ms && !warned) {
                    warned = true;
                    if (warning) warning(Message{"Конечные метки субтитров ограничены длительностью аудио.", "Subtitle end timestamps were limited to the audio duration."});
                }
                const int64_t offset = chunk.begin * 1000 / 16000;
                if (!web) output << ++index << '\n';
                output << time_string(std::min(cue.begin, duration_ms) + offset, web ? '.' : ',') << " --> "
                       << time_string(std::min(cue.end, duration_ms) + offset, web ? '.' : ',')
                       << '\n'
                       << cue.text << '\n';
            }
        }
    }
    txt.close();
    srt.close();
    vtt.close();
    check_cancelled();
    for (const char* ext : {"txt", "srt", "vtt"}) replace_file(staging / (std::string("transcript.") + ext), transcripts / (std::string("transcript.") + ext));
}
} // namespace transcribe
