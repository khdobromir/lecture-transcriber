#include "support.hpp"
#include <sys/file.h>

using namespace test;
constexpr std::string_view payload = "verified fixture model\n";
struct Fixture {
    Temp temp;
    fs::path directory = temp.path, project = directory / "Проект с пробелами", home = directory / "Пользователь";
    fs::path root = home / "app data", bin = directory / "tools", log = directory / "calls";
    fs::path command = home / ".local/bin/transcribe", default_model = root / "default-model", engine = root / "whisper.cpp/build/bin/whisper-cli";
    Env env;
    Fixture() {
        CHECK(geteuid() != 0);
        fs::create_directory(project);
        fs::copy_file(fs::path(PROJECT_SOURCE) / "install.sh", project / "install.sh");
        fs::copy(fs::path(PROJECT_SOURCE) / "scripts", project / "scripts", fs::copy_options::recursive);
        manifest(project / "scripts/models.tsv", project / "scripts/models.tsv", payload);
        for (const char* tool : {"dirname", "mkdir", "mv", "rm", "mktemp", "install", "sha256sum", "flock"}) test::link(which(tool), bin / tool);
        for (const char* tool : {"git", "cmake", "curl", "g++", "ffmpeg", "yt-dlp"}) test::link(MOCK_BINARY, bin / tool);
        env = {{"HOME", home.string()}, {"TRANSCRIBE_HOME", root.string()}, {"PATH", bin.string()},
               {"MOCK_LOG", log.string()}, {"TRANSCRIBE_BUILD_JOBS", ""}};
    }
    Capture invoke(const std::string& script = "install.sh", std::vector<std::string> args = {"small"}, const Env& extra = {}) {
        auto variables = env; for (const auto& [key, value] : extra) variables[key] = value;
        args.insert(args.begin(), {"/bin/bash", (project / script).string()}); return test::invoke(args, variables, directory);
    }
    std::vector<std::vector<std::string>> calls(std::string_view kind) const {
        std::vector<std::vector<std::string>> result;
        for (const auto& call : test::calls(log)) if (call.kind == kind) result.push_back(call.args);
        return result;
    }
    fs::path model() const { return root / "models/ggml-small-q5_1.bin"; }
    fs::path partial() const { return model().string() + ".part"; }
    void previous() { write(command, "old command"); write(default_model, "medium\n"); write(engine, "old engine"); }
    void preserved() const {
        CHECK(read(command) == "old command"); CHECK(read(default_model) == "medium\n"); CHECK(read(engine) == "old engine");
        for (const auto& file : fs::directory_iterator(root)) if (file.path().filename().string().starts_with(".install.")) CHECK(file.path().filename() == ".install.lock");
    }
};
int main() {
    Suite suite;
    const auto add = [&](std::string_view name, auto run) { suite.add(std::string(name), [run = std::move(run)] { Fixture f; run(f); }); };
    add("success_and_repeat_skip_download_and_clone", [](Fixture& f) {
        success(f.invoke()); success(f.invoke());
        CHECK(read(f.default_model) == "small\n"); CHECK(access(f.command.c_str(), X_OK) == 0); CHECK(access(f.engine.c_str(), X_OK) == 0);
        CHECK(f.calls("curl").size() == 2);
        const auto git = f.calls("git"); CHECK(std::count_if(git.begin(), git.end(), [](const auto& args) { return args[0] == "clone"; }) == 1);
        CHECK(read(f.model()) == payload);
        for (const auto& file : fs::directory_iterator(f.root)) if (file.path().filename().string().starts_with(".install.")) CHECK(file.path().filename() == ".install.lock");
    });
    add("invalid_arguments_create_no_directories", [](Fixture& f) {
        for (const auto& args : std::vector<std::vector<std::string>>{{"bad"}, {"small", "extra"}}) { CHECK(f.invoke("install.sh", args).code == 2); CHECK(!fs::exists(f.home)); }
        CHECK(f.invoke("install.sh", {"small"}, {{"TRANSCRIBE_BUILD_JOBS", "0"}}).code == 2); CHECK(!fs::exists(f.home));
    });
    add("default_data_directory_without_override", [](Fixture& f) {
        success(f.invoke("install.sh", {"small"}, {{"TRANSCRIBE_HOME", ""}})); const auto data = f.home / ".local/share/transcribe";
        CHECK(read(data / "default-model") == "small\n"); CHECK(fs::is_regular_file(data / "whisper.cpp/build/bin/whisper-cli"));
        CHECK(access(f.command.c_str(), X_OK) == 0); CHECK(!fs::exists(f.root));
    });
    add("missing_tool_creates_no_directories", [](Fixture& f) {
        fs::remove(f.bin / "curl"); const auto result = f.invoke(); CHECK(result.code != 0); CHECK(contains(result.err, "curl")); CHECK(!fs::exists(f.home));
    });
    add("wrong_revision_preserves_previous_installation", [](Fixture& f) {
        f.previous(); CHECK(f.invoke("install.sh", {"small"}, {{"ENGINE_REVISION", std::string(40, '0')}}).code != 0);
        CHECK(read(f.command) == "old command"); CHECK(read(f.default_model) == "medium\n"); CHECK(f.calls("cmake").empty() && f.calls("curl").empty());
    });
    add("dirty_sources_are_rejected", [](Fixture& f) {
        f.previous(); CHECK(f.invoke("install.sh", {"small"}, {{"DIRTY_ENGINE", " M example.cpp\n"}}).code != 0); CHECK(f.calls("cmake").empty());
    });
    add("clone_failure_does_not_publish_sources", [](Fixture& f) {
        CHECK(f.invoke("install.sh", {"small"}, {{"FAIL_CLONE", "1"}}).code == 41); CHECK(!fs::exists(f.root / "whisper.cpp"));
        for (const auto& file : fs::directory_iterator(f.root)) if (file.path().filename().string().starts_with(".install.")) CHECK(file.path().filename() == ".install.lock");
    });
    add("wrong_cloned_revision_does_not_publish_sources", [](Fixture& f) {
        CHECK(f.invoke("install.sh", {"small"}, {{"ENGINE_REVISION", std::string(40, '0')}}).code != 0); CHECK(!fs::exists(f.root / "whisper.cpp"));
    });
    add("build_failure_preserves_previous_installation", [](Fixture& f) {
        f.previous(); CHECK(f.invoke("install.sh", {"small"}, {{"FAIL_BUILD", "1"}}).code == 42); f.preserved(); CHECK(f.calls("curl").empty());
    });
    add("network_failure_preserves_previous_installation", [](Fixture& f) {
        f.previous(); CHECK(f.invoke("install.sh", {"small"}, {{"FAIL_NETWORK", "1"}}).code == 28); f.preserved(); CHECK(fs::exists(f.partial()) && !fs::exists(f.model()));
    });
    add("vad_failure_preserves_previous_installation", [](Fixture& f) {
        f.previous(); CHECK(f.invoke("install.sh", {"small"}, {{"FAIL_VAD", "1"}}).code == 28); f.preserved(); CHECK(read(f.model()) == payload);
    });
    add("bad_download_hash_removes_only_partial_file", [](Fixture& f) {
        f.previous(); const auto result = f.invoke("install.sh", {"small"}, {{"BAD_HASH", "1"}}); CHECK(result.code != 0 && contains(result.err, "SHA-256"));
        f.preserved(); CHECK(!fs::exists(f.model()) && !fs::exists(f.partial()));
    });
    add("corrupt_existing_model_is_preserved", [](Fixture& f) {
        write(f.model(), "existing corrupt model"); const auto result = f.invoke("scripts/download-model.sh");
        CHECK(result.code != 0 && contains(result.err, "SHA-256")); CHECK(read(f.model()) == "existing corrupt model"); CHECK(f.calls("curl").empty());
    });
    add("network_failure_then_resume", [](Fixture& f) {
        CHECK(f.invoke("scripts/download-model.sh", {"small"}, {{"FAIL_NETWORK", "1"}}).code == 28); CHECK(!fs::exists(f.model()));
        success(f.invoke("scripts/download-model.sh")); CHECK(read(f.model()) == payload); CHECK(!fs::exists(f.partial()));
    });
    add("complete_partial_is_promoted_without_network", [](Fixture& f) {
        write(f.partial(), payload); success(f.invoke("scripts/download-model.sh")); CHECK(read(f.model()) == payload); CHECK(!fs::exists(f.partial())); CHECK(f.calls("curl").empty());
    });
    add("existing_corrupt_model_preserves_previous_installation", [](Fixture& f) {
        f.previous(); write(f.model(), "keep corrupt existing model"); CHECK(f.invoke().code != 0); f.preserved();
        CHECK(read(f.model()) == "keep corrupt existing model"); CHECK(f.calls("curl").empty());
    });
    add("download_selection_and_tools_validated_before_mkdir", [](Fixture& f) {
        for (const auto& args : std::vector<std::vector<std::string>>{{"bad"}, {"small", "extra"}}) { CHECK(f.invoke("scripts/download-model.sh", args).code == 2); CHECK(!fs::exists(f.home)); }
        fs::remove(f.bin / "sha256sum"); CHECK(f.invoke("scripts/download-model.sh").code != 0); CHECK(!fs::exists(f.home));
    });
    add("partial_symlink_does_not_modify_external_file", [](Fixture& f) {
        const auto external = f.directory / "keep"; write(external, "original"); test::link(external, f.partial());
        CHECK(f.invoke("scripts/download-model.sh").code != 0); CHECK(read(external) == "original"); CHECK(f.calls("curl").empty());
    });
    add("lock_rejects_concurrent_download", [](Fixture& f) {
        fs::create_directories(f.root); const int fd = open((f.root / ".install.lock").c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600); CHECK(fd >= 0);
        CHECK(flock(fd, LOCK_EX | LOCK_NB) == 0);
        const auto result = f.invoke("scripts/download-model.sh"); flock(fd, LOCK_UN); close(fd);
        CHECK(result.code != 0); CHECK(f.calls("curl").empty());
    });
    return suite.run();
}
