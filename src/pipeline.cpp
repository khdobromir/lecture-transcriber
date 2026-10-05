// Shared pipeline; terminal and GUI adapters own presentation.
#include "audio.hpp"
#include "cli.hpp"
#include "media.hpp"
#include "progress.hpp"
#include "result.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "pipeline.hpp"
#include "platform.hpp"
#include "protocol.hpp"

namespace fs = std::filesystem;
namespace transcribe {
namespace {
void metadata(const ResultPaths& paths, const std::string& details, const Options& options, std::string_view status, int code) {
    const auto& result = paths.root;
    // Failure metadata must remain publishable after cancellation has been accepted.
    const std::function<void()> checkpoint = status == "failed" || status == "interrupted" ?
        std::function<void()>{[] {}} : std::function<void()>{check_cancelled};
    std::ofstream out(result / ".source.tmp");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << details << "Статус: " << status << "\nКод: " << code << '\n';
    out.close();
    replace_file(result / ".source.tmp", result / "source.txt", checkpoint);
    nlohmann::json manifest{{"version", 1}, {"status", status}, {"code", code}, {"source", options.input},
        {"model", options.model}, {"language", "ru"}, {"threads", options.threads}, {"chunks", options.chunks},
        {"jobs", options.jobs}, {"vad", options.vad}, {"title", path_utf8(result.filename())}, {"created_unix_ms", paths.created},
        {"transcripts", {{"txt", "transcripts/transcript.txt"}, {"srt", "transcripts/transcript.srt"}, {"vtt", "transcripts/transcript.vtt"}}}};
    std::ofstream json(result / ".result.tmp", std::ios::binary);
    json.exceptions(std::ios::badbit | std::ios::failbit);
    json << manifest.dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n'; json.close();
    replace_file(result / ".result.tmp", result / "result.json", checkpoint);
}

void recognize(const std::vector<transcribe::Chunk>& chunks, const Options& o,
               const transcribe::Inputs& files, const transcribe::ResultPaths& paths, const EventSink& sink) {
    struct Part {
        std::ofstream file;
        std::string pending;
        int progress = 0;
        bool complete = false;
        transcribe::Lines text, errors;
        explicit Part(const fs::path& path)
            : file(path), text([this](std::string_view line) {
                const auto value = transcribe::segment_text(line);
                if (value.empty()) return;
                file << value << '\n'; file.flush();
                pending += value + '\n';
            }), errors([this](std::string_view line) {
                const auto pos = line.find("progress = ");
                if (pos == std::string_view::npos) return;
                auto value = line.substr(pos + 11);
                while (value.starts_with(' ')) value.remove_prefix(1);
                int number{};
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
                if (error == std::errc{} && end < value.data() + value.size() && *end == '%' && number >= 0 && number <= 100)
                    progress = std::max(progress, number);
            }) { file.exceptions(std::ios::badbit | std::ios::failbit); }
    };
    std::ofstream live(paths.transcripts() / "transcript.txt");
    live.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<std::unique_ptr<Part>> parts;
    parts.reserve(chunks.size());
    std::vector<std::unique_ptr<transcribe::Process>> processes(chunks.size());
    for (const auto& chunk : chunks) parts.push_back(std::make_unique<Part>(chunk.prefix.parent_path() / "transcript.partial.txt"));
    size_t next = 0, current = 0, finished = 0, active = 0;
    transcribe::Eta eta(transcribe::Eta::Clock::now());
    auto last = transcribe::Eta::Clock::time_point::min();
    const auto publish = [&] {
        while (current < parts.size()) {
            auto& part = *parts[current];
            live << part.pending;
            live.flush();
            part.pending.clear();
            if (!part.complete) break;
            ++current;
        }
    };
    try {
        while (finished < chunks.size()) {
            transcribe::check_cancelled();
            while (next < chunks.size() && active < static_cast<size_t>(o.jobs)) {
                const size_t index = next;
                const auto& chunk = chunks[index];
                std::vector<std::string> args{
                    path_utf8(files.engine), "--model", path_utf8(files.model), "--file", path_utf8(chunk.wav), "--language", "ru",
                    "--threads", std::to_string(o.threads), "--no-gpu", "--output-txt", "--output-srt", "--output-vtt",
                    "--output-file", path_utf8(chunk.prefix), "--print-progress"
                };
                if (o.vad) args.insert(args.end(), {"--vad", "--vad-model", path_utf8(files.vad_model)});
                if (!o.prompt.empty()) args.insert(args.end(), {"--prompt", o.prompt});
                processes[index] = std::make_unique<transcribe::Process>(args, paths.logs() / ("whisper-" + std::to_string(index + 1) + ".log"),
                    [&, index](std::string_view bytes) { parts[index]->text.feed(bytes); },
                    [&, index](std::string_view bytes) { parts[index]->errors.feed(bytes); });
                ++next; ++active;
            }
            for (size_t i = 0; i < next; ++i) if (processes[i] && !parts[i]->complete) {
                processes[i]->tick();
                if (processes[i]->done()) {
                    parts[i]->text.finish(); parts[i]->errors.finish();
                    processes[i]->require_success();
                    for (const char* ext : {".txt", ".srt", ".vtt"})
                        if (!fs::is_regular_file(utf8_path(path_utf8(chunks[i].prefix) + ext)))
                            throw std::runtime_error("whisper-cli не создал ожидаемый файл " + std::string(ext));
                    parts[i]->complete = true;
                    parts[i]->progress = 100;
                    --active; ++finished;
                    processes[i].reset();
                }
            }
            publish();
            std::vector<transcribe::PartProgress> states;
            states.reserve(chunks.size());
            for (size_t i = 0; i < chunks.size(); ++i) states.push_back({chunks[i].end - chunks[i].begin, parts[i]->progress});
            const auto now = transcribe::Eta::Clock::now();
            if (last == transcribe::Eta::Clock::time_point::min() || now - last >= std::chrono::milliseconds(200) || finished == chunks.size()) {
                last = now;
                const double fraction = transcribe::weighted_progress(states);
                sink(Event{.type = EventType::progress, .stage = "recognize", .result = paths.root, .fraction = fraction,
                           .finished = finished, .total = chunks.size(), .eta = eta.update(fraction, now)});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        transcribe::check_cancelled();
        live.close();
    } catch (...) {
        const auto error = std::current_exception();
        transcribe::stop_all(processes, transcribe::cancellation_signal() ? transcribe::cancellation_signal() : 15);
        // Flush even an incomplete final line; files for later parts remain intact.
        try {
            for (auto& part : parts) part->text.finish();
            publish();
        } catch (...) { // NOLINT(bugprone-empty-catch): best-effort flush must preserve the original exception.
        }
        std::rethrow_exception(error);
    }
}

}
RunResult Pipeline::run(Options o, const ToolPaths& tools) {
    transcribe::ResultPaths paths;
    std::string details;
    try {
        transcribe::check_cancelled();
        const auto files = transcribe::validate_inputs(o, {tools.root, tools.cwd});
        const auto& model = files.model;
        auto input = files.input;
        const bool url = files.url;
        o.cookies = files.cookies;
        o.model = files.model_selection;
        const auto parent = o.output.empty() ? transcribe::default_output() : fs::absolute(o.output);
        paths = transcribe::ResultPaths::create(parent, url ? "video" : path_utf8(input.stem()));
        details = "Источник: " + o.input + "\nМодель: " + path_utf8(model) + "\nЯзык: ru\nПотоки: " + std::to_string(o.threads) +
            "\nЧасти: " + std::to_string(o.chunks) + "\nРаботники: " + std::to_string(o.jobs) + "\nVAD: " + (o.vad ? "on\n" : "off\n");
        metadata(paths, details, o, "processing", 0);
        std::unique_ptr<transcribe::MediaCache> cache;
        std::optional<transcribe::MediaCache::Entry> cached;
        transcribe::SourceInfo source;
        std::string context;
        const auto cache_warning = [&](const std::exception& error) {
            transcribe::check_cancelled();
            sink_(Event{.type = EventType::warning, .message = std::string("кэш недоступен: ") + error.what(), .result = paths.root});
        };
        if (url) {
            sink_(Event{.type = EventType::result, .stage = "temporary", .result = paths.root});
            if (o.cache) try {
                context = transcribe::authentication_context(o);
                cache = std::make_unique<transcribe::MediaCache>(o.cache_dir.empty() ? transcribe::default_cache() : o.cache_dir, o.cache_limit);
                if (!o.refresh_cache) cached = cache->lookup_url(o.input, context);
            } catch (const std::exception& error) { cache_warning(error); cache.reset(); }
            if (cached) source = cached->source;
            else {
                sink_(Event{.type = EventType::stage, .stage = "probe", .result = paths.root});
                source = transcribe::probe_source(o, paths);
                if (cache && !o.refresh_cache) try { cached = cache->lookup_source(source, context); }
                catch (const std::exception& error) { cache_warning(error); cache.reset(); }
            }
            paths.name(source.title);
            details += "Название: " + transcribe::safe_title(source.title) + '\n';
            metadata(paths, details, o, "processing", 0);
            sink_(Event{.type = EventType::result, .result = paths.root});
            if (cached && cache) try {
                input = paths.work() / "source.cached";
                cache->copy(*cached, input, o.input);
                sink_(Event{.type = EventType::stage, .stage = "cache", .result = paths.root});
            } catch (const std::exception& error) { cache_warning(error); cached.reset(); cache.reset(); }
            if (!cached) {
                sink_(Event{.type = EventType::stage, .stage = "download", .result = paths.root});
                input = transcribe::download_source(o, paths);
            }
        } else {
            sink_(Event{.type = EventType::result, .result = paths.root});
            sink_(Event{.type = EventType::stage, .stage = "local", .result = paths.root});
        }

        const auto work = paths.work();
        const fs::path wav = work / "lecture.wav";
        sink_(Event{.type = EventType::stage, .stage = "prepare", .result = paths.root});
        try {
            transcribe::run({tool_path("ffmpeg"), "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             "-i", path_utf8(input), "-map", "0:a:0", "-vn", "-ar", "16000",
             "-ac", "1", "-c:a", "pcm_s16le", path_utf8(wav)}, paths.logs() / "ffmpeg.log");
            require_file(wav, "Проверь, есть ли в видео аудиодорожка");
            (void)transcribe::wav_samples(wav);
        } catch (...) {
            // Cancellation or a broken output pipe does not prove cache corruption.
            const auto failure = std::current_exception();
            if (cache && cached && !transcribe::cancellation_signal()) try { cache->invalidate(*cached); }
            catch (const std::exception& error) { sink_(Event{.type = EventType::warning, .message = error.what(), .result = paths.root}); }
            std::rethrow_exception(failure);
        }
        if (cache && !cached) try {
            if (!cache->publish(source, context, o.input, input))
                sink_(Event{.type = EventType::warning, .message = "аудио не сохранено в кэше (размер превышает лимит или live-поток).", .result = paths.root});
        } catch (const std::exception& error) { cache_warning(error); }
        cache.reset(); // Recognition does not hold the shared download/cache lock.

        sink_(Event{.type = EventType::stage, .stage = "split", .result = paths.root});
        const auto chunks = transcribe::split_audio(wav, work, o.chunks, paths.logs(), [&](std::string_view message, bool warning) {
            sink_(Event{.type = warning ? EventType::warning : EventType::stage, .stage = "split", .message = std::string(message), .result = paths.root});
        });
        for (size_t i = 0; i < chunks.size(); ++i)
            details += "Часть " + std::to_string(i + 1) + " (семплы): " + std::to_string(chunks[i].begin) + "–" + std::to_string(chunks[i].end) + '\n';
        metadata(paths, details, o, "processing", 0);
        sink_(Event{.type = EventType::stage, .stage = "recognize", .message = std::to_string(o.jobs) + " работников, по " + std::to_string(o.threads) + " поток(а)", .result = paths.root});
        const auto start = std::chrono::steady_clock::now();
        recognize(chunks, o, files, paths, sink_);
        sink_(Event{.type = EventType::stage, .stage = "merge", .result = paths.root});
        transcribe::merge_exports(chunks, paths.transcripts(), paths.staging());
        transcribe::check_cancelled();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        sink_(Event{.type = EventType::finalizing, .message = std::to_string(seconds / 60.0), .result = paths.root});
        transcribe::check_cancelled();
        metadata(paths, details, o, "completed", 0);
        transcribe::commit_completion();
        // Cleanup is irreversible. Once success is committed, it must not turn
        // into an interrupted/failed run that falsely promises preserved audio.
        try {
            if (!o.keep) fs::remove_all(work);
        } catch (const std::exception& error) {
            try { sink_(Event{.type = EventType::warning, .message = "Не удалось удалить рабочие файлы в " + path_utf8(work) + ": " + error.what(), .result = paths.root}); } catch (...) { // NOLINT(bugprone-empty-catch): after commit, losing the notification cannot revoke saved exports.
            }
        }
        try { sink_(Event{.type = EventType::completed, .status = "completed", .result = paths.root}); }
        catch (...) { // NOLINT(bugprone-empty-catch): completion is irrevocable.
            /* Completion is already committed; a lost UI cannot revoke saved exports. */ }
        return {paths.root, "completed", {}, 0};
    } catch (const std::exception& error) {
        const auto* process = dynamic_cast<const ProcessError*>(&error);
        const int code = transcribe::cancellation_signal() ? 128 + transcribe::cancellation_signal() : (process ? process->code : 1);
        if (!paths.root.empty()) {
            try { metadata(paths, details, o, transcribe::cancellation_signal() ? "interrupted" : "failed", code); }
            catch (const std::exception&) { // NOLINT(bugprone-empty-catch): retain the original failure.
                /* Preserve the original failure if metadata cannot be saved. */ }
        }
        const std::string status = transcribe::cancellation_signal() ? "interrupted" : "failed";
        try { sink_(Event{.type = EventType::failed, .message = error.what(), .status = status, .result = paths.root, .code = code}); }
        catch (...) { // NOLINT(bugprone-empty-catch): preserve original exit status when the event channel is broken.
            /* The original exit status survives a broken event channel. */ }
        return {paths.root, status, error.what(), code};
    }
}
}
