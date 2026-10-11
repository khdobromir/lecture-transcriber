#include "platform.hpp"
#include "process.hpp"
#include "windows.hpp"
#include <aclapi.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace transcribe {
namespace {
struct LocalDelete { void operator()(void* memory) const { if (memory) LocalFree(memory); } };
using LocalMemory = std::unique_ptr<void, LocalDelete>;
std::vector<unsigned char> user_token() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) windows_error("OpenProcessToken");
    WinHandle token(raw);
    DWORD bytes = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> result(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, result.data(), bytes, &bytes)) windows_error("GetTokenInformation");
    return result;
}
struct PrivateSecurity {
    LocalMemory descriptor;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    PrivateSecurity() {
        const auto token = user_token();
        const auto* user = reinterpret_cast<const TOKEN_USER*>(token.data());
        LPWSTR raw_sid = nullptr;
        if (!ConvertSidToStringSidW(user->User.Sid, &raw_sid)) windows_error("ConvertSidToStringSid");
        LocalMemory sid(raw_sid);
        const std::wstring sddl = L"O:" + std::wstring(raw_sid) + L"D:P(A;OICI;FA;;;" + std::wstring(raw_sid) + L")";
        PSECURITY_DESCRIPTOR raw = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &raw, nullptr)) windows_error("SecurityDescriptor");
        descriptor.reset(raw); attributes.lpSecurityDescriptor = raw;
    }
};
void require_private_handle(HANDLE handle, bool directory) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) windows_error("GetFileInformationByHandle");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        static_cast<bool>(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory || (!directory && info.nNumberOfLinks != 1))
        throw std::runtime_error("Некорректный приватный путь");
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto code = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &dacl, nullptr, &descriptor);
    LocalMemory memory(descriptor);
    const auto token = user_token();
    const auto* user = reinterpret_cast<const TOKEN_USER*>(token.data());
    if (code != ERROR_SUCCESS || !owner || !dacl || !EqualSid(owner, user->User.Sid))
        throw std::runtime_error("Путь должен принадлежать текущему пользователю и иметь приватный ACL");
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        void* raw = nullptr;
        if (!GetAce(dacl, i, &raw)) windows_error("GetAce");
        const auto* header = static_cast<const ACE_HEADER*>(raw);
        if (header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) throw std::runtime_error("Неподдерживаемый ACL приватного каталога");
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
        if (!EqualSid(const_cast<DWORD*>(&ace->SidStart), user->User.Sid))
            throw std::runtime_error("Приватный ACL предоставляет доступ другому пользователю");
    }
}
}
fs::path windows_environment_path(const wchar_t* name) {
    const DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
    if (!count) return {};
    std::wstring value(count, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), count);
    if (!written || written >= count) windows_error("GetEnvironmentVariable");
    value.resize(written); return fs::path(value);
}
std::string environment_utf8(std::string_view name) {
    return narrow_utf8(windows_environment_path(wide_utf8(name).c_str()).native());
}
fs::path user_home() {
    auto home = windows_environment_path(L"USERPROFILE");
    if (home.empty()) throw std::runtime_error("Не задан USERPROFILE");
    return fs::absolute(home);
}
fs::path executable_directory() {
    std::wstring name(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (!count || count >= name.size()) windows_error("GetModuleFileName");
    name.resize(count); return fs::path(name).parent_path();
}
bool executable_file(const fs::path& path) { return fs::is_regular_file(path); }
int logical_cpus() {
    WORD group_count = 64; std::array<USHORT, 64> groups{};
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (GetProcessGroupAffinity(GetCurrentProcess(), &group_count, groups.data()) && group_count == 1 &&
        GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) && process_mask)
        return std::clamp(std::popcount(process_mask), 1, 256);
    return static_cast<int>(std::clamp(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), DWORD{1}, DWORD{256}));
}
bool terminal_output() { DWORD mode{}; return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0; }
int terminal_columns() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    return GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) ? info.srWindow.Right - info.srWindow.Left + 1 : 80;
}
bool create_private_directory(const fs::path& path) {
    PrivateSecurity security;
    if (CreateDirectoryW(path.c_str(), &security.attributes)) {
        require_private_directory(path); // Refuse filesystems that silently ignore the ACL.
        return true;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) return false;
    windows_error("CreateDirectory"); return false;
}
void require_private_directory(const fs::path& path) {
    WinHandle handle(CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle) windows_error("Open private directory");
    require_private_handle(handle.get(), true);
}
bool indirect_path(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) windows_error("GetFileAttributes");
    return false;
}
void create_private_file(const fs::path& path) {
    PrivateSecurity security;
    WinHandle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) windows_error("Create private file");
}
void replace_file(const fs::path& source, const fs::path& destination, const std::function<void()>& cancellation) {
    const auto target = fs::absolute(destination).native();
    std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + target.size() * sizeof(wchar_t));
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    rename->FileNameLength = static_cast<DWORD>(target.size() * sizeof(wchar_t));
    std::memcpy(rename->FileName, target.data(), rename->FileNameLength);
    WinHandle file(CreateFileW(source.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!file) windows_error("Open file for publication");
    for (unsigned attempt = 0; ; ++attempt) {
        if (cancellation) cancellation(); else check_cancelled();
        if (SetFileInformationByHandle(file.get(), FileRenameInfoEx, rename, static_cast<DWORD>(storage.size()))) return;
        const auto error = GetLastError();
        if ((error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION) || attempt == 40)
            throw std::runtime_error("Не удалось сохранить файл " + path_utf8(destination) +
                "; закройте программу, удерживающую файл, и повторите запуск. Windows error " + std::to_string(error));
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}
struct SharedReader::Native { WinHandle file; std::uint64_t bytes = 0; };
SharedReader::SharedReader(const fs::path& path) : native_(std::make_unique<Native>()) {
    native_->file.reset(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!native_->file || !GetFileInformationByHandle(native_->file.get(), &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Не удалось открыть файл для чтения: " + path_utf8(path));
    native_->bytes = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow;
}
SharedReader::~SharedReader() = default;
std::uint64_t SharedReader::size() const { return native_->bytes; }
std::string SharedReader::read(std::uint64_t offset, std::size_t limit) {
    if (offset >= size()) return {};
    LARGE_INTEGER position{}; position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(native_->file.get(), position, nullptr, FILE_BEGIN)) windows_error("Seek reader");
    std::string result(static_cast<std::size_t>(std::min<std::uint64_t>(size() - offset, limit)), '\0');
    std::size_t done = 0;
    while (done < result.size()) {
        DWORD count = 0;
        if (!ReadFile(native_->file.get(), result.data() + done,
                      static_cast<DWORD>(std::min<std::size_t>(result.size() - done, 1048576)), &count, nullptr)) windows_error("Read file");
        if (!count) break;
        done += count;
    }
    result.resize(done); return result;
}
bool rename_directory(const fs::path& source, const fs::path& destination) {
    if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    const auto error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || (error == ERROR_ACCESS_DENIED && fs::exists(destination))) return false;
    windows_error("Rename directory"); return false;
}
struct FileLock::Native { WinHandle file; OVERLAPPED overlapped{}; };
FileLock::FileLock(const fs::path& path, std::function<void()> cancellation) : native_(std::make_unique<Native>()) {
    const auto check = [&] { if (cancellation) cancellation(); else check_cancelled(); };
    PrivateSecurity security;
    native_->file.reset(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security.attributes, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!native_->file) windows_error("Open cache lock");
    require_private_handle(native_->file.get(), false);
    while (!LockFileEx(native_->file.get(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &native_->overlapped)) {
        if (GetLastError() != ERROR_LOCK_VIOLATION) windows_error("LockFileEx");
        check(); std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check();
}
FileLock::~FileLock() = default;
bool read_control(std::string& pending) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD available = 0;
    if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) return false;
        windows_error("Peek control pipe");
    }
    if (!available) return true;
    std::array<char, 4096> bytes{};
    DWORD count = 0;
    if (!ReadFile(input, bytes.data(), std::min(available, static_cast<DWORD>(bytes.size())), &count, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE) return false;
        windows_error("Read control pipe");
    }
    pending.append(bytes.data(), count); return count != 0;
}
}
