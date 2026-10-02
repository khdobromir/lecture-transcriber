#include "support.hpp"
#include <sys/file.h>

using namespace test;
namespace {
void append(const fs::path& file, std::string_view text) {
    const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    CHECK(fd >= 0); CHECK(flock(fd, LOCK_EX) == 0);
    const auto n = ::write(fd, text.data(), text.size());
    flock(fd, LOCK_UN); close(fd); CHECK(n == static_cast<ssize_t>(text.size()));
}
void event(std::string_view kind, std::string_view action, int part) {
    const auto file = test::getenv("EVENT_LOG");
    if (file.empty()) return;
    const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    append(file, std::string(kind) + '\t' + std::string(action) + '\t' + std::to_string(part) + '\t' + std::to_string(getpid()) + '\t' + std::to_string(time) + '\n');
}
void hang(std::string_view kind, int part) {
    if (!test::getenv("IGNORE_STOP").empty()) for (int signal : {SIGINT, SIGTERM, SIGHUP}) std::signal(signal, SIG_IGN);
    const pid_t helper = fork();
    CHECK(helper >= 0);
    if (helper == 0) { event(kind, "helper", part); for (;;) pause(); }
    event(kind, "ready", part);
    for (;;) pause();
}
bool hanging(std::string_view kind, int part) {
    return test::getenv("HANG_TOOL") == kind && (test::getenv("HANG_PART").empty() || test::getenv("HANG_PART") == std::to_string(part));
}
}
int main(int argc, char** argv) {
    try {
        const std::string kind = fs::path(argv[0]).filename();
        std::vector<std::string> args(argv + 1, argv + argc);
        const auto log = test::getenv("MOCK_LOG");
        if (!log.empty()) {
            std::ostringstream row;
            row << kind << '\n' << args.size() << '\n';
            for (const auto& arg : args) row << arg.size() << '\n' << arg << '\n';
            append(log, row.str());
        }
        if (has(args, "--help") || has(args, "--version")) return 0;
        if (!test::getenv("CHECK_CLOSED_FDS").empty()) for (const auto& fd : fs::directory_iterator("/proc/self/fd")) {
            std::error_code error;
            const auto target = fs::read_symlink(fd.path(), error);
            if (!error) CHECK(!contains(target.string(), "/Результаты с пробелами/"));
        }
        if (kind == "yt-dlp") {
            if (hanging(kind, 0)) hang(kind, 0);
            if (!test::getenv("FAIL_DOWNLOAD").empty()) return 23;
            CHECK(has(args, "--no-playlist") && has(args, "--ignore-config"));
            CHECK(args.size() >= 2 && args[args.size() - 2] == "--" && args.back().starts_with("https://"));
            CHECK(value(args, "-f") == "bestaudio/best");
            auto destination = value(args, "-o");
            destination.replace(destination.find("%(ext)s"), 7, "wav");
            fs::copy_file(test::getenv("MOCK_INPUT"), destination);
            const auto it = std::find(args.begin(), args.end(), "--print-to-file");
            const auto index = static_cast<size_t>(it - args.begin());
            CHECK(it != args.end());
            CHECK(args.at(index + 1) == "after_move:%(filepath)s");
            write(args.at(index + 2), destination + '\n');
        } else if (kind == "whisper-cli") {
            const fs::path prefix = value(args, "--output-file");
            const int part = std::stoi(prefix.parent_path().filename());
            event(kind, "start", part);
            if (!test::getenv("FAIL_ENGINE").empty()) return 17;
            CHECK(value(args, "--language") == "ru" && has(args, "--no-gpu"));
            CHECK(!has(args, "--no-timestamps"));
            const auto audio = read(value(args, "--file"));
            CHECK(audio.starts_with("RIFF") && contains(audio.substr(0, 100), "WAVEfmt "));
            const auto fmt = audio.find("fmt ") + 8;
            CHECK(static_cast<unsigned char>(audio.at(fmt + 2)) == 1);
            CHECK(static_cast<unsigned char>(audio.at(fmt + 4)) == 0x80 && static_cast<unsigned char>(audio.at(fmt + 5)) == 0x3e);
            CHECK(static_cast<unsigned char>(audio.at(fmt + 14)) == 16);
            const std::string text = test::getenv("EMPTY_SPEECH").empty() ? "Тестовая расшифровка " + std::to_string(part) + ".\n" : "";
            if (!text.empty()) {
                const std::string line = "\n[00:00:00.000 --> 00:00:00.100]  " +
                    (test::getenv("INCOMPLETE_TAIL").empty() ? text : text.substr(0, text.size() - 1));
                if (!test::getenv("SPLIT_UTF8").empty()) {
                    for (char ch : line) { std::cout.put(ch); std::cout.flush(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
                } else std::cout << line << std::flush;
            }
            std::cerr << "whisper_print_progress_callback: progress =  50%\n" << std::flush;
            if (test::getenv("CRASH_PART") == std::to_string(part)) {
                event(kind, "crash_ready", part);
                until([] { return fs::exists(test::getenv("CRASH_RELEASE")); });
                if (test::getenv("CRASH_MODE") == "segv") {
                    const rlimit limit{0, 0}; CHECK(setrlimit(RLIMIT_CORE, &limit) == 0);
                    // Deliberate fixture signal; sanitizers must not turn it into exit 1.
                    std::signal(SIGSEGV, SIG_DFL);
                    CHECK(kill(getpid(), SIGSEGV) == 0);
                    _exit(99);
                }
                return 17;
            }
            if (!test::getenv("ORPHAN_HELPER").empty()) {
                const auto helper = fork(); CHECK(helper >= 0);
                if (helper == 0) { event(kind, "helper", part); for (;;) pause(); }
            }
            if (!test::getenv("FLOOD").empty() && (test::getenv("FLOOD_PART").empty() || test::getenv("FLOOD_PART") == std::to_string(part))) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::cerr << std::string(size_t{512} * 1024, 'x') << '\n' << std::flush;
            }
            if (hanging(kind, part)) hang(kind, part);
            int delay = test::getenv("ENGINE_DELAY_MS").empty() ? 20 : std::stoi(test::getenv("ENGINE_DELAY_MS"));
            if (!test::getenv("OUT_OF_ORDER").empty()) delay = part == 1 ? 500 : 20;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            if (test::getenv("FAIL_PART") == std::to_string(part)) return 17;
            if (test::getenv("MISSING_EXPORT") != "txt") write(prefix.string() + ".txt", text);
            if (test::getenv("MISSING_EXPORT") != "srt") write(prefix.string() + ".srt", text.empty() ? "" : "1\n00:00:00,000 --> 00:00:00,100\n" + text + '\n');
            if (test::getenv("MISSING_EXPORT") != "vtt") write(prefix.string() + ".vtt", "WEBVTT\n\n" + (text.empty() ? std::string() : "00:00:00.000 --> 00:00:00.100\n" + text + '\n'));
            if (!test::getenv("MALFORMED_EXPORT").empty()) write(prefix.string() + ".srt", "1\ninvalid times\ntext\n");
            if (!test::getenv("SLOW_CLEANUP").empty())
                for (int i = 0; i < 4000; ++i) fs::create_directory(prefix.parent_path() / ("cleanup-" + std::to_string(i)));
            if (!test::getenv("DENY_CLEANUP").empty()) {
                const auto locked = prefix.parent_path() / "locked";
                write(locked / "keep", "cleanup fixture");
                fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec);
            }
            event(kind, "finish", part);
        } else if (kind == "git") {
            if (args.at(0) == "clone") {
                write(fs::path(args.back()) / "partial-clone", "fixture");
                if (!test::getenv("FAIL_CLONE").empty()) return 41;
            } else if (has(args, "rev-parse")) std::cout << (test::getenv("ENGINE_REVISION").empty() ? "927cfce34f31707e17f2bff35c349632fb9e2c3a" : test::getenv("ENGINE_REVISION")) << '\n';
            else if (has(args, "status")) std::cout << test::getenv("DIRTY_ENGINE");
        } else if (kind == "cmake") {
            if (!test::getenv("FAIL_BUILD").empty()) return 42;
            if (has(args, "--build")) {
                const fs::path binary = fs::path(value(args, "--build")) / (has(args, "--target") ? "bin/whisper-cli" : "transcribe");
                fs::create_directories(binary.parent_path());
                // The staged executable is C++, too; basename controls fixture mode.
                fs::copy_file(fs::read_symlink("/proc/self/exe"), binary);
                fs::permissions(binary, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec);
            }
        } else if (kind == "curl") {
            const fs::path destination = value(args, "--output");
            CHECK(has(args, "--continue-at") && !contains(args.back(), "/resolve/main/"));
            std::string data = "verified fixture model\n";
            if (!test::getenv("FAIL_NETWORK").empty() || (!test::getenv("FAIL_VAD").empty() && contains(args.back(), "silero"))) {
                write(destination, data.substr(0, 7)); return 28;
            }
            if (!test::getenv("BAD_HASH").empty()) data = "corrupted download";
            if (fs::exists(destination)) CHECK(data.starts_with(read(destination)));
            write(destination, data);
        } else if (kind == "ffmpeg" && hanging(kind, 0)) hang(kind, 0);
        else if (kind == "ffmpeg" && !test::getenv("REAL_FFMPEG").empty()) {
            args.insert(args.begin(), test::getenv("REAL_FFMPEG"));
            std::vector<char*> command;
            command.reserve(args.size() + 1);
            for (auto& arg : args) command.push_back(arg.data());
            command.push_back(nullptr); execv(command[0], command.data()); return 127;
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 99; }
}
