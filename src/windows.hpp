#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
namespace transcribe {
inline std::wstring wide_utf8(std::string_view bytes) {
    if (bytes.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Некорректный UTF-8");
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), result.data(), count);
    return result;
}
inline std::string narrow_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Некорректный UTF-16");
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
inline void windows_error(const char* action) { throw std::runtime_error(std::string(action) + ": Windows error " + std::to_string(GetLastError())); }
class WinHandle {
public:
    WinHandle() = default;
    explicit WinHandle(HANDLE handle) : value_(handle) {}
    ~WinHandle() { reset(); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    WinHandle(WinHandle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    WinHandle& operator=(WinHandle&& other) noexcept { if (this != &other) reset(std::exchange(other.value_, INVALID_HANDLE_VALUE)); return *this; }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = INVALID_HANDLE_VALUE) { if (*this) CloseHandle(value_); value_ = value; }
private:
    HANDLE value_ = INVALID_HANDLE_VALUE;
};
// CommandLineToArgvW / CRT quoting rules, including trailing backslashes.
inline std::wstring quote_windows(std::wstring_view argument) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') { ++backslashes; continue; }
        result.append(backslashes * (ch == L'\"' ? 2 : 1), L'\\');
        if (ch == L'\"') result += L'\\';
        result += ch;
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\'); result += L'\"';
    return result;
}
}
#endif
