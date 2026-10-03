#include "support.hpp"
#include "process.hpp"
#include "exports.hpp"

using namespace test;
int main(int argc, char** argv) {
    try {
        transcribe::install_signal_handlers();
        fs::path binary = TRANSCRIBE_BINARY, home, artifacts, manifest_path = fs::path(PROJECT_SOURCE) / "scripts/models.tsv";
        const auto app = test::getenv("TRANSCRIBE_HOME");
        home = app.empty() ? fs::path(test::getenv("HOME")) / ".local/share/transcribe" : fs::path(app);
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") { std::cout << "smoke_real [--binary ПУТЬ] [--app-home КАТАЛОГ] [--manifest ФАЙЛ] [--artifacts НОВЫЙ-КАТАЛОГ]\n"; return 0; }
            if (i + 1 >= argc) throw std::runtime_error("Нужно значение параметра");
            if (arg == "--binary") binary = argv[++i];
            else if (arg == "--app-home") home = argv[++i];
            else if (arg == "--manifest") manifest_path = argv[++i];
            else if (arg == "--artifacts") artifacts = argv[++i];
            else throw std::runtime_error("Неизвестный параметр smoke_real");
        }
        binary = fs::absolute(binary); home = fs::absolute(home);
        if (!fs::is_regular_file(binary) || access(binary.c_str(), X_OK)) throw std::runtime_error("Нет исполняемой программы: " + binary.string() + "\nСоберите её через CMake или укажите --binary ПУТЬ.");
        if (!fs::is_directory(home)) throw std::runtime_error("Каталог установки не найден: " + home.string() + "\nУкажите --app-home ПУТЬ или выполните bash install.sh medium.");
        const auto engine = home / "whisper.cpp/build/bin/whisper-cli", sample = home / "whisper.cpp/samples/jfk.wav";
        if (!fs::is_regular_file(engine) || access(engine.c_str(), X_OK)) throw std::runtime_error("Нет исполняемого whisper-cli: " + engine.string() + "\nПроверьте --app-home или выполните bash install.sh medium.");
        if (!fs::is_regular_file(sample)) throw std::runtime_error("Нет технического примера: " + sample.string());
        const auto ffmpeg = which("ffmpeg");
        std::istringstream manifest_file(read(manifest_path)); std::string line; std::map<std::string, fs::path> models;
        while (std::getline(manifest_file, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream row(line); std::string selection, filename, repository, revision, expected, extra;
            if (!(row >> selection >> filename >> repository >> revision >> expected) || (row >> extra)) throw std::runtime_error("Некорректный манифест моделей");
            if (selection != "medium" && selection != "vad") continue;
            const auto model = home / "models" / filename;
            if (!fs::is_regular_file(model)) throw std::runtime_error("Нет модели: " + model.string() + "\nПроверьте --app-home или выполните bash install.sh medium.");
            const auto digest = invoke({which("sha256sum").string(), model.string()}); success(digest);
            if (digest.out.substr(0, 64) != expected) throw std::runtime_error("SHA-256 модели не совпадает: " + model.string());
            models[selection] = model;
        }
        if (models.size() != 2) throw std::runtime_error("В манифесте нужны medium и Silero VAD");
        Temp temp;
        const auto workspace = artifacts.empty() ? temp.path : fs::absolute(artifacts);
        if (!artifacts.empty() && !fs::create_directory(workspace)) throw std::runtime_error("Каталог --artifacts уже существует");
        const auto data = workspace / "app data", audio = workspace / "technical sample.wav", output = workspace / "results";
        test::link(engine, data / "whisper.cpp/build/bin/whisper-cli");
        for (const auto& [selection, model] : models) { (void)selection; test::link(model, data / "models" / model.filename()); }
        write(data / "default-model", "medium\n");
        const auto converted = invoke({ffmpeg.string(), "-nostdin", "-hide_banner", "-loglevel", "error", "-i", sample.string(), "-t", "5", "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", audio.string()});
        if (converted.code) throw std::runtime_error("ffmpeg завершился с кодом " + std::to_string(converted.code) + ": " + converted.err);
        transcribe::check_cancelled();
        const auto original = read(audio); const auto start = std::chrono::steady_clock::now();
        Child child({binary.string(), "--out", output.string(), audio.string()}, {{"TRANSCRIBE_HOME", data.string()}});
        const auto completed = child.wait(std::chrono::seconds(300), [] { transcribe::check_cancelled(); }); success(completed);
        const auto result = single(output);
        for (const char* file : {"transcripts/transcript.txt", "transcripts/transcript.srt", "transcripts/transcript.vtt", "source.txt"}) CHECK(fs::is_regular_file(result / file));
        CHECK(!fs::exists(result / "audio")); CHECK(read(audio) == original); CHECK(contains(read(result / "source.txt"), "Статус: completed"));
        check_exports(result / "transcripts", 5000);
        std::cout << "Real smoke passed: medium-q5_0, Silero VAD 6.2.0, ru, 5 s JFK; "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ошибка проверки: " << error.what() << '\n';
        return transcribe::cancellation_signal() ? 128 + transcribe::cancellation_signal() : 1;
    }
}
