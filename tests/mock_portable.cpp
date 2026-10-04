// Native, deterministic tool fixture; never included in installation packages.
#include "platform.hpp"
#include "windows.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace fs = std::filesystem;
using namespace transcribe;
namespace {
void put(const fs::path& path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary); out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); out.close();
}
int execute(const std::vector<std::string>& args, const std::string& kind) {
    const auto value = [&](std::string_view flag) {
        const auto item = std::find(args.begin(), args.end(), flag);
        if (item == args.end() || item + 1 == args.end()) throw std::runtime_error("missing mock argument");
        return *(item + 1);
    };
    if (kind == "ffmpeg") {
        if (args.back() == "-") return 0; // Silence detection returns no pauses in this fixture.
        if (std::find(args.begin(), args.end(), "-filter_complex") != args.end()) {
            std::ifstream input(utf8_path(value("-i")), std::ios::binary);
            const std::string wave{std::istreambuf_iterator<char>(input), {}};
            const auto graph = value("-filter_complex");
            const std::regex trim("start_sample=([0-9]+):end_sample=([0-9]+)");
            size_t map = 0;
            for (auto match = std::sregex_iterator(graph.begin(), graph.end(), trim); match != std::sregex_iterator(); ++match) {
                while (map < args.size() && args[map] != "-map") ++map;
                if (map + 4 >= args.size()) throw std::runtime_error("missing split output");
                const auto begin = std::stoull((*match)[1]), end = std::stoull((*match)[2]);
                auto part = wave.substr(0, 44) + wave.substr(44 + static_cast<size_t>(begin * 2), static_cast<size_t>((end - begin) * 2));
                const auto size = static_cast<uint32_t>(part.size());
                for (unsigned i = 0; i < 4; ++i) { part[4 + i] = static_cast<char>((size - 8) >> (i * 8)); part[40 + i] = static_cast<char>((size - 44) >> (i * 8)); }
                put(utf8_path(args[map + 4]), part); map += 5;
            }
        } else fs::copy_file(utf8_path(value("-i")), utf8_path(args.back()), fs::copy_options::overwrite_existing);
    } else if (kind == "whisper-cli") {
        const auto startDelay = environment_utf8("TRANSCRIBE_MOCK_START_DELAY");
        if (!startDelay.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(startDelay)));
        const auto prefix = value("--output-file");
        const auto part = path_utf8(utf8_path(prefix).parent_path().filename());
        const std::string text = "Текст 😀 " + part + ".\n";
        std::cout << "\n[00:00:00.000 --> 00:00:00.100]  " << text << std::flush;
        std::cerr << "whisper_print_progress_callback: progress = 50%\n" << std::flush;
        const auto delay = environment_utf8("TRANSCRIBE_MOCK_DELAY");
        if (!delay.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(delay)));
        put(utf8_path(prefix + ".txt"), text);
        put(utf8_path(prefix + ".srt"), "1\n00:00:00,000 --> 00:00:00,100\n" + text + '\n');
        put(utf8_path(prefix + ".vtt"), "WEBVTT\n\n00:00:00.000 --> 00:00:00.100\n" + text + '\n');
    } else if (kind == "yt-dlp") {
        if (std::find(args.begin(), args.end(), "--simulate") != args.end()) {
            for (size_t i = 0; i + 2 < args.size(); ++i) if (args[i] == "--print-to-file") {
                auto path = args[i + 2];
                for (size_t at = 0; (at = path.find("%%", at)) != std::string::npos; ++at) path.erase(at, 1);
                put(utf8_path(path), args[i + 1] == "%(title)s" ? "Лекция 😀\n" : args[i + 1] == "%(is_live)s" ? "False\n" : "Mock\n");
            }
        } else {
            const auto input = environment_utf8("TRANSCRIBE_MOCK_AUDIO");
            if (input.empty()) throw std::runtime_error("mock audio is missing");
            auto path = value("-o");
            const auto extension = path.find("%(ext)s");
            if (extension != path.npos) path.replace(extension, 7, "wav");
            for (size_t at = 0; (at = path.find("%%", at)) != std::string::npos; ++at) path.erase(at, 1);
            fs::copy_file(utf8_path(input), utf8_path(path), fs::copy_options::overwrite_existing);
            const auto report = std::find(args.begin(), args.end(), "--print-to-file");
            if (std::distance(report, args.end()) < 3) throw std::runtime_error("missing download report");
            put(utf8_path(*(report + 2)), path + '\n');
        }
    } else throw std::runtime_error("unknown mock tool");
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args; for (int i = 1; i < argc; ++i) args.push_back(narrow_utf8(argv[i]));
    const auto kind = path_utf8(fs::path(argv[0]).stem());
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    const auto kind = path_utf8(fs::path(argv[0]).stem());
#endif
    try { return execute(args, kind); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
