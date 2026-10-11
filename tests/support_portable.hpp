#pragma once
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace test {
namespace fs = std::filesystem;
using Env = std::map<std::string, std::string>;
inline void check(bool value, const char* expression, int line) {
    if (!value) throw std::runtime_error(std::string(expression) + " (line " + std::to_string(line) + ")");
}
#define CHECK(expr) ::test::check(static_cast<bool>(expr), #expr, __LINE__)
inline std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
inline void write(const fs::path& path, std::string_view data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
}
inline bool contains(std::string_view text, std::string_view value) { return text.find(value) != std::string_view::npos; }
inline std::string utf8(const fs::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}
inline fs::path suffixed(fs::path path, std::string_view suffix) { path += suffix; return path; }
inline std::string getenv(const char* name) { const char* value = std::getenv(name); return value ? value : ""; }
struct Temp {
    fs::path path;
    Temp() {
#ifdef _WIN32
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = fs::temp_directory_path() /
                ("transcribe-test-" + std::to_string(random()) + '-' + std::to_string(random()));
            if (fs::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("Cannot create independent test directory");
#else
        std::string pattern = (fs::temp_directory_path() / "transcribe-test-XXXXXX").string();
        if (!mkdtemp(pattern.data())) throw std::runtime_error("mkdtemp");
        path = pattern;
#endif
    }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
    Temp(const Temp&) = delete;
    Temp& operator=(const Temp&) = delete;
};
inline void link(const fs::path& target, const fs::path& destination) {
    fs::create_directories(destination.parent_path()); fs::create_symlink(target, destination);
}
inline size_t entries(const fs::path& directory) { return static_cast<size_t>(std::distance(fs::directory_iterator(directory), fs::directory_iterator())); }
inline fs::path single(const fs::path& directory) { CHECK(entries(directory) == 1); return fs::directory_iterator(directory)->path(); }
inline void wav(const fs::path& file, double seconds = 0.5, int rate = 44100, int channels = 2,
                const std::function<bool(double)>& silent = {}) {
    const auto samples = static_cast<uint32_t>(seconds * rate);
    std::ostringstream out(std::ios::binary);
    const auto le = [&](uint32_t value, int bytes) { for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (8U * static_cast<unsigned>(i))) & 255U)); };
    out << "RIFF"; le(36 + samples * static_cast<uint32_t>(channels) * 2, 4); out << "WAVEfmt "; le(16, 4);
    le(1, 2); le(static_cast<uint32_t>(channels), 2); le(static_cast<uint32_t>(rate), 4);
    le(static_cast<uint32_t>(rate * channels * 2), 4); le(static_cast<uint32_t>(channels * 2), 2); le(16, 2);
    out << "data"; le(samples * static_cast<uint32_t>(channels) * 2, 4);
    for (uint32_t i = 0; i < samples; ++i) {
        const double time = static_cast<double>(i) / rate;
        const auto value = static_cast<int16_t>(silent && silent(time) ? 0 : 10000 * std::sin(time * 2 * 3.141592653589793 * 440));
        for (int ch = 0; ch < channels; ++ch) le(static_cast<uint16_t>(value), 2);
    }
    write(file, out.str());
}
struct Call { std::string kind; std::vector<std::string> args; };
inline std::vector<Call> calls(const fs::path& file) {
    if (!fs::exists(file)) return {};
    std::ifstream in(file, std::ios::binary);
    std::vector<Call> result;
    std::string line;
    while (std::getline(in, line)) {
        Call call{line, {}};
        CHECK(static_cast<bool>(std::getline(in, line)));
        const int count = std::stoi(line);
        for (int i = 0; i < count; ++i) {
            CHECK(static_cast<bool>(std::getline(in, line)));
            std::string arg(static_cast<size_t>(std::stoul(line)), '\0');
            CHECK(static_cast<bool>(in.read(arg.data(), static_cast<std::streamsize>(arg.size()))));
            CHECK(in.get() == '\n'); call.args.push_back(std::move(arg));
        }
        result.push_back(std::move(call));
    }
    return result;
}
inline std::string value(const std::vector<std::string>& args, std::string_view flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    CHECK(it != args.end() && it + 1 != args.end());
    return *(it + 1);
}
inline bool has(const std::vector<std::string>& args, std::string_view flag) { return std::find(args.begin(), args.end(), flag) != args.end(); }
struct Suite {
    std::vector<std::pair<std::string, std::function<void()>>> cases;
    void add(std::string name, std::function<void()> run) { cases.emplace_back(std::move(name), std::move(run)); }
    int run() {
        size_t failed = 0, selected = 0;
        const auto filter = test::getenv("TEST_CASE_FILTER");
        for (const auto& [name, run] : cases) {
            if (!filter.empty() && !contains(name, filter)) continue;
            ++selected;
            try { run(); std::cout << "PASS " << name << '\n'; }
            catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
        }
        std::cout << selected - failed << '/' << selected << " cases passed\n";
        return failed || !selected ? 1 : 0;
    }
};
}
