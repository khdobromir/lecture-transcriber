#include "audio.hpp"
#include "cancellation.hpp"
#include "platform.hpp"
#include "process.hpp"
#include "windows.hpp"
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace transcribe;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("real FFmpeg split regression failed"); }
int execute(const std::string& ffmpeg) {
    install_signal_handlers();
    const auto tools = path_utf8(utf8_path(ffmpeg).parent_path());
#ifdef _WIN32
    const auto search = wide_utf8(tools + ';' + environment_utf8("PATH"));
    require(SetEnvironmentVariableW(L"PATH", search.c_str()) != 0);
#else
    require(setenv("PATH", (tools + ':' + environment_utf8("PATH")).c_str(), 1) == 0);
#endif
    const auto temp = temporary_directory(fs::temp_directory_path(), "transcribe-split-");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{temp};
    auto root = temp / utf8_path("Лекция 😀 с пробелами");
    fs::create_directory(root);
    while (path_utf8(root).size() < 180) { root /= "long-directory"; fs::create_directory(root); }
    const auto input = root / utf8_path("Вход 😀.wav");
    run({ffmpeg, "-nostdin", "-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=1.234", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", path_utf8(input)}, temp / "generate.log");
    const auto samples = wav_samples(input);
#ifdef _WIN32
    std::string inlineGraph = "[0:a]asplit=256";
    for (int i = 0; i < 256; ++i) inlineGraph += "[a" + std::to_string(i) + "]";
    for (int i = 0; i < 256; ++i) inlineGraph += ";[a" + std::to_string(i) + "]atrim=start_sample=" +
        std::to_string(samples * i / 256) + ":end_sample=" + std::to_string(samples * (i + 1) / 256) +
        ",asetpts=PTS-STARTPTS[o" + std::to_string(i) + "]";
    std::vector<std::string> previous{ffmpeg, "-filter_complex", inlineGraph};
    for (int i = 0; i < 256; ++i) previous.insert(previous.end(), {"-map", "[o" + std::to_string(i) + "]", "-c:a", "pcm_s16le", path_utf8(root / "work-256/parts" / std::to_string(i + 1) / "audio.wav")});
    require(command_line_size(previous) > 32767);
#endif
    for (int count : {1, 2, 17, 256}) {
        const auto work = root / ("work-" + std::to_string(count)); fs::create_directory(work);
        const auto chunks = split_audio(input, work, count, work);
        require(chunks.size() == static_cast<std::size_t>(count));
        std::int64_t total = 0;
        for (const auto& chunk : chunks) {
            require(chunk.begin == total && chunk.end > chunk.begin);
            require(wav_samples(chunk.wav) == chunk.end - chunk.begin); total = chunk.end;
        }
        require(total == samples);
        std::cout << "PASS real FFmpeg " << count << " chunks, Unicode paths and exact sample bounds\n";
    }
    const auto cancelWork = root / "cancel"; fs::create_directory(cancelWork);
    int groups = 0; bool cancelled = false;
    try {
        (void)split_audio(input, cancelWork, 256, cancelWork, [&](std::string_view message, bool) {
            if (message.starts_with("Нарезка группы") && ++groups == 2) cancellation_token().request(2);
        });
    } catch (const ProcessError& error) { cancelled = error.code == 130; }
    require(cancelled && groups == 2);
    require(fs::is_regular_file(cancelWork / "parts/64/audio.wav") && !fs::exists(cancelWork / "parts/65/audio.wav"));
    cancellation_token().reset();
    const auto failWork = root / "fail"; fs::create_directory(failWork); groups = 0; bool failed = false;
    try {
        (void)split_audio(input, failWork, 256, failWork, [&](std::string_view message, bool) {
            if (message.starts_with("Нарезка группы") && ++groups == 2) fs::remove(failWork / "parts/65");
        });
    } catch (const ProcessError&) { failed = true; }
    require(failed && groups == 2 && fs::is_regular_file(failWork / "parts/64/audio.wav"));
    std::cout << "PASS cancellation and tool failure between groups preserve earlier parts\n";
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    const std::string ffmpeg = argc > 1 ? narrow_utf8(argv[1]) : "ffmpeg";
#else
int main(int argc, char** argv) {
    const std::string ffmpeg = argc > 1 ? argv[1] : "ffmpeg";
#endif
    try { return execute(ffmpeg); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
