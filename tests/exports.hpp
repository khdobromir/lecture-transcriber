#pragma once
#include "support.hpp"
#include <regex>
#include <tuple>

namespace test {
// Independent export oracle: never calls the production subtitle parser/merger.
inline void check_exports(const fs::path& result, int64_t duration_ms) {
    CHECK(duration_ms >= 0);
    using Cue = std::tuple<int64_t, int64_t, std::string>;
    std::vector<Cue> reference;
    for (bool web : {false, true}) {
        std::istringstream stream(read(result / (web ? "transcript.vtt" : "transcript.srt")));
        std::string line;
        if (web) { CHECK(static_cast<bool>(std::getline(stream, line))); CHECK(line == "WEBVTT"); }
        const std::regex pattern(web
            ? R"(^([0-9]{2,}):([0-9]{2}):([0-9]{2})\.([0-9]{3}) --> ([0-9]{2,}):([0-9]{2}):([0-9]{2})\.([0-9]{3})$)"
            : R"(^([0-9]{2,}):([0-9]{2}):([0-9]{2}),([0-9]{3}) --> ([0-9]{2,}):([0-9]{2}):([0-9]{2}),([0-9]{3})$)");
        std::vector<Cue> cues;
        int64_t previous = 0;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (!web) { CHECK(line == std::to_string(cues.size() + 1)); CHECK(static_cast<bool>(std::getline(stream, line))); }
            std::smatch match; CHECK(std::regex_match(line, match, pattern));
            const auto time = [&](int offset) {
                const int64_t hours = std::stoll(match[offset].str());
                CHECK(hours <= duration_ms / 3600000);
                const int minutes = std::stoi(match[offset + 1].str()), seconds = std::stoi(match[offset + 2].str());
                CHECK(minutes < 60 && seconds < 60);
                return ((hours * 60 + minutes) * 60 + seconds) * 1000 + std::stoi(match[offset + 3].str());
            };
            const auto begin = time(1), end = time(5);
            CHECK(begin >= previous && end >= begin && end <= duration_ms);
            previous = begin;
            std::string text;
            while (std::getline(stream, line) && !line.empty()) text += line + '\n';
            CHECK(!text.empty()); cues.emplace_back(begin, end, text);
        }
        if (web) CHECK(cues == reference); else reference = std::move(cues);
    }
    CHECK(fs::is_regular_file(result / "transcript.txt"));
}
}
