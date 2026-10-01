// Linux, C++23. Запускает yt-dlp, FFmpeg и whisper-cli без оболочки.
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;
namespace fs = std::filesystem;

struct ProcessError : std::runtime_error {
    int code;
    ProcessError(const std::string& message, int value)
        : std::runtime_error(message), code(value) {}
};

// argv передаётся напрямую процессу: пробелы, кавычки, & и $ в URL/путях
// не становятся командами shell.
void run(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    std::cout.flush();
    pid_t pid{};
    const int error = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (error != 0) {
        throw ProcessError("Не удалось запустить " + args[0] + ": " +
                           std::strerror(error), 127);
    }
    int status{};
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) throw std::runtime_error("Ошибка waitpid");
    }
    if (WIFSIGNALED(status)) {
        throw ProcessError("Процесс " + args[0] + " прерван сигналом " +
                           std::to_string(WTERMSIG(status)), 128 + WTERMSIG(status));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        throw ProcessError(args[0] + " завершился с кодом " + std::to_string(code), code);
    }
}

fs::path app_home() {
    if (const char* p = std::getenv("VKLECTURE_HOME"); p && *p)
        return fs::absolute(p);
    const char* p = std::getenv("HOME");
    if (!p || !*p) throw std::runtime_error("Не задан HOME или VKLECTURE_HOME");
    return fs::path(p) / ".local/share/vklecture";
}

std::string preset(std::string_view name) {
    if (name == "small") return "small-q5_1";
    if (name == "medium") return "medium-q5_0";
    if (name == "turbo") return "large-v3-turbo-q5_0";
    return std::string(name);
}

std::string read_line(const fs::path& path) {
    std::ifstream stream(path);
    std::string line;
    if (!std::getline(stream, line))
        throw std::runtime_error("Не удалось прочитать " + path.string());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

int positive_integer(std::string_view text) {
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > 256)
        throw std::runtime_error("Число потоков должно быть целым от 1 до 256");
    return value;
}

void help() {
    std::cout << R"(vklecture — расшифровка русской речи на CPU

Использование:
  vklecture [параметры] "URL или путь к файлу"

Параметры:
  --model small|medium|turbo|ПУТЬ.bin  Модель (по умолчанию выбранная при установке)
  --threads N                        Потоки CPU (по умолчанию не более 4)
  --out КАТАЛОГ                      Родительский каталог результатов
                                     (по умолчанию ./transcripts)
  --cookies-from-browser СПЕЦ         Например firefox или chromium:ПРОФИЛЬ
  --cookies ФАЙЛ                     Файл cookies в формате Netscape
  --prompt ТЕКСТ                     Краткий список терминов лекции
  --no-vad                           Отключить определение участков речи
  --keep-audio                       Оставить рабочие аудио/видеофайлы
  --help                             Эта справка
  --                                 Конец параметров

Результаты: transcript.txt, transcript.srt, transcript.vtt, source.txt.
Каждый запуск создаёт отдельный каталог; исходный локальный файл не удаляется.
)";
}

struct Options {
    std::string input, model, browser, cookies, prompt;
    fs::path output = "transcripts";
    int threads = static_cast<int>(std::min(4u, std::max(1u, std::thread::hardware_concurrency())));
    bool vad = true, keep = false;
};

Options parse(int argc, char** argv) {
    Options o;
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (++i >= argc || !*argv[i]) throw std::runtime_error("Нужно значение для " + arg);
            return argv[i];
        };
        if (!positional && arg == "--") positional = true;
        else if (!positional && arg == "--model") o.model = value();
        else if (!positional && arg == "--threads") o.threads = positive_integer(value());
        else if (!positional && arg == "--out") o.output = value();
        else if (!positional && arg == "--cookies-from-browser") o.browser = value();
        else if (!positional && arg == "--cookies") o.cookies = value();
        else if (!positional && arg == "--prompt") o.prompt = value();
        else if (!positional && arg == "--no-vad") o.vad = false;
        else if (!positional && arg == "--keep-audio") o.keep = true;
        else if (!positional && arg.starts_with('-'))
            throw std::runtime_error("Неизвестный параметр: " + arg);
        else if (o.input.empty()) o.input = arg;
        else throw std::runtime_error("За один запуск можно передать один файл или URL");
    }
    if (o.input.empty()) throw std::runtime_error("Укажи URL или путь к файлу; справка: vklecture --help");
    if (o.input.starts_with('[') && o.input.find("](") != std::string::npos)
        throw std::runtime_error("Вставь обычный URL, без Markdown-разметки [ссылка](ссылка)");
    if (!o.browser.empty() && !o.cookies.empty())
        throw std::runtime_error("Выбери --cookies либо --cookies-from-browser");
    return o;
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

