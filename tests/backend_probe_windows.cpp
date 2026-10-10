// Harmless native fixture. Loading it, even only to query its score, writes a
// marker. It is built for tests only and never installed into the package.
#include <windows.h>
#include <array>
extern "C" __declspec(dllexport) int ggml_backend_score() { return 0; }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        std::array<wchar_t, 32768> path{};
        const auto size = GetEnvironmentVariableW(L"TRANSCRIBE_BACKEND_PROBE", path.data(), static_cast<DWORD>(path.size()));
        if (size && size < path.size()) {
            const auto file = CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        }
    }
    return TRUE;
}
