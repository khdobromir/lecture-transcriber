#pragma once
// Integration patch for whisper.cpp v1.9.4. UTF-8 at the argument boundary,
// native UTF-16 paths at Windows filesystem APIs; no ANSI-codepage conversion.
#include <filesystem>
#include <string>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif
namespace transcribe_windows {
inline std::filesystem::path path(const std::string& value) { return std::filesystem::u8path(value); }
#ifdef _WIN32
inline std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid UTF-16 argument");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back(); return result;
}
#endif
}
