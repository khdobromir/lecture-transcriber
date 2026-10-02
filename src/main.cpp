// Linux, C++23. Запускает yt-dlp, FFmpeg и whisper-cli без оболочки.
#include "audio.hpp"
#include "cli.hpp"
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
#include <iostream>
#include <sched.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

using transcribe::ProcessError;
using transcribe::Options;
using transcribe::read_line;
using transcribe::require_file;
using transcribe::run;

fs::path app_home() {
    if (const char* p = std::getenv("TRANSCRIBE_HOME"); p && *p)
        return fs::absolute(p);
    const char* p = std::getenv("HOME");
    if (!p || !*p) throw std::runtime_error("Не задан HOME или TRANSCRIBE_HOME");
    return fs::path(p) / ".local/share/transcribe";
}

void help() {
    std::cout << R"(Transcribe CLI — транскрипция русской речи на CPU

Использование:
  transcribe [параметры] "URL или путь к файлу"

Параметры:
  --model small|medium|turbo|ПУТЬ.bin  Модель (по умолчанию выбранная при установке)
  --threads N                        Потоки каждого whisper-cli (1–256)
                                     По умолчанию физические ядра / число работников
  --chunks N                         Число частей (1–256, по умолчанию 1)
  --jobs N                           Одновременные части (не больше --chunks)
                                     По умолчанию до двух при дроблении
  --out КАТАЛОГ                      Родительский каталог результатов
                                     (по умолчанию ./transcripts)
  --cookies-from-browser СПЕЦ         Например firefox или chromium:ПРОФИЛЬ
  --cookies ФАЙЛ                     Файл cookies в формате Netscape
  --prompt ТЕКСТ                     Краткий список терминов лекции
  --no-vad                           Отключить определение участков речи
  --keep-audio                       Оставить рабочие аудио/видеофайлы
  --help                             Эта справка
  --version                          Версия программы
  --                                 Конец параметров

Результаты: transcript.txt, transcript.srt, transcript.vtt, source.txt.
Каждый запуск создаёт отдельный каталог; исходный локальный файл не удаляется.
TXT дополняется во время распознавания в порядке записи; субтитры — после успеха.
Ctrl+C, SIGTERM и SIGHUP останавливают работников и сохраняют частичный текст.
)";
}

fs::path new_result_dir(const fs::path& parent) {
    fs::create_directories(parent);
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&now, &local);
    std::ostringstream name;
    name << "lecture-" << std::put_time(&local, "%Y%m%d-%H%M%S") << "-XXXXXX";
    std::string pattern = (parent / name.str()).string();
    char* result = mkdtemp(pattern.data());
    if (!result) throw std::runtime_error("Не удалось создать каталог: " + std::string(std::strerror(errno)));
    return fs::path(result);
}

void metadata(const fs::path& result, const std::string& details, std::string_view status, int code) {
    std::ofstream out(result / ".source.tmp");
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << details << "Статус: " << status << "\nКод: " << code << '\n';
    out.close();
    fs::rename(result / ".source.tmp", result / "source.txt");
}

void recognize(const std::vector<transcribe::Chunk>& chunks, const Options& o,
               const transcribe::Inputs& files, const fs::path& result) {
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
    std::ofstream live(result / "transcript.txt");
    live.exceptions(std::ios::badbit | std::ios::failbit);
    std::vector<std::unique_ptr<Part>> parts;
    parts.reserve(chunks.size());
    std::vector<std::unique_ptr<transcribe::Process>> processes(chunks.size());
    for (const auto& chunk : chunks) parts.push_back(std::make_unique<Part>(chunk.prefix.parent_path() / "transcript.partial.txt"));
    size_t next = 0, current = 0, finished = 0, active = 0;
    int last_progress = -1;
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
                    files.engine.string(), "--model", files.model.string(), "--file", chunk.wav.string(), "--language", "ru",
                    "--threads", std::to_string(o.threads), "--no-gpu", "--output-txt", "--output-srt", "--output-vtt",
                    "--output-file", chunk.prefix.string(), "--print-progress"
                };
                if (o.vad) args.insert(args.end(), {"--vad", "--vad-model", files.vad_model.string()});
                if (!o.prompt.empty()) args.insert(args.end(), {"--prompt", o.prompt});
                processes[index] = std::make_unique<transcribe::Process>(args, result / ("whisper-" + std::to_string(index + 1) + ".log"),
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
                        if (!fs::is_regular_file(chunks[i].prefix.string() + ext))
                            throw std::runtime_error("whisper-cli не создал ожидаемый файл " + std::string(ext));
                    parts[i]->complete = true;
                    parts[i]->progress = 100;
                    --active; ++finished;
                    processes[i].reset();
                }
            }
            publish();
            int64_t weighted = 0;
            for (size_t i = 0; i < chunks.size(); ++i) weighted += (chunks[i].end - chunks[i].begin) * parts[i]->progress;
            const int progress = static_cast<int>(weighted / chunks.back().end);
            if (progress > last_progress) {
                std::cout << "Распознавание: " << progress << "%; завершено частей " << finished << '/' << chunks.size() << '\n' << std::flush;
                last_progress = progress;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        transcribe::check_cancelled();
        live.close();
    } catch (...) {
        const auto error = std::current_exception();
        transcribe::stop_all(processes, transcribe::cancellation_signal() ? transcribe::cancellation_signal() : SIGTERM);
        // Flush even an incomplete final line; files for later parts remain intact.
        try {
            for (auto& part : parts) part->text.finish();
            publish();
        } catch (...) { // NOLINT(bugprone-empty-catch): best-effort flush must preserve the original exception.
        }
        std::rethrow_exception(error);
    }
}

