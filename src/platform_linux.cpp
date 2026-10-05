#include "platform.hpp"
#include "process.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
namespace transcribe {
std::string environment_utf8(std::string_view name) {
    const char* value = std::getenv(std::string(name).c_str());
    return value ? value : "";
}
fs::path user_home() {
    const char* value = std::getenv("HOME");
    if (!value || !*value) throw std::runtime_error("Не задан HOME; укажи каталог явно");
    return fs::absolute(value);
}
fs::path executable_directory() { return fs::read_symlink("/proc/self/exe").parent_path(); }
bool executable_file(const fs::path& path) { return fs::is_regular_file(path) && access(path.c_str(), X_OK) == 0; }
int logical_cpus() {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    return sched_getaffinity(0, sizeof(mask), &mask) == 0 ? CPU_COUNT(&mask) : static_cast<int>(std::thread::hardware_concurrency());
}
bool terminal_output() { return isatty(STDOUT_FILENO) != 0; }
int terminal_columns() {
    winsize size{};
    return ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col ? size.ws_col : 80;
}
bool create_private_directory(const fs::path& path) {
    if (mkdir(path.c_str(), 0700) == 0) return true;
    if (errno == EEXIST) return false;
    throw std::runtime_error("Не удалось создать каталог: " + path_utf8(path));
}
void require_private_directory(const fs::path& path) {
    struct stat info{};
    if (lstat(path.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0077U))
        throw std::runtime_error("Каталог должен принадлежать пользователю, иметь права 0700 и не быть ссылкой: " + path_utf8(path));
}
bool indirect_path(const fs::path& path) { return fs::is_symlink(fs::symlink_status(path)); }
void create_private_file(const fs::path& path) {
    const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::runtime_error("Не удалось создать файл: " + path_utf8(path));
    close(fd);
}
void replace_file(const fs::path& source, const fs::path& destination, std::function<void()> cancellation) {
    if (cancellation) cancellation();
    fs::rename(source, destination);
}
struct SharedReader::Native { int fd = -1; std::uint64_t bytes = 0; ~Native() { if (fd >= 0) close(fd); } };
SharedReader::SharedReader(const fs::path& path) : native_(std::make_unique<Native>()) {
    native_->fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat info{};
    if (native_->fd < 0 || fstat(native_->fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0)
        throw std::runtime_error("Не удалось открыть файл для чтения: " + path_utf8(path));
    native_->bytes = static_cast<std::uint64_t>(info.st_size);
}
SharedReader::~SharedReader() = default;
std::uint64_t SharedReader::size() const { return native_->bytes; }
std::string SharedReader::read(std::uint64_t offset, std::size_t limit) {
    if (offset >= size()) return {};
    std::string result(static_cast<std::size_t>(std::min<std::uint64_t>(size() - offset, limit)), '\0');
    std::size_t done = 0;
    while (done < result.size()) {
        const auto count = pread(native_->fd, result.data() + done, result.size() - done, static_cast<off_t>(offset + done));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("Ошибка чтения файла");
        if (count == 0) break;
        done += static_cast<std::size_t>(count);
    }
    result.resize(done); return result;
}
bool rename_directory(const fs::path& source, const fs::path& destination) {
    if (renameat2(AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) == 0) return true;
    if (errno == EEXIST) return false;
    throw std::runtime_error("Не удалось переименовать каталог: " + path_utf8(destination));
}
struct FileLock::Native { int fd = -1; ~Native() { if (fd >= 0) close(fd); } };
FileLock::FileLock(const fs::path& path, std::function<void()> cancellation) : native_(std::make_unique<Native>()) {
    const auto check = [&] { if (cancellation) cancellation(); else check_cancelled(); };
    native_->fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info{};
    if (native_->fd < 0 || fstat(native_->fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1)
        throw std::runtime_error("Некорректный файл блокировки: " + path_utf8(path));
    while (flock(native_->fd, LOCK_EX | LOCK_NB)) {
        if (errno != EWOULDBLOCK && errno != EINTR) throw std::runtime_error("Не удалось заблокировать кэш");
        check();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check();
}
FileLock::~FileLock() = default;
bool read_control(std::string& pending) {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    if (poll(&descriptor, 1, 0) < 0) {
        if (errno == EINTR) return true;
        throw std::runtime_error("Ошибка управляющего канала");
    }
    if (!(static_cast<unsigned>(descriptor.revents) & static_cast<unsigned>(POLLIN | POLLHUP | POLLERR))) return true;
    char bytes[4096];
    const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
    if (count > 0) pending.append(bytes, static_cast<size_t>(count));
    else if (count == 0) return false;
    else if (errno != EINTR) throw std::runtime_error("Ошибка чтения управляющего канала");
    return true;
}
}
