#include "support.hpp"
using namespace test;
struct Fixture {
    Temp temp;
    fs::path root = temp.path, binary = root / "transcribe", home = root / "app data", manifest_path = root / "models.tsv";
    fs::path engine = home / "whisper.cpp/build/bin/whisper-cli", sample = home / "whisper.cpp/samples/jfk.wav";
    fs::path medium = home / "models/ggml-medium-q5_0.bin", vad = home / "models/ggml-silero-v6.2.0.bin";
    Env env{{"HOME", (root / "user").string()}, {"TRANSCRIBE_HOME", ""}};
    Fixture() {
        test::link(MOCK_BINARY, binary); test::link(MOCK_BINARY, engine);
        write(sample, "invalid audio fixture"); write(medium, "smoke model fixture\n"); write(vad, "smoke model fixture\n");
        manifest(fs::path(PROJECT_SOURCE) / "scripts/models.tsv", manifest_path, "smoke model fixture\n");
    }
    Capture invoke(std::vector<std::string> args = {}, const Env& extra = {}) {
        args.insert(args.begin(), {SMOKE_BINARY, "--binary", binary.string(), "--manifest", manifest_path.string()});
        auto variables = env; for (const auto& [key, value] : extra) variables[key] = value;
        return test::invoke(args, variables, root);
    }
    Capture custom() { return invoke({"--app-home", home.string()}); }
    void error(const Capture& result, std::string_view message) { CHECK(result.code == 1 && contains(result.err, message)); }
};
int main() {
    Suite suite;
    const auto add = [&](std::string_view name, auto run) { suite.add(std::string(name), [run = std::move(run)] { Fixture f; run(f); }); };
    add("missing_default_installation_explains_app_home", [](Fixture& f) {
        const auto result = f.invoke(); f.error(result, "Каталог установки не найден");
        CHECK(contains(result.err, ".local/share/transcribe") && contains(result.err, "--app-home") && contains(result.err, "bash install.sh medium"));
        CHECK(!fs::exists(f.root / "user"));
    });
    add("missing_medium_explains_installation", [](Fixture& f) { fs::remove(f.medium); const auto result = f.custom(); f.error(result, "ggml-medium-q5_0.bin"); CHECK(contains(result.err, "bash install.sh medium")); });
    add("missing_vad_is_reported", [](Fixture& f) { fs::remove(f.vad); f.error(f.custom(), "ggml-silero-v6.2.0.bin"); });
    add("missing_binary_explains_build", [](Fixture& f) { fs::remove(f.binary); const auto result = f.custom(); f.error(result, "Нет исполняемой программы"); CHECK(contains(result.err, "CMake")); });
    add("missing_engine_is_reported", [](Fixture& f) { fs::remove(f.engine); f.error(f.custom(), "Нет исполняемого whisper-cli"); });
    add("missing_sample_is_reported", [](Fixture& f) { fs::remove(f.sample); f.error(f.custom(), "samples/jfk.wav"); });
    add("custom_app_home_overrides_environment", [](Fixture& f) {
        fs::remove(f.vad); const auto result = f.invoke({"--app-home", f.home.string()}, {{"TRANSCRIBE_HOME", (f.root / "wrong home").string()}});
        f.error(result, f.vad.string()); CHECK(!contains(result.err, "wrong home"));
    });
    add("corrupt_model_is_preserved", [](Fixture& f) { write(f.medium, "corrupt model"); f.error(f.custom(), "SHA-256"); CHECK(read(f.medium) == "corrupt model"); });
    add("ffmpeg_failure_is_reported", [](Fixture& f) { f.error(f.custom(), "ffmpeg"); CHECK(read(f.sample) == "invalid audio fixture"); });
    add("artifacts_directory_refuses_existing_path", [](Fixture& f) {
        const auto artifacts = f.root / "diagnostics"; write(artifacts / "keep", "original");
        f.error(f.invoke({"--app-home", f.home.string(), "--artifacts", artifacts.string()}), "уже существует");
        CHECK(read(artifacts / "keep") == "original");
    });
    add("artifacts_directory_preserves_failed_run", [](Fixture& f) {
        wav(f.sample); const auto artifacts = f.root / "diagnostics";
        CHECK(f.invoke({"--app-home", f.home.string(), "--artifacts", artifacts.string()}).code == 1);
        CHECK(fs::is_regular_file(artifacts / "technical sample.wav"));
        CHECK(fs::is_directory(artifacts / "app data"));
    });
    return suite.run();
}
