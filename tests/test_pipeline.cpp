#include "support.hpp"
#include "audio.hpp"
#include "exports.hpp"
#include <array>
#include <sched.h>
#include <sys/inotify.h>
#include <sys/file.h>
#include <sys/ioctl.h>

using namespace test;
struct Fixture {
    Temp temp;
    fs::path root = temp.path, data = root / "app data", engine = data / "whisper.cpp/build/bin/whisper-cli";
    fs::path audio = root / "Лекция '$(touch PWNED)' & 1.wav", out = root / "Результаты с пробелами";
    fs::path log = root / "calls", events = root / "events", tools = root / "bin";
    Env env;
    Fixture() {
        test::link(MOCK_BINARY, engine);
        test::link(MOCK_BINARY, tools / "yt-dlp");
        write(data / "models/ggml-medium-q5_0.bin", "test-model");
        write(data / "models/ggml-silero-v6.2.0.bin", "test-vad");
        write(data / "default-model", "medium\n");
        wav(audio);
        env = {{"TRANSCRIBE_HOME", data.string()}, {"HOME", (root / "home").string()}, {"XDG_CACHE_HOME", (root / "cache").string()},
               {"PATH", tools.string() + ':' + test::getenv("PATH")},
               {"MOCK_LOG", log.string()}, {"MOCK_INPUT", audio.string()}, {"EVENT_LOG", events.string()}};
    }
    std::vector<std::string> command(std::vector<std::string> args) const {
        args.insert(args.begin(), {TRANSCRIBE_BINARY, "--out", out.string()}); return args;
    }
    Capture invoke(std::vector<std::string> args, const Env& extra = {}) {
        auto variables = env; for (auto& [key, value] : extra) variables[key] = value;
        return test::invoke(command(std::move(args)), variables, root);
    }
    fs::path result() const { return single(out); }
    std::vector<Call> calls() const { return test::calls(log); }
    std::vector<std::vector<std::string>> event_rows() const {
        std::vector<std::vector<std::string>> rows;
        if (!fs::exists(events)) return rows;
        std::istringstream stream(read(events)); std::string line;
        while (std::getline(stream, line)) {
            std::vector<std::string> row; std::istringstream fields(line); std::string field;
            while (std::getline(fields, field, '\t')) row.push_back(field);
            if (row.size() == 5) rows.push_back(std::move(row));
        }
        return rows;
    }
    size_t event_count(std::string_view action) const {
        const auto rows = event_rows();
        return static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [&](const auto& row) { return row[1] == action; }));
    }
    void no_children() const {
        for (const auto& row : event_rows()) {
            const pid_t pid = static_cast<pid_t>(std::stoi(row[3]));
            CHECK(kill(pid, 0) < 0 && errno == ESRCH);
        }
    }
};

