#include "support.hpp"
#include "cli.hpp"
#include <array>

using namespace test;
using namespace transcribe;
namespace {
template<class F> void rejected(F run, std::string_view message) {
    try { run(); }
    catch (const std::runtime_error& error) { CHECK(contains(error.what(), message)); return; }
    throw std::runtime_error("Expected validation error");
}
Options parse(std::initializer_list<std::string_view> args, int cpus = 8) {
    return parse_arguments({args.begin(), args.size()}, cpus);
}
struct Files {
    Temp temp;
    fs::path root = temp.path / "app data", input = temp.path / "Лекция 1.wav";
    Options options;
    Files() {
        write(root / "whisper.cpp/build/bin/whisper-cli", "fixture");
        fs::permissions(root / "whisper.cpp/build/bin/whisper-cli", fs::perms::owner_all);
        for (const auto* model : {"medium-q5_0", "small-q5_1", "large-v3-turbo-q5_0", "silero-v6.2.0"})
            write(root / "models" / (std::string("ggml-") + model + ".bin"), "model");
        write(root / "default-model", "medium\r\n"); write(input, "audio");
        options = parse({"Лекция 1.wav"});
    }
    Inputs validate() { return validate_inputs(options, {root, temp.path}); }
};
}
int main() {
    Suite suite;
    suite.add("numeric_boundaries_and_all_numeric_options", [] {
        for (auto flag : {"--threads", "--chunks", "--jobs"}) for (auto number : {"1", "256"}) {
            const std::array<std::string_view, 5> args{"--chunks", "256", flag, number, "audio.wav"};
            const auto o = parse_arguments(args, 8);
            CHECK((flag == std::string_view("--threads") ? o.threads : flag == std::string_view("--jobs") ? o.jobs : o.chunks) == std::stoi(number));
        }
        for (auto flag : {"--threads", "--chunks", "--jobs"})
            for (auto number : {"0", "257", "-1", "+1", "1.5", " 1", "1 ", "1x", "999999999999999999999"})
                rejected([&] { (void)parse({flag, number, "audio.wav"}); }, "Число должно");
    });
    suite.add("missing_and_empty_values", [] {
        for (auto flag : {"--model", "--threads", "--chunks", "--jobs", "--out", "--prompt", "--cookies", "--cookies-from-browser"}) {
            rejected([&] { (void)parse({"audio.wav", flag}); }, "Нужно значение");
            rejected([&] { (void)parse({flag, "", "audio.wav"}); }, "Нужно значение");
        }
    });
    suite.add("invalid_combinations_and_sources", [] {
        rejected([] { (void)parse({}); }, "Укажи URL");
        rejected([] { (void)parse({"--unknown", "audio.wav"}); }, "Неизвестный параметр");
        rejected([] { (void)parse({"one.wav", "two.wav"}); }, "один файл");
        rejected([] { (void)parse({"[video](https://example.org)"}); }, "Markdown");
        rejected([] { (void)parse({"--cookies", "file", "--cookies-from-browser", "firefox", "audio.wav"}); }, "Выбери");
        rejected([] { (void)parse({"--chunks", "2", "--jobs", "3", "audio.wav"}); }, "не должен");
    });
    suite.add("values_terminator_and_literal_shell_characters", [] {
        const auto o = parse({"--out", "Результаты с пробелами", "--prompt", "$(touch PWNED); термин", "--no-vad", "--keep-audio", "--", "--version"});
        CHECK(o.input == "--version" && o.output == "Результаты с пробелами");
        CHECK(o.prompt == "$(touch PWNED); термин" && !o.vad && o.keep);
    });
    suite.add("help_version_precedence_and_values", [] {
        CHECK(cli_action({}) == CliAction::usage);
        const auto action = [](std::initializer_list<std::string_view> args) { return cli_action({args.begin(), args.size()}); };
        CHECK(action({"--unknown", "--help"}) == CliAction::help);
        CHECK(action({"--version", "--help"}) == CliAction::version);
        CHECK(action({"--help", "--version"}) == CliAction::help);
        CHECK(action({"--", "--help"}) == CliAction::run);
        for (auto flag : {"--model", "--threads", "--chunks", "--jobs", "--out", "--prompt", "--cookies", "--cookies-from-browser"})
            CHECK(action({flag, "--help", "audio.wav"}) == CliAction::run);
    });
    suite.add("deterministic_cpu_budget_and_explicit_override", [] {
        CHECK(parse({"audio.wav"}, 8).threads == 8);
        auto o = parse({"--chunks", "10", "audio.wav"}, 8); CHECK(o.jobs == 2 && o.threads == 4);
        o = parse({"--chunks", "10", "audio.wav"}, 1); CHECK(o.jobs == 1 && o.threads == 1);
        o = parse({"audio.wav"}, 0); CHECK(o.jobs == 1 && o.threads == 1);
        o = parse({"--chunks", "4", "--jobs", "4", "--threads", "8", "audio.wav"}, 2);
        CHECK(o.jobs == 4 && o.threads == 8 && oversubscribed(o, 2));
        CHECK(!oversubscribed(o, 32) && !oversubscribed(o, 0));
    });
    suite.add("file_preflight_resolves_paths_without_creating_outputs", [] {
        Files f; const auto before = fs::current_path();
        const auto files = f.validate(); CHECK(files.input == f.input && !files.url);
        CHECK(files.model == f.root / "models/ggml-medium-q5_0.bin");
        CHECK(fs::current_path() == before && !fs::exists(f.temp.path / "transcripts"));
        for (const auto& [preset, filename] : std::vector<std::pair<std::string, std::string>>{
                 {"small", "ggml-small-q5_1.bin"}, {"medium", "ggml-medium-q5_0.bin"}, {"turbo", "ggml-large-v3-turbo-q5_0.bin"}}) {
            f.options.model = preset; CHECK(f.validate().model == f.root / "models" / filename);
        }
        write(f.temp.path / "custom model.bin", "custom"); f.options.model = "custom model.bin";
        CHECK(f.validate().model == f.temp.path / "custom model.bin");
        f.options.cookies = "cookies.txt"; write(f.temp.path / "cookies.txt", "cookies");
        CHECK(f.validate().cookies == (f.temp.path / "cookies.txt").string());
        f.options.input = "https://example.org/video"; CHECK(f.validate().url && f.validate().input.empty());
    });
    for (const auto& name : {"input", "model", "vad", "cookies"}) for (const auto& defect : {"missing", "empty", "directory"}) {
        suite.add(std::string("preflight_") + name + '_' + defect, [=] {
            Files f;
            fs::path path = f.input;
            if (name == std::string_view("model")) path = f.root / "models/ggml-medium-q5_0.bin";
            if (name == std::string_view("vad")) path = f.root / "models/ggml-silero-v6.2.0.bin";
            if (name == std::string_view("cookies")) { path = f.temp.path / "cookies.txt"; f.options.cookies = path.string(); write(path, "cookies"); }
            fs::remove(path);
            if (defect == std::string_view("empty")) write(path, "");
            if (defect == std::string_view("directory")) fs::create_directory(path);
            rejected([&] { (void)f.validate(); }, "Нет файла");
            CHECK(!fs::exists(f.temp.path / "transcripts"));
        });
    }
    suite.add("preflight_engine_directory_is_rejected", [] {
        Files f;
        const auto engine = f.root / "whisper.cpp/build/bin/whisper-cli";
        fs::remove(engine); fs::create_directory(engine);
        rejected([&] { (void)f.validate(); }, "Нет whisper-cli");
        CHECK(!fs::exists(f.temp.path / "transcripts"));
    });
    suite.add("missing_engine_and_default_selection", [] {
        Files f; fs::permissions(f.root / "whisper.cpp/build/bin/whisper-cli", fs::perms::owner_read);
        rejected([&] { (void)f.validate(); }, "Нет whisper-cli");
        fs::permissions(f.root / "whisper.cpp/build/bin/whisper-cli", fs::perms::owner_all);
        fs::remove(f.root / "default-model"); rejected([&] { (void)f.validate(); }, "Не удалось прочитать");
        f.options.model = "medium"; fs::remove(f.root / "models/ggml-silero-v6.2.0.bin");
        f.options.vad = false; CHECK(f.validate().model == f.root / "models/ggml-medium-q5_0.bin");
    });
    return suite.run();
}
