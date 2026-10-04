#include "cli.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include "platform.hpp"

namespace fs = std::filesystem;
namespace transcribe {
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
        throw std::runtime_error("Не удалось прочитать " + path_utf8(path));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

int positive_integer(std::string_view text) {
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 1 || value > 256)
        throw std::runtime_error("Число должно быть целым от 1 до 256");
    return value;
}

void require_file(const fs::path& path, const std::string& hint) {
    if (!fs::is_regular_file(path) || fs::file_size(path) == 0)
        throw std::runtime_error("Нет файла: " + path_utf8(path) + "\n" + hint);
}

namespace {
bool consumes_value(std::string_view arg) {
    return arg == "--model" || arg == "--threads" || arg == "--chunks" || arg == "--jobs" ||
            arg == "--out" || arg == "--cache-dir" || arg == "--cache-limit-gib" ||
            arg == "--prompt" || arg == "--cookies" || arg == "--cookies-from-browser";
}
}
bool machine_requested(std::span<const std::string_view> args) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--") break;
        if (args[i] == "--machine") return true;
        if (consumes_value(args[i])) ++i;
    }
    return false;
}
CliAction cli_action(std::span<const std::string_view> args) {
    if (args.empty()) return CliAction::usage;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--") break;
        if (arg == "--help") return CliAction::help;
        if (arg == "--version") return CliAction::version;
        if (consumes_value(arg)) ++i;
    }
    return CliAction::run;
}
bool oversubscribed(const Options& o, int logical_cpus) {
    return logical_cpus > 0 && o.jobs * o.threads > logical_cpus;
}
Inputs validate_inputs(const Options& o, const ValidationPaths& paths) {
    const auto& root = paths.root;
    const auto& cwd = paths.cwd;
    const auto absolute = [&](const fs::path& path) { return path.is_absolute() ? path : cwd / path; };
    Inputs files;
    files.engine = absolute(root) / "whisper.cpp/build/bin/whisper-cli";
#ifdef _WIN32
    files.engine += ".exe";
    if (!executable_file(files.engine)) files.engine = executable_directory() / "tools/whisper-cli.exe";
#endif
    if (!executable_file(files.engine))
#ifdef _WIN32
        throw std::runtime_error("Нет tools/whisper-cli.exe. Распакуйте весь Windows ZIP заново");
#else
        throw std::runtime_error("Нет whisper-cli. Сначала выполни bash install.sh");
#endif
    const std::string selected = o.model.empty() ? read_line(absolute(root) / "default-model") : o.model;
    files.model_selection = selected;
    files.model = selected == "small" || selected == "medium" || selected == "turbo"
        ? absolute(root) / "models" / ("ggml-" + preset(selected) + ".bin") : absolute(utf8_path(selected));
    #ifdef _WIN32
    require_file(files.model, "Скачайте или импортируйте модель на вкладке «Модели» в GUI");
#else
    require_file(files.model, "Скачай модель: bash scripts/download-model.sh small|medium|turbo");
#endif
    files.vad_model = absolute(root) / "models/ggml-silero-v6.2.0.bin";
    if (o.vad) require_file(files.vad_model, "Повтори установку или добавь --no-vad");
    files.url = o.input.starts_with("https://") || o.input.starts_with("http://");
    if (!files.url) {
        files.input = absolute(utf8_path(o.input));
        require_file(files.input, "Проверь имя локального видео/аудиофайла");
    }
    if (!o.cookies.empty()) {
        files.cookies = path_utf8(absolute(utf8_path(o.cookies)));
        require_file(utf8_path(files.cookies), "Проверь путь к файлу cookies");
    }
    return files;
}
Options parse_arguments(std::span<const std::string_view> args, int physical_cpus) {
    Options o;
    bool positional = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string arg(args[i]);
        const auto value = [&]() -> std::string {
            ++i;
            if (i >= args.size() || args[i].empty()) throw std::runtime_error("Нужно значение для " + arg);
            return std::string(args[i]);
        };
        if (!positional && arg == "--") positional = true;
        else if (!positional && arg == "--model") o.model = value();
        else if (!positional && arg == "--threads") o.threads = positive_integer(value());
        else if (!positional && arg == "--chunks") o.chunks = positive_integer(value());
        else if (!positional && arg == "--jobs") o.jobs = positive_integer(value());
        else if (!positional && arg == "--out") o.output = utf8_path(value());
        else if (!positional && arg == "--cache-dir") o.cache_dir = utf8_path(value());
        else if (!positional && arg == "--cache-limit-gib") {
            const auto text = value();
            uint64_t gib{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), gib);
            if (error != std::errc{} || end != text.data() + text.size() || gib == 0 || gib > UINT64_MAX / (uint64_t{1024} * 1024 * 1024))
                throw std::runtime_error("--cache-limit-gib: нужно положительное целое число ГиБ без переполнения");
            o.cache_limit = gib * 1024 * 1024 * 1024;
        }
        else if (!positional && arg == "--cookies-from-browser") o.browser = value();
        else if (!positional && arg == "--cookies") o.cookies = value();
        else if (!positional && arg == "--prompt") o.prompt = value();
        else if (!positional && arg == "--no-vad") o.vad = false;
        else if (!positional && arg == "--keep-audio") o.keep = true;
        else if (!positional && arg == "--machine") o.machine = true;
        else if (!positional && arg == "--no-progress") o.progress = false;
        else if (!positional && arg == "--no-cache") o.cache = false;
        else if (!positional && arg == "--refresh-cache") o.refresh_cache = true;
        else if (!positional && arg.starts_with('-'))
            throw std::runtime_error("Неизвестный параметр: " + arg);
        else if (o.input.empty()) o.input = arg;
        else throw std::runtime_error("За один запуск можно передать один файл или URL");
    }
    if (o.input.empty()) throw std::runtime_error("Укажи URL или путь к файлу; справка: transcribe --help");
    if (o.input.starts_with('[') && o.input.find("](") != std::string::npos)
        throw std::runtime_error("Вставь обычный URL, без Markdown-разметки [ссылка](ссылка)");
    if (!o.browser.empty() && !o.cookies.empty())
        throw std::runtime_error("Выбери --cookies либо --cookies-from-browser");
    if (!o.cache && o.refresh_cache) throw std::runtime_error("--no-cache и --refresh-cache несовместимы");
    if (o.jobs > o.chunks) throw std::runtime_error("--jobs не должен превышать --chunks");
    const int cpus = std::clamp(physical_cpus, 1, 256);
    if (!o.jobs) o.jobs = std::min({2, o.chunks, cpus});
    if (!o.threads) o.threads = std::max(1, cpus / o.jobs);
    return o;
}
}