int main() {
    Suite suite;
    const auto add = [&](std::string_view name, auto run) {
        suite.add(std::string(name), [run = std::move(run)] { Fixture fixture; run(fixture); });
    };
    add("default_output_under_home_and_missing_home_override", [](Fixture& f) {
        success(test::invoke({TRANSCRIBE_BINARY, f.audio.string()}, f.env, f.root));
        CHECK(fs::is_regular_file(single(f.root / "home/Transcriptions") / "transcripts/transcript.txt"));
        CHECK(!fs::exists(f.root / "transcripts"));
        auto env = f.env; env["HOME"] = "";
        CHECK(test::invoke({TRANSCRIBE_BINARY, f.audio.string()}, env, f.root).code == 1);
        success(f.invoke({f.audio.string()}, {{"HOME", ""}}));
    });
    add("url_title_percent_unicode_and_no_path_traversal", [](Fixture& f) {
        success(f.invoke({"https://example.org/video"}, {{"MOCK_TITLE", "../../Лекция 100%\\часть\n2"}}));
        CHECK(f.result().filename().string().starts_with("_.._Лекция 100%_часть_2_"));
        CHECK(fs::is_regular_file(f.result() / "logs/metadata.log") && fs::is_regular_file(f.result() / "logs/download.log"));
        CHECK(fs::is_regular_file(f.result() / "transcripts/transcript.txt"));
    });
    add("cache_repeat_url_without_downloader_or_network", [](Fixture& f) {
        const std::string url = "https://example.org/video";
        success(f.invoke({url}));
        fs::remove(f.tools / "yt-dlp"); test::link(which("ffmpeg"), f.tools / "ffmpeg");
        const auto result = f.invoke({url}, {{"PATH", f.tools.string()}}); success(result);
        CHECK(contains(result.out, "из кэша") && f.calls().size() == 4 && entries(f.out) == 2);
    });
    add("cache_alias_same_video_skips_media_download", [](Fixture& f) {
        success(f.invoke({"https://example.org/first"}));
        const auto result = f.invoke({"https://example.org/alias"}); success(result);
        CHECK(contains(result.out, "из кэша"));
        const auto calls = f.calls();
        CHECK(calls.size() == 5 && calls[3].kind == "yt-dlp" && has(calls[3].args, "--simulate"));
    });
    add("cache_refresh_and_disabled_download_again", [](Fixture& f) {
        const std::string url = "https://example.org/video";
        success(f.invoke({url})); success(f.invoke({"--refresh-cache", url})); success(f.invoke({"--no-cache", url}));
        const auto calls = f.calls(); CHECK(calls.size() == 9);
        CHECK(std::count_if(calls.begin(), calls.end(), [](const Call& c) { return c.kind == "yt-dlp" && !has(c.args, "--simulate"); }) == 3);
    });
    add("cache_corrupt_content_invalidates_without_deleting_work", [](Fixture& f) {
        const std::string url = "https://example.org/video"; success(f.invoke({url}));
        const auto cache = f.root / "cache/transcribe/media/entries";
        const auto entry = single(cache);
        const auto bytes = fs::file_size(entry / "media");
        write(entry / "media", std::string(static_cast<size_t>(bytes), 'x'));
        const auto result = f.invoke({url}); CHECK(result.code != 0 && !fs::exists(entry));
        CHECK(!contains(result.out, "Готово."));
        bool preserved = false;
        for (const auto& directory : fs::directory_iterator(f.out)) if (contains(read(directory.path() / "source.txt"), "Статус: failed"))
            preserved = fs::is_regular_file(directory.path() / "audio/source.cached");
        CHECK(preserved);
    });
    add("cache_unavailable_is_warning_and_normal_download", [](Fixture& f) {
        write(f.root / "bad-cache", "file");
        const auto result = f.invoke({"--cache-dir", (f.root / "bad-cache").string(), "https://example.org/video"}); success(result);
        CHECK(contains(result.err, "кэш недоступен") && f.calls().size() == 3);
    });
    add("cache_concurrent_runs_download_once", [](Fixture& f) {
        const std::string url = "https://example.org/video";
        Child first(f.command({url}), f.env, f.root);
        Child second(f.command({url}), f.env, f.root);
        success(first.wait()); success(second.wait());
        const auto calls = f.calls();
        CHECK(std::count_if(calls.begin(), calls.end(), [](const Call& c) { return c.kind == "yt-dlp" && !has(c.args, "--simulate"); }) == 1);
        CHECK(entries(f.out) == 2); f.no_children();
    });
    add("cache_lock_wait_is_cancellable", [](Fixture& f) {
        const std::string url = "https://example.org/video"; success(f.invoke({url}));
        const int lock = open((f.root / "cache/transcribe/media/lock").c_str(), O_RDWR | O_CLOEXEC); CHECK(lock >= 0);
        CHECK(flock(lock, LOCK_EX) == 0);
        try {
            Child child(f.command({url}), f.env, f.root);
            until([&] { return fs::exists(f.out) && entries(f.out) == 2; });
            CHECK(kill(child.pid, SIGINT) == 0); CHECK(child.wait().code == 130);
        } catch (...) { close(lock); throw; }
        close(lock); CHECK(f.calls().size() == 3); f.no_children();
    });
    add("cache_download_cancel_does_not_publish", [](Fixture& f) {
        auto env = f.env; env["HANG_DOWNLOAD"] = "1";
        Child child(f.command({"https://example.org/video"}), env, f.root);
        until([&] { return f.event_count("ready") == 1 && f.event_count("helper") == 1; });
        CHECK(kill(child.pid, SIGINT) == 0); CHECK(child.wait().code == 130); f.no_children();
        CHECK(entries(f.root / "cache/transcribe/media/entries") == 0 && fs::exists(f.result() / "audio"));
    });
    add("cache_live_stream_not_saved", [](Fixture& f) {
        success(f.invoke({"https://example.org/video"}, {{"MOCK_LIVE", "1"}}));
        CHECK(entries(f.root / "cache/transcribe/media/entries") == 0);
    });
    add("cache_failed_refresh_preserves_previous_audio", [](Fixture& f) {
        const std::string url = "https://example.org/video"; success(f.invoke({url}));
        CHECK(f.invoke({"--refresh-cache", url}, {{"FAIL_DOWNLOAD_ONLY", "1"}}).code == 23);
        const auto result = f.invoke({url}, {{"FAIL_DOWNLOAD", "1"}}); success(result);
        CHECK(contains(result.out, "из кэша"));
    });
    add("plain_progress_and_disabled_progress", [](Fixture& f) {
        auto result = f.invoke({f.audio.string()}); success(result);
        CHECK(contains(result.out, "Распознавание: ") && contains(result.out, "ETA") && !contains(result.out, "\033["));
        result = f.invoke({"--no-progress", f.audio.string()}); success(result);
        CHECK(!contains(result.out, "Распознавание: ") && !contains(result.out, "ETA"));
    });
    add("terminal_progress_updates_one_line_and_respects_dumb", [](Fixture& f) {
        for (const auto* term : {"xterm", "dumb"}) {
            const int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK); CHECK(master >= 0);
            CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
            const int slave = open(ptsname(master), O_RDWR | O_NOCTTY | O_CLOEXEC); CHECK(slave >= 0);
            winsize size{}; size.ws_col = 80; size.ws_row = 24; CHECK(ioctl(slave, TIOCSWINSZ, &size) == 0);
            auto env = f.env; env["TERM"] = term; env["ENGINE_DELAY_MS"] = "700";
            std::string output;
            try {
                Child child(f.command({f.audio.string()}), env, f.root, slave);
                const auto drain = [&] { std::array<char, 4096> data{}; ssize_t n; while ((n = ::read(master, data.data(), data.size())) > 0) output.append(data.data(), static_cast<size_t>(n)); };
                success(child.wait(std::chrono::seconds(15), drain)); drain();
            } catch (...) { close(master); throw; }
            close(master);
            if (std::string_view(term) == "xterm") CHECK(contains(output, "\r\033[KРаспознавание [") && contains(output, "ETA"));
            else CHECK(!contains(output, "\033[") && contains(output, "Распознавание: "));
        }
    });
    add("local_file_conversion_exports_cleanup", [](Fixture& f) {
        const auto original = read(f.audio);
        const auto result = f.invoke({f.audio.string()}); success(result);
        for (const char* file : {"transcripts/transcript.txt", "transcripts/transcript.srt", "transcripts/transcript.vtt", "source.txt"}) CHECK(fs::is_regular_file(f.result() / file));
        CHECK(!fs::exists(f.result() / "audio")); CHECK(read(f.audio) == original); CHECK(!fs::exists(f.root / "PWNED"));
        CHECK(f.calls().size() == 1 && f.calls()[0].kind == "whisper-cli");
        CHECK(!contains(result.out + result.err, "Тестовая расшифровка"));
        CHECK(contains(read(f.result() / "source.txt"), "Статус: completed"));
        check_exports(f.result() / "transcripts", 500);
        CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\n");
        CHECK(read(f.result() / "transcripts/transcript.srt") == "1\n00:00:00,000 --> 00:00:00,100\nТестовая расшифровка 1.\n\n");
        CHECK(read(f.result() / "transcripts/transcript.vtt") == "WEBVTT\n\n00:00:00.000 --> 00:00:00.100\nТестовая расшифровка 1.\n\n");
    });
    add("url_arguments_cookies_prompt_and_keep_audio", [](Fixture& f) {
        const std::string url = "https://vkvideo.ru/video-1_2?x=$(touch PWNED)&y='lecture'";
        const std::string browser = "chromium:/profile path/Default", prompt = "Тензор, базис; $(touch PWNED)";
        success(f.invoke({"--cookies-from-browser", browser, "--prompt", prompt, "--threads", "2", "--keep-audio", url}));
        const auto calls = f.calls(); CHECK(calls.size() == 3);
        CHECK(calls[0].args.back() == url); CHECK(value(calls[0].args, "--cookies-from-browser") == browser);
        CHECK(value(calls[2].args, "--prompt") == prompt); CHECK(value(calls[2].args, "--threads") == "2");
        CHECK(fs::is_regular_file(f.result() / "audio/lecture.wav")); CHECK(!fs::exists(f.root / "PWNED"));
    });
    add("no_vad_and_custom_model", [](Fixture& f) {
        fs::remove(f.data / "models/ggml-silero-v6.2.0.bin");
        const auto model = f.root / "Моя модель.bin"; write(model, "custom-test-model");
        success(f.invoke({"--no-vad", "--model", model.string(), f.audio.string()}));
        CHECK(!has(f.calls()[0].args, "--vad")); CHECK(value(f.calls()[0].args, "--model") == model);
    });
    add("engine_error_preserves_audio", [](Fixture& f) {
        const auto result = f.invoke({f.audio.string()}, {{"FAIL_ENGINE", "1"}});
        CHECK(result.code == 17); CHECK(fs::exists(f.result() / "audio/lecture.wav")); CHECK(!contains(result.out, "Готово."));
    });
    add("download_error_stops_pipeline", [](Fixture& f) {
        const auto result = f.invoke({"https://vkvideo.ru/video-1_2"}, {{"FAIL_DOWNLOAD", "1"}});
        CHECK(result.code == 23); CHECK(f.calls().size() == 1 && f.calls()[0].kind == "yt-dlp"); CHECK(!contains(result.out, "Готово."));
    });
    add("invalid_input_and_options", [](Fixture& f) {
        for (const auto& args : std::vector<std::vector<std::string>>{{"--threads", "0", f.audio.string()}, {"--threads", "4x", f.audio.string()},
             {"--model"}, {"missing.mp4"}, {"[https://vkvideo.ru/video](https://vkvideo.ru/video)"}, {"--unknown", f.audio.string()},
             {"--chunks", "0", f.audio.string()}, {"--jobs", "2", f.audio.string()}, {"--chunks", "257", f.audio.string()},
             {"--chunks", "2", "--jobs", "3", f.audio.string()}, {"--jobs", "-1", f.audio.string()}}) CHECK(f.invoke(args).code != 0);
        CHECK(!fs::exists(f.out));
    });
    add("ffmpeg_failure_stops_before_engine", [](Fixture& f) {
        write(f.audio, "not audio"); const auto result = f.invoke({f.audio.string()});
        CHECK(result.code != 0); CHECK(!fs::exists(f.log)); CHECK(fs::is_regular_file(f.audio)); CHECK(!contains(result.out, "Готово."));
        CHECK(!read(f.result() / "logs/ffmpeg.log").empty());
    });
    add("repeated_runs_have_distinct_directories", [](Fixture& f) {
        success(f.invoke({f.audio.string()})); success(f.invoke({f.audio.string()})); CHECK(entries(f.out) == 2);
    });
    add("help_and_version_without_installation", [](Fixture& f) {
        for (const auto* option : {"--help", "--version"}) {
            const auto result = f.invoke({option}, {{"TRANSCRIBE_HOME", (f.root / "not installed").string()}}); success(result);
            if (std::string_view(option) == "--version") CHECK(result.out == "transcribe " TRANSCRIBE_VERSION "\n");
        }
        CHECK(!fs::exists(f.out));
    });
    add("version_after_option_terminator_is_input", [](Fixture& f) { CHECK(f.invoke({"--", "--version"}).code != 0); CHECK(!fs::exists(f.out)); });
    add("no_arguments_and_help_precedence", [](Fixture& f) {
        CHECK(test::invoke({TRANSCRIBE_BINARY}, f.env, f.root).code == 2);
        success(f.invoke({"--unknown", "--help"}, {{"TRANSCRIBE_HOME", (f.root / "absent").string()}}));
        CHECK(f.invoke({"--prompt", "--help"}).code == 1);
        CHECK(!fs::exists(f.out));
    });
    add("default_data_directory_without_override", [](Fixture& f) {
        const auto home = f.root / "home", data = home / ".local/share/transcribe";
        fs::create_directories(data.parent_path()); fs::rename(f.data, data);
        success(f.invoke({f.audio.string()}, {{"HOME", home.string()}, {"TRANSCRIBE_HOME", ""}}));
        CHECK(contains(read(f.result() / "source.txt"), data.string()));
    });
    add("missing_downloader_preserves_work_directory", [](Fixture& f) {
        fs::remove(f.tools / "yt-dlp"); test::link(which("ffmpeg"), f.tools / "ffmpeg");
        const auto result = f.invoke({"https://vkvideo.ru/video-1_2"}, {{"PATH", f.tools.string()}});
        CHECK(result.code == 127); CHECK(fs::exists(f.result() / "audio")); CHECK(!contains(result.out, "Готово."));
    });
    add("missing_engine_stops_before_creating_results", [](Fixture& f) {
        fs::remove(f.engine); CHECK(f.invoke({f.audio.string()}).code != 0); CHECK(!fs::exists(f.out));
    });
    add("preflight_engine_directory_stops_before_conversion", [](Fixture& f) {
        fs::remove(f.engine); fs::create_directory(f.engine);
        const auto result = f.invoke({f.audio.string()});
        CHECK(result.code == 1 && contains(result.err, "Нет whisper-cli"));
        CHECK(!fs::exists(f.out) && f.calls().empty());
    });
    for (const auto* ext : {"srt", "vtt"}) for (bool multiple : {false, true})
        add(std::string("subtitle_bounds_") + ext + (multiple ? "_chunks" : "_single"), [=](Fixture& f) {
            wav(f.audio, 1, 16000, 1); const auto original = read(f.audio);
            const auto result = f.invoke({"--chunks", multiple ? "2" : "1", "--jobs", "1", f.audio.string()},
                {{"OUT_OF_BOUNDS_EXPORT", ext}, {"OUT_OF_BOUNDS_PART", multiple ? "2" : "1"}});
            CHECK(result.code == 1 && contains(result.err, "границы части"));
            CHECK(contains(read(f.result() / "source.txt"), "Статус: failed\nКод: 1"));
            CHECK(read(f.audio) == original && fs::is_regular_file(f.result() / "audio/lecture.wav"));
            CHECK(!read(f.result() / "transcripts/transcript.txt").empty());
            CHECK(!fs::exists(f.result() / "transcripts/transcript.srt") && !fs::exists(f.result() / "transcripts/transcript.vtt"));
            CHECK(!contains(result.out, "Готово.")); f.no_children();
        });
    add("missing_export_preserves_audio", [](Fixture& f) {
        const auto result = f.invoke({f.audio.string()}, {{"MISSING_EXPORT", "vtt"}});
        CHECK(result.code != 0); CHECK(fs::exists(f.result() / "audio/lecture.wav"));
        CHECK(contains(read(f.result() / "source.txt"), "Статус: failed")); CHECK(!contains(result.out, "Готово."));
    });
    add("incremental_text_before_exit_and_split_utf8", [](Fixture& f) {
        auto env = f.env; env["ENGINE_DELAY_MS"] = "800"; env["SPLIT_UTF8"] = "1";
        Child child(f.command({f.audio.string()}), env, f.root);
        until([&] { return fs::exists(f.out) && entries(f.out) == 1 && fs::exists(f.result() / "transcripts/transcript.txt") && contains(read(f.result() / "transcripts/transcript.txt"), "Тестовая расшифровка 1."); });
        CHECK(child.running()); CHECK(contains(read(f.result() / "source.txt"), "Статус: processing"));
        const auto result = child.wait(); success(result); CHECK(!contains(result.out + result.err, "Тестовая расшифровка"));
    });
    add("chunk_order_subtitles_and_sample_coverage", [](Fixture& f) {
        wav(f.audio, 4, 16000, 1);
        success(f.invoke({"--chunks", "4", "--jobs", "2", "--threads", "1", "--keep-audio", f.audio.string()}, {{"OUT_OF_ORDER", "1"}}));
        CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\nТестовая расшифровка 2.\nТестовая расшифровка 3.\nТестовая расшифровка 4.\n");
        const auto srt = read(f.result() / "transcripts/transcript.srt"), vtt = read(f.result() / "transcripts/transcript.vtt");
        CHECK(contains(srt, "2\n00:00:01,000 --> 00:00:01,100")); CHECK(contains(srt, "4\n00:00:03,000"));
        CHECK(vtt.starts_with("WEBVTT\n\n") && vtt.find("WEBVTT", 1) == std::string::npos);
        CHECK(contains(vtt, "00:00:03.000 --> 00:00:03.100"));
        std::string combined;
        for (int i = 1; i <= 4; ++i) {
            const auto part = f.result() / "audio/parts" / std::to_string(i) / "audio.wav";
            CHECK(transcribe::wav_samples(part) == 16000);
            const auto bytes = read(part); combined += bytes.substr(bytes.find("data") + 8);
        }
        const auto original = read(f.result() / "audio/lecture.wav"); CHECK(combined == original.substr(original.find("data") + 8));
        int active = 0, maximum = 0; std::vector<int> order;
        for (const auto& row : f.event_rows()) {
            if (row[1] == "start") { ++active; maximum = std::max(maximum, active); }
            if (row[1] == "finish") { --active; order.push_back(std::stoi(row[2])); }
        }
        CHECK(active == 0 && maximum == 2 && order.front() == 2);
    });
    add("out_of_order_partial_waits_for_first_chunk", [](Fixture& f) {
        wav(f.audio, 2, 16000, 1); auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["HANG_PART"] = "1";
        Child child(f.command({"--chunks", "2", "--jobs", "2", f.audio.string()}), env, f.root);
        until([&] { return f.event_count("finish") == 1 && f.event_count("helper") == 1; });
        CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\n");
        CHECK(contains(read(f.result() / "audio/parts/2/transcript.txt"), "расшифровка 2"));
        kill(child.pid, SIGTERM); CHECK(child.wait().code == 143); f.no_children();
    });
    add("pause_boundary_and_fallback", [](Fixture& f) {
        wav(f.audio, 6, 16000, 1, [](double time) { return time >= 2.3 && time < 3.1; });
        const auto result = f.invoke({"--chunks", "2", "--keep-audio", f.audio.string()}); success(result);
        const auto n = transcribe::wav_samples(f.result() / "audio/parts/1/audio.wav");
        CHECK(n >= 43150 && n <= 43250); CHECK(!contains(result.err, "паузы не найдены"));
    });
    add("empty_speech_valid_exports", [](Fixture& f) {
        success(f.invoke({"--chunks", "3", f.audio.string()}, {{"EMPTY_SPEECH", "1"}}));
        CHECK(read(f.result() / "transcripts/transcript.txt").empty()); CHECK(read(f.result() / "transcripts/transcript.srt").empty());
        CHECK(read(f.result() / "transcripts/transcript.vtt") == "WEBVTT\n\n");
    });
    add("malformed_export_preserves_partial_text", [](Fixture& f) {
        CHECK(f.invoke({f.audio.string()}, {{"MALFORMED_EXPORT", "1"}}).code != 0);
        CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\n"); CHECK(fs::exists(f.result() / "audio"));
    });
    add("default_thread_budget_and_explicit_override", [](Fixture& f) {
        success(f.invoke({"--chunks", "2", f.audio.string()}));
        const int cpus = transcribe::physical_cpus(), jobs = std::min(2, cpus);
        for (const auto& call : f.calls()) CHECK(std::stoi(value(call.args, "--threads")) == std::max(1, cpus / jobs));
    });
    for (int signal : {SIGINT, SIGTERM, SIGHUP}) for (const std::string tool : {"yt-dlp", "ffmpeg", "whisper-cli"}) {
        add("cancel_" + tool + "_" + std::to_string(signal), [=](Fixture& f) {
            if (tool == "ffmpeg") test::link(MOCK_BINARY, f.tools / "ffmpeg");
            auto env = f.env; env["HANG_TOOL"] = tool;
            Child child(f.command({tool == "yt-dlp" ? "https://vkvideo.ru/video-1_2" : f.audio.string()}), env, f.root);
            until([&] { return f.event_count("ready") == 1 && f.event_count("helper") == 1; });
            kill(child.pid, signal); CHECK(child.wait().code == 128 + signal); f.no_children();
            CHECK(contains(read(f.result() / "source.txt"), "Статус: interrupted"));
            CHECK(fs::exists(f.result() / "audio"));
            if (tool == "whisper-cli") CHECK(contains(read(f.result() / "transcripts/transcript.txt"), "расшифровка 1"));
        });
    }
    add("cancel_all_stubborn_workers_and_isolate_other_run", [](Fixture& f) {
        wav(f.audio, 2, 16000, 1); auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["IGNORE_STOP"] = "1";
        Child child(f.command({"--chunks", "2", "--jobs", "2", f.audio.string()}), env, f.root);
        until([&] { return f.event_count("ready") == 2 && f.event_count("helper") == 2; });
        Fixture other; auto other_env = other.env; other_env["HANG_TOOL"] = "whisper-cli";
        Child independent(other.command({other.audio.string()}), other_env, other.root);
        until([&] { return other.event_count("ready") == 1 && other.event_count("helper") == 1; });
        const auto start = std::chrono::steady_clock::now(); kill(child.pid, SIGTERM);
        CHECK(child.wait().code == 143); CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(4));
        f.no_children(); CHECK(independent.running()); kill(independent.pid, SIGTERM); CHECK(independent.wait().code == 143); other.no_children();
    });
    add("forced_backend_exit_cleans_managed_tree", [](Fixture& f) {
        auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["IGNORE_STOP"] = "1";
        Child child(f.command({f.audio.string()}), env, f.root);
        until([&] { return f.event_count("ready") == 1 && f.event_count("helper") == 1; });
        CHECK(kill(child.pid, SIGKILL) == 0); CHECK(child.wait().code == 137);
        try {
            until([&] {
                for (const auto& row : f.event_rows()) if (kill(static_cast<pid_t>(std::stoi(row[3])), 0) == 0) return false;
                return true;
            }, std::chrono::seconds(4));
        } catch (...) {
            // A failing regression still owns the deliberately exposed tools.
            for (const auto& row : f.event_rows()) kill(static_cast<pid_t>(std::stoi(row[3])), SIGKILL);
            throw;
        }
        f.no_children();
    });
    add("worker_failure_stops_other_workers_and_queue", [](Fixture& f) {
        wav(f.audio, 4, 16000, 1);
        const auto result = f.invoke({"--chunks", "4", "--jobs", "2", f.audio.string()},
            {{"HANG_TOOL", "whisper-cli"}, {"HANG_PART", "1"}, {"FAIL_PART", "2"}});
        CHECK(result.code == 17); CHECK(f.event_count("start") == 2); f.no_children();
        CHECK(fs::exists(f.result() / "audio/parts/1/transcript.partial.txt"));
    });
    for (bool multiple : {false, true}) for (bool tail : {false, true}) for (const auto* mode : {"exit", "segv"}) {
        add(std::string("reliability_crash_") + mode + (multiple ? "_workers" : "_single") + (tail ? "_tail" : "_line"), [=](Fixture& f) {
            wav(f.audio, 4, 16000, 1); const auto original = read(f.audio);
            auto env = f.env;
            env["CRASH_PART"] = multiple ? "2" : "1"; env["CRASH_MODE"] = mode;
            env["CRASH_RELEASE"] = (f.root / "release crash").string();
            if (tail) env["INCOMPLETE_TAIL"] = "1";
            if (multiple) { env["HANG_TOOL"] = "whisper-cli"; env["HANG_PART"] = "1"; }
            Child child(f.command({"--chunks", multiple ? "4" : "1", "--jobs", multiple ? "2" : "1", f.audio.string()}), env, f.root);
            until([&] { return f.event_count("crash_ready") == 1 && (!multiple || f.event_count("helper") == 1); });
            if (!tail) until([&] { return contains(read(f.result() / "audio/parts" / env["CRASH_PART"] / "transcript.partial.txt"), "расшифровка"); });
            write(env["CRASH_RELEASE"], "release");
            const auto result = child.wait(); CHECK(result.code == (mode == std::string_view("segv") ? 139 : 17)); f.no_children();
            const auto metadata = read(f.result() / "source.txt");
            CHECK(contains(metadata, "Статус: failed\nКод: " + std::to_string(result.code)));
            CHECK(read(f.audio) == original && fs::is_regular_file(f.result() / "audio/lecture.wav"));
            CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\n");
            CHECK(read(f.result() / "audio/parts" / env["CRASH_PART"] / "transcript.partial.txt") ==
                  "Тестовая расшифровка " + env["CRASH_PART"] + ".\n");
            CHECK(contains(read(f.result() / "logs" / ("whisper-" + env["CRASH_PART"] + ".log")), "progress ="));
            CHECK(f.event_count("start") == (multiple ? 2 : 1));
            CHECK(!fs::exists(f.result() / "transcripts/transcript.srt") && !fs::exists(f.result() / "transcripts/transcript.vtt"));
            CHECK(!contains(result.out, "Готово."));
        });
    }
    for (bool repeat : {false, true}) add(repeat ? "reliability_sigint_repeated" : "reliability_sigint_workers", [=](Fixture& f) {
        wav(f.audio, 4, 16000, 1); const auto original = read(f.audio);
        auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["IGNORE_STOP"] = "1";
        Child child(f.command({"--chunks", "4", "--jobs", "2", f.audio.string()}), env, f.root);
        until([&] { return f.event_count("ready") == 2 && f.event_count("helper") == 2; });
        Fixture other; auto other_env = other.env; other_env["HANG_TOOL"] = "whisper-cli";
        Child independent(other.command({other.audio.string()}), other_env, other.root);
        until([&] { return other.event_count("ready") == 1 && other.event_count("helper") == 1; });
        CHECK(kill(child.pid, SIGINT) == 0);
        const auto result = child.wait(std::chrono::seconds(10), [&] { if (repeat) CHECK(kill(child.pid, SIGINT) == 0); });
        CHECK(result.code == 130); f.no_children(); CHECK(independent.running());
        CHECK(contains(read(f.result() / "source.txt"), "Статус: interrupted\nКод: 130"));
        CHECK(read(f.result() / "transcripts/transcript.txt") == "Тестовая расшифровка 1.\n");
        CHECK(read(f.audio) == original && fs::is_regular_file(f.result() / "audio/lecture.wav"));
        CHECK(f.event_count("start") == 2);
        CHECK(kill(independent.pid, SIGINT) == 0); CHECK(independent.wait().code == 130); other.no_children();
    });
    add("large_diagnostic_pipe_does_not_deadlock", [](Fixture& f) {
        success(f.invoke({"--chunks", "4", "--jobs", "2", f.audio.string()}, {{"FLOOD", "1"}}));
        CHECK(fs::file_size(f.result() / "logs/whisper-1.log") > uintmax_t{512} * 1024);
    });
    add("exited_tool_remaining_helper_reaped_and_descriptors_closed", [](Fixture& f) {
        success(f.invoke({f.audio.string()}, {{"ORPHAN_HELPER", "1"}, {"CHECK_CLOSED_FDS", "1"}}));
        CHECK(f.event_count("helper") == 1); f.no_children();
    });
    add("repeated_cancel_forces_stubborn_worker_immediately", [](Fixture& f) {
        auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["IGNORE_STOP"] = "1";
        Child child(f.command({f.audio.string()}), env, f.root);
        until([&] { return f.event_count("ready") == 1 && f.event_count("helper") == 1; });
        const auto start = std::chrono::steady_clock::now(); kill(child.pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); kill(child.pid, SIGINT);
        CHECK(child.wait().code == 143); CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1)); f.no_children();
    });
    add("closed_terminal_output_pipe_cancels_workers", [](Fixture& f) {
        auto env = f.env; env["HANG_TOOL"] = "whisper-cli"; env["HANG_PART"] = "1"; env["ENGINE_DELAY_MS"] = "500";
        int output[2]; CHECK(pipe2(output, O_CLOEXEC) == 0);
        Child child(f.command({"--chunks", "2", "--jobs", "2", f.audio.string()}), env, f.root, output[1]);
        until([&] { return f.event_count("ready") == 1 && f.event_count("helper") == 1 && f.event_count("start") == 2; });
        close(output[0]); CHECK(child.wait().code == 128 + SIGPIPE); f.no_children();
        CHECK(contains(read(f.result() / "source.txt"), "Статус: interrupted"));
    });
    add("closed_terminal_output_pipe_after_last_progress_preserves_audio", [](Fixture& f) {
        int output[2]; CHECK(pipe2(output, O_CLOEXEC | O_NONBLOCK) == 0);
        Child child(f.command({f.audio.string()}), f.env, f.root, output[1]);
        std::string progress;
        until([&] {
            std::array<char, 4096> bytes{};
            const auto n = ::read(output[0], bytes.data(), bytes.size());
            if (n > 0) progress.append(bytes.data(), static_cast<size_t>(n));
            return contains(progress, "Распознавание: 100%");
        });
        close(output[0]);
        CHECK(child.wait().code == 128 + SIGPIPE); f.no_children();
        CHECK(contains(read(f.result() / "source.txt"), "Статус: interrupted"));
        CHECK(fs::is_regular_file(f.result() / "audio/lecture.wav"));
    });
    for (int signal : {SIGINT, SIGTERM, SIGHUP}) {
        add("signal_during_cleanup_keeps_completed_status_" + std::to_string(signal), [=](Fixture& f) {
            auto env = f.env; env["SLOW_CLEANUP"] = "1"; env["ENGINE_DELAY_MS"] = "500";
            Child child(f.command({f.audio.string()}), env, f.root);
            until([&] { return fs::exists(f.out) && entries(f.out) == 1 && fs::exists(f.result() / "audio/parts/1"); });
            const int notify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK); CHECK(notify >= 0);
            const int watch = inotify_add_watch(notify, (f.result() / "audio/parts/1").c_str(), IN_DELETE);
            if (watch < 0) { close(notify); throw std::runtime_error("inotify_add_watch"); }
            try {
                until([&] {
                    std::array<char, 4096> events{};
                    return ::read(notify, events.data(), events.size()) > 0;
                });
                CHECK(kill(child.pid, signal) == 0);
            } catch (...) { close(notify); throw; }
            close(notify);
            success(child.wait()); f.no_children();
            CHECK(contains(read(f.result() / "source.txt"), "Статус: completed\nКод: 0"));
            CHECK(!fs::exists(f.result() / "audio"));
            for (const char* ext : {"txt", "srt", "vtt"}) CHECK(fs::is_regular_file(f.result() / "transcripts" / (std::string("transcript.") + ext)));
        });
    }
    add("cleanup_failure_warns_without_failing_completed_transcription", [](Fixture& f) {
        const auto result = f.invoke({f.audio.string()}, {{"DENY_CLEANUP", "1"}});
        const auto locked = f.result() / "audio/parts/1/locked";
        // Restore write access before assertions, including when the old CLI fails.
        fs::permissions(locked, fs::perms::owner_all);
        success(result);
        CHECK(contains(result.err, "Не удалось удалить рабочие файлы"));
        CHECK(contains(read(f.result() / "source.txt"), "Статус: completed\nКод: 0"));
        CHECK(fs::is_regular_file(locked / "keep"));
        for (const char* ext : {"txt", "srt", "vtt"}) CHECK(fs::is_regular_file(f.result() / "transcripts" / (std::string("transcript.") + ext)));
    });
    add("write_failure_stops_running_workers", [](Fixture& f) {
        wav(f.audio, 1, 16000, 1);
        const auto result = f.invoke({"--chunks", "2", "--jobs", "2", f.audio.string()},
            {{"TEST_FILE_LIMIT", "65536"}, {"FLOOD", "1"}, {"FLOOD_PART", "2"}, {"HANG_TOOL", "whisper-cli"}, {"HANG_PART", "1"}});
        CHECK(result.code != 0); CHECK(f.event_count("helper") == 1); f.no_children();
        CHECK(contains(read(f.result() / "source.txt"), "Статус: failed"));
        CHECK(contains(read(f.result() / "transcripts/transcript.txt"), "расшифровка 1"));
    });
    suite.add("stream_timestamp_prefix_literal_brackets_and_incomplete_line", [] {
        std::vector<std::string> lines; transcribe::Lines reader([&](auto line) { lines.push_back(transcribe::segment_text(line)); });
        reader.feed("[00:00:01.000 --> 00:00:02.000]  Привет [термин]\n[буквальный текст]\nХво"); reader.feed("ст"); reader.finish();
        CHECK((lines == std::vector<std::string>{"Привет [термин]", "[буквальный текст]", "Хвост"}));
    });
    return suite.run();
}
