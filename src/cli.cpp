#include "cli.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

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
        throw std::runtime_error("Не удалось прочитать " + path.string());
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
        throw std::runtime_error("Нет файла: " + path.string() + "\n" + hint);
}

CliAction cli_action(std::span<const std::string_view> args) {
    if (args.empty()) return CliAction::usage;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto arg = args[i];
        if (arg == "--") break;
        if (arg == "--help") return CliAction::help;
        if (arg == "--version") return CliAction::version;
        if (arg == "--model" || arg == "--threads" || arg == "--chunks" || arg == "--jobs" ||
            arg == "--out" || arg == "--cache-dir" || arg == "--cache-limit-gib" ||
            arg == "--prompt" || arg == "--cookies" || arg == "--cookies-from-browser") ++i;
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
    if (!fs::is_regular_file(files.engine) || access(files.engine.c_str(), X_OK) != 0)
        throw std::runtime_error("Нет whisper-cli. Сначала выполни bash install.sh");
    const std::string selected = o.model.empty() ? read_line(absolute(root) / "default-model") : o.model;
    files.model = selected == "small" || selected == "medium" || selected == "turbo"
        ? absolute(root) / "models" / ("ggml-" + preset(selected) + ".bin") : absolute(selected);
    require_file(files.model, "Скачай модель: bash scripts/download-model.sh small|medium|turbo");
    files.vad_model = absolute(root) / "models/ggml-silero-v6.2.0.bin";
    if (o.vad) require_file(files.vad_model, "Повтори установку или добавь --no-vad");
    files.url = o.input.starts_with("https://") || o.input.starts_with("http://");
    if (!files.url) {
        files.input = absolute(o.input);
        require_file(files.input, "Проверь имя локального видео/аудиофайла");
    }
    if (!o.cookies.empty()) {
        files.cookies = absolute(o.cookies).string();
        require_file(files.cookies, "Проверь путь к файлу cookies");
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
        else if (!positional && arg == "--out") o.output = value();
        else if (!positional && arg == "--cache-dir") o.cache_dir = value();
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
