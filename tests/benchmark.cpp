#include "support.hpp"
#include "process.hpp"
#include <iomanip>
#include <limits>
#include <set>
using namespace test;
namespace {
struct Memory { uint64_t rss = 0, swap = 0; };
Memory tree_memory(pid_t root, std::set<pid_t>& visited) {
    if (!visited.insert(root).second) return {};
    const auto proc = fs::path("/proc") / std::to_string(root);
    std::ifstream status(proc / "status"); std::string line; Memory memory;
    while (std::getline(status, line)) {
        std::istringstream row(line); std::string name; uint64_t kb{};
        if (row >> name >> kb) {
            if (name == "VmRSS:") memory.rss += kb;
            if (name == "VmSwap:") memory.swap += kb;
        }
    }
    std::ifstream children(proc / "task" / std::to_string(root) / "children"); pid_t child{};
    while (children >> child) {
        const auto value = tree_memory(child, visited); memory.rss += value.rss; memory.swap += value.swap;
    }
    return memory;
}
struct Measurement { double seconds; Memory peak; };
Measurement measure(const std::vector<std::string>& args, const Env& env) {
    const auto started = std::chrono::steady_clock::now(); Child child(args, env); Memory peak;
    const auto deadline = started + std::chrono::hours(12);
    while (child.running()) {
        transcribe::check_cancelled();
        std::set<pid_t> visited; const auto value = tree_memory(child.pid, visited);
        peak.rss = std::max(peak.rss, value.rss); peak.swap = std::max(peak.swap, value.swap);
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Benchmark run timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    success(child.wait());
    return {std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(), peak};
}
}
int main(int argc, char** argv) {
    try {
        transcribe::install_signal_handlers();
        fs::path input, output = "benchmark-results", binary = TRANSCRIBE_BINARY;
        std::string model = "medium", app_home; bool allow_short = false, vad = true;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "benchmark --input ФАЙЛ [--out КАТАЛОГ] [--binary ПУТЬ] [--app-home КАТАЛОГ]\n"
                             "          [--model medium|small|turbo|ПУТЬ.bin] [--no-vad] [--allow-short]\n"
                             "Прогрев, затем 3 измерения каждого варианта; запись не менее 10 минут.\n";
                return 0;
            }
            if (arg == "--allow-short") { allow_short = true; continue; }
            if (arg == "--no-vad") { vad = false; continue; }
            if (i + 1 >= argc) throw std::runtime_error("Нужно значение параметра benchmark");
            if (arg == "--input") input = argv[++i];
            else if (arg == "--out") output = argv[++i];
            else if (arg == "--binary") binary = argv[++i];
            else if (arg == "--app-home") app_home = argv[++i];
            else if (arg == "--model") model = argv[++i];
            else throw std::runtime_error("Неизвестный параметр benchmark");
        }
        if (input.empty() || !fs::is_regular_file(input)) throw std::runtime_error("Укажите локальную запись через --input");
        const auto probe = invoke({which("ffprobe").string(), "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=duration:format=duration", "-of", "default=noprint_wrappers=1:nokey=1", fs::absolute(input).string()}); success(probe);
        std::istringstream durations(probe.out); std::string line; double duration = 0;
        while (std::getline(durations, line)) {
            try { const double value = std::stod(line); if (std::isfinite(value)) duration = std::max(duration, value); } catch (const std::exception&) {}
        }
        if (duration <= 0 || (!allow_short && duration < 600)) throw std::runtime_error("Для сравнения нужна запись не менее 10 минут; --allow-short разрешает только технический эксперимент");
        fs::create_directories(output);
        // Isolate each invocation, retaining text and boundary metadata for review.
        std::string pattern = (fs::absolute(output) / "run-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("Не удалось создать каталог сравнения");
        const fs::path root = pattern; Env env;
        if (!app_home.empty()) env["TRANSCRIBE_HOME"] = fs::absolute(app_home).string();
        std::ofstream csv(root / "measurements.csv"), report(root / "report.md");
        csv.exceptions(std::ios::badbit | std::ios::failbit); report.exceptions(std::ios::badbit | std::ios::failbit);
        csv << "chunks,jobs,threads,repeat,seconds,peak_tree_rss_kib,peak_tree_swap_kib\n" << std::fixed << std::setprecision(3);
        report << "# Сравнение распознавания\n\nИсточник: " << fs::absolute(input).string() << "\n\nДлительность: " << duration
               << " с. Модель: " << model << "; VAD: " << (vad ? "on" : "off") << ".\n\n"
               << "Время включает подготовку аудио, поиск пауз, нарезку, загрузку моделей и объединение. "
                  "RAM — максимум суммы RSS дерева процессов, выборка каждые 10 мс; общие страницы могут учитываться несколько раз. "
                  "Swap — максимальная сумма VmSwap. Результаты запусков сохранены для проверки границ.\n\n";
        if (duration < 600) report << "Короткий технический пример: результат нельзя переносить на длинные русские лекции.\n\n";
        Temp warm; const auto warm_audio = warm.path / "warm.wav";
        success(invoke({which("ffmpeg").string(), "-nostdin", "-hide_banner", "-loglevel", "error", "-i", fs::absolute(input).string(),
            "-t", "5", "-vn", "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", warm_audio.string()}));
        const auto command = [&](int chunks, int jobs, int threads, const fs::path& source, const fs::path& results) {
            std::vector<std::string> args{fs::absolute(binary).string(), "--model", model, "--chunks", std::to_string(chunks), "--jobs", std::to_string(jobs),
                "--threads", std::to_string(threads), "--out", results.string()};
            if (!vad) args.push_back("--no-vad");
            args.push_back(source.string());
            return args;
        };
        std::cout << "Прогрев модели...\n" << std::flush;
        (void)measure(command(1, 1, 6, warm_audio, warm.path / "results"), env);
        struct Variant { int chunks, jobs, threads; };
        const std::vector<Variant> variants{{1, 1, 4}, {1, 1, 6}, {1, 1, 8}, {1, 1, 12}, {10, 2, 3}, {10, 3, 2}};
        report << "| Части | Работники | Потоки каждого | Медиана, с | Пиковая сумма RSS, МиБ | Swap, МиБ |\n"
                  "|---|---|---|---|---|---|\n" << std::fixed << std::setprecision(3);
        double best_single = std::numeric_limits<double>::max(), best_split = best_single;
        for (const auto variant : variants) {
            std::vector<double> times; Memory maximum;
            for (int repeat = 1; repeat <= 3; ++repeat) {
                std::cout << "Части " << variant.chunks << ", работники " << variant.jobs << ", потоки " << variant.threads << ": измерение " << repeat << "/3\n" << std::flush;
                const auto results = root / (std::to_string(variant.chunks) + "-" + std::to_string(variant.jobs) + "-" + std::to_string(variant.threads)) / std::to_string(repeat);
                const auto measured = measure(command(variant.chunks, variant.jobs, variant.threads, fs::absolute(input), results), env);
                times.push_back(measured.seconds); maximum.rss = std::max(maximum.rss, measured.peak.rss); maximum.swap = std::max(maximum.swap, measured.peak.swap);
                csv << variant.chunks << ',' << variant.jobs << ',' << variant.threads << ',' << repeat << ',' << measured.seconds << ',' << measured.peak.rss << ',' << measured.peak.swap << '\n'; csv.flush();
            }
            std::sort(times.begin(), times.end());
            auto& best = variant.chunks == 1 ? best_single : best_split; best = std::min(best, times[1]);
            report << '|' << variant.chunks << '|' << variant.jobs << '|' << variant.threads << '|' << times[1] << '|' << maximum.rss / 1024.0 << '|' << maximum.swap / 1024.0 << "|\n"; report.flush();
        }
        const double gain = 100 * (1 - best_split / best_single);
        report << "\nИзменение времени лучшего дробления относительно лучшего цельного запуска: " << gain << "% (положительное — быстрее).\n\n"
                  "Качество автоматически не оценивалось. Сравните речь и субтитры возле всех девяти границ. "
                  "Рекомендация дробления требует выигрыша более 15% без заметного ухудшения качества.\n";
        csv.close(); report.close(); std::cout << "Отчёт: " << (root / "report.md").string() << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка сравнения: " << error.what() << '\n';
        return transcribe::cancellation_signal() ? 128 + transcribe::cancellation_signal() : 1;
    }
}