int main(int argc, char** argv) {
    fs::path result;
    std::string details;
    try {
        transcribe::install_signal_handlers();
        std::vector<std::string_view> args(argv + 1, argv + argc);
        const auto action = transcribe::cli_action(args);
        if (action == transcribe::CliAction::help || action == transcribe::CliAction::usage) {
            help(); return action == transcribe::CliAction::usage ? 2 : 0;
        }
        if (action == transcribe::CliAction::version) {
            std::cout << "transcribe " << TRANSCRIBE_VERSION << '\n'; return 0;
        }
        Options o = transcribe::parse_arguments(args, transcribe::physical_cpus());
        cpu_set_t mask;
        CPU_ZERO(&mask);
        const int logical = sched_getaffinity(0, sizeof(mask), &mask) == 0 ? CPU_COUNT(&mask)
            : static_cast<int>(std::thread::hardware_concurrency());
        if (transcribe::oversubscribed(o, logical))
            std::cerr << "Число работников × потоки превышает доступные CPU; скорость может снизиться.\n";
        transcribe::check_cancelled();
        const auto files = transcribe::validate_inputs(o, {app_home(), fs::current_path()});
        const auto& model = files.model;
        auto input = files.input;
        const bool url = files.url;
        o.cookies = files.cookies;
        result = new_result_dir(fs::absolute(o.output));
        const fs::path work = result / "audio";
        fs::create_directory(work);
        details = "Источник: " + o.input + "\nМодель: " + model.string() + "\nЯзык: ru\nПотоки: " + std::to_string(o.threads) +
            "\nЧасти: " + std::to_string(o.chunks) + "\nРаботники: " + std::to_string(o.jobs) + "\nVAD: " + (o.vad ? "on\n" : "off\n");
        metadata(result, details, "processing", 0);
        std::cout << "Каталог результата: " << result.string() << '\n' << std::flush;
        if (url) {
            std::cout << "[1/3] Скачивание медиа по URL...\n";
            const fs::path path_file = work / "download.path";
            std::vector<std::string> args = {
                "yt-dlp", "--ignore-config", "--no-playlist", "--no-simulate",
                "-f", "bestaudio/best", "--restrict-filenames", "-o",
                (work / "source.%(ext)s").string(), "--print-to-file",
                "after_move:%(filepath)s", path_file.string()
            };
            if (!o.browser.empty()) {
                args.insert(args.end(), {"--cookies-from-browser", o.browser});
            }
            if (!o.cookies.empty()) args.insert(args.end(), {"--cookies", o.cookies});
            args.insert(args.end(), {"--", o.input});
            std::cout.flush();
            run(args, result / "download.log");
            input = read_line(path_file);
            require_file(input, "yt-dlp не сохранил аудио/видео");
        } else std::cout << "[1/3] Используется локальный файл\n";

        const fs::path wav = work / "lecture.wav";
        std::cout << "[2/3] WAV: моно, 16 кГц, PCM 16 бит...\n";
        std::cout.flush();
        run({"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             "-i", input.string(), "-map", "0:a:0", "-vn", "-ar", "16000",
             "-ac", "1", "-c:a", "pcm_s16le", wav.string()}, result / "ffmpeg.log");
        require_file(wav, "Проверь, есть ли в видео аудиодорожка");

        const auto chunks = transcribe::split_audio(wav, work, o.chunks);
        for (size_t i = 0; i < chunks.size(); ++i)
            details += "Часть " + std::to_string(i + 1) + " (семплы): " + std::to_string(chunks[i].begin) + "–" + std::to_string(chunks[i].end) + '\n';
        metadata(result, details, "processing", 0);
        std::cout << "[3/3] Русская речь, CPU, " << o.jobs << " работников, по " << o.threads << " поток(а)...\n" << std::flush;
        const auto start = std::chrono::steady_clock::now();
        recognize(chunks, o, files, result);
        transcribe::merge_exports(chunks, result);
        transcribe::check_cancelled();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Готово. Распознавание заняло " << std::fixed << std::setprecision(1)
                  << seconds / 60.0 << " мин.\n" << result.string() << '\n' << std::flush;
        transcribe::check_cancelled();
        if (!std::cout) throw std::runtime_error("Не удалось вывести итог обработки");
        metadata(result, details, "completed", 0);
        transcribe::commit_completion();
        // Cleanup is irreversible. Once success is committed, it must not turn
        // into an interrupted/failed run that falsely promises preserved audio.
        try {
            if (!o.keep) fs::remove_all(work);
        } catch (const std::exception& error) {
            std::cerr << "Предупреждение: Не удалось удалить рабочие файлы в " << work.string()
                      << ": " << error.what() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        const auto* process = dynamic_cast<const ProcessError*>(&error);
        const int code = transcribe::cancellation_signal() ? 128 + transcribe::cancellation_signal() : (process ? process->code : 1);
        if (!result.empty()) {
            try { metadata(result, details, transcribe::cancellation_signal() ? "interrupted" : "failed", code); }
            catch (const std::exception& failure) { std::cerr << "Не удалось сохранить статус: " << failure.what() << '\n'; }
        }
        std::cerr << "Ошибка: " << error.what() << '\n';
        if (!result.empty()) std::cerr << "Промежуточные файлы оставлены в " << result.string() << '\n';
        return code;
    }
}