void require_file(const fs::path& path, const std::string& hint) {
    if (!fs::is_regular_file(path) || fs::file_size(path) == 0)
        throw std::runtime_error("Нет файла: " + path.string() + "\n" + hint);
}

int main(int argc, char** argv) {
    fs::path result;
    try {
        if (argc == 1) { help(); return 2; }
        // --help действует и без установленного движка/модели.
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--") break;
            if (std::string_view(argv[i]) == "--help") { help(); return 0; }
            if (std::string_view(argv[i]) == "--model" || std::string_view(argv[i]) == "--threads" ||
                std::string_view(argv[i]) == "--out" || std::string_view(argv[i]) == "--prompt" ||
                std::string_view(argv[i]) == "--cookies" || std::string_view(argv[i]) == "--cookies-from-browser") ++i;
        }
        Options o = parse(argc, argv);
        const fs::path root = app_home();
        const fs::path engine = root / "whisper.cpp/build/bin/whisper-cli";
        if (access(engine.c_str(), X_OK) != 0)
            throw std::runtime_error("Нет whisper-cli. Сначала выполни bash install.sh");
        if (o.model.empty()) o.model = read_line(root / "default-model");
        fs::path model;
        if (o.model == "small" || o.model == "medium" || o.model == "turbo")
            model = root / "models" / ("ggml-" + preset(o.model) + ".bin");
        else model = fs::absolute(o.model);
        require_file(model, "Скачай модель: bash scripts/download-model.sh small|medium|turbo");
        const fs::path vad_model = root / "models/ggml-silero-v6.2.0.bin";
        if (o.vad) require_file(vad_model, "Повтори установку или добавь --no-vad");

        const bool url = o.input.starts_with("https://") || o.input.starts_with("http://");
        fs::path input;
        if (!url) {
            input = fs::absolute(o.input);
            require_file(input, "Проверь имя локального видео/аудиофайла");
        }
        if (!o.cookies.empty()) {
            o.cookies = fs::absolute(o.cookies).string();
            require_file(o.cookies, "Проверь путь к файлу cookies");
        }
        result = new_result_dir(fs::absolute(o.output));
        const fs::path work = result / "audio";
        fs::create_directory(work);
        std::cout << "Каталог результата: " << result.string() << '\n';
        if (url) {
            std::cout << "[1/3] Скачивание из VK/другого сайта...\n";
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
            run(args);
            input = read_line(path_file);
            require_file(input, "yt-dlp не сохранил аудио/видео");
        } else std::cout << "[1/3] Используется локальный файл\n";

        const fs::path wav = work / "lecture.wav";
        std::cout << "[2/3] WAV: моно, 16 кГц, PCM 16 бит...\n";
        run({"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             "-i", input.string(), "-map", "0:a:0", "-vn", "-ar", "16000",
             "-ac", "1", "-c:a", "pcm_s16le", wav.string()});
        require_file(wav, "Проверь, есть ли в видео аудиодорожка");

        std::cout << "[3/3] Русская речь, CPU, " << o.threads << " поток(а)...\n";
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::string> args = {
            engine.string(), "--model", model.string(), "--file", wav.string(),
            "--language", "ru", "--threads", std::to_string(o.threads), "--no-gpu",
            "--output-txt", "--output-srt", "--output-vtt", "--output-file",
            (result / "transcript").string(), "--print-progress"
        };
        if (o.vad) args.insert(args.end(), {"--vad", "--vad-model", vad_model.string()});
        if (!o.prompt.empty()) args.insert(args.end(), {"--prompt", o.prompt});
        run(args);
        // Пустой TXT допустим, если модель не обнаружила речи.
        for (const char* extension : {".txt", ".srt", ".vtt"}) {
            if (!fs::is_regular_file(result / (std::string("transcript") + extension)))
                throw std::runtime_error("whisper-cli не создал ожидаемый файл " + std::string(extension));
        }
        std::ofstream metadata(result / "source.txt");
        metadata.exceptions(std::ios::badbit | std::ios::failbit);
        metadata << "Источник: " << o.input << "\nМодель: " << model.string()
                 << "\nЯзык: ru\nПотоки: " << o.threads << "\nVAD: " << (o.vad ? "on" : "off") << '\n';
        metadata.close();
        if (!o.keep) fs::remove_all(work);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Готово. Распознавание заняло " << std::fixed << std::setprecision(1)
                  << seconds / 60.0 << " мин.\n" << result.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        if (!result.empty()) std::cerr << "Промежуточные файлы оставлены в " << result.string() << '\n';
        if (const auto* process = dynamic_cast<const ProcessError*>(&error)) return process->code;
        return 1;
    }
}
