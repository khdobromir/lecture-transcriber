#pragma once
// Integration patch for whisper.cpp v1.9.4. UTF-8 at the argument boundary,
// native UTF-16 paths at Windows filesystem APIs; no ANSI-codepage conversion.
#include <filesystem>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
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
inline void isolate_dll_search() {
    // Covers later DLL loads. Backend dependencies additionally use explicit
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 flags.
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)
            || !SetDllDirectoryW(L""))
        throw std::runtime_error("Cannot isolate Windows DLL search");
    if (_wputenv_s(L"GGML_BACKEND_PATH", L"") != 0)
        throw std::runtime_error("Cannot clear GGML_BACKEND_PATH");
}
inline std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Invalid UTF-16 argument");
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back(); return result;
}
#endif
}
