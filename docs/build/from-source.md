# Build from source

[Developer entry](../developer/getting-started.md) · [Linux package](linux-portable.md)
· [Windows package](windows.md)

## Prerequisites

- Linux or Windows, C++23 compiler and CMake 3.20+ for the standalone CLI.
- GUI: CMake 3.24+, Qt 6.8+ Quick, QuickControls2, Network, Concurrent and LinguistTools.
  The SDK must include `qtbase_ru.qm` and `qtdeclarative_ru.qm` translation catalogs;
  they and the compiled application catalog are embedded during the build.
  Qt Test is needed only with `BUILD_TESTING=ON` (the default).
- Tests: Python 3.11+ Interpreter. Linux tests require FFmpeg; the portable split
  test is registered when FFmpeg is found. Runtime ASR/model dependencies are
  separate from compilation of Transcribe itself.
- Linux GUI builds with the official Qt SDK need OpenGL development libraries;
  Ubuntu packages include `libgl-dev libglx-dev libopengl-dev libegl-dev`.
- Windows: Visual Studio 2022 C++ workload/Windows SDK, x64 developer environment.
  Its preset generator needs CMake 3.21+ (the CLI project still accepts 3.20
  with other supported generators). Official GUI packages require CMake 3.24+,
  shared Qt 6.8.3 MSVC x64 and PowerShell.

Tested toolchains are recorded in [CI](../../.github/workflows/ci.yml), rather than
claiming support for every compiler with a C++23 switch. Qt is not required by
the CLI/core. Vendored nlohmann/json is already in the source tree; building
Transcribe does not fetch whisper.cpp automatically.

## Linux development

Run from the repository root. Presets use `out/build/<preset>` and preserve
source/build separation:

```sh
cmake --preset linux-cli-debug
cmake --build --preset linux-cli-debug --parallel 2
ctest --preset linux-cli-debug
./out/build/linux-cli-debug/transcribe --help

cmake --preset linux-gui-release
cmake --build --preset linux-gui-release --parallel 2
ctest --preset linux-gui-release
cmake --build out/build/linux-gui-release --target all_qmllint
./out/build/linux-gui-release/transcribe-gui
```

Also available: `linux-cli-release` and `linux-gui-debug`. Select a separately
installed Qt SDK with `-DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64` at configure.
For Clang use a fresh preset directory and `-DCMAKE_CXX_COMPILER=clang++`;
do not change the compiler inside an already configured directory.

## Windows development

Use the x64 developer environment with CMake, Python and the Qt SDK available:

```powershell
cmake --preset windows-gui-release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build --preset windows-gui-release --parallel 4
ctest --preset windows-gui-release
.\out\build\windows-gui-release\Release\transcribe-gui.exe
```

The `windows-cli-debug`, `windows-cli-release` and `windows-gui-debug` presets
use the same Visual Studio 2022 x64 generator with explicit build/test
configurations. Executables and native test helper DLLs remain at the build
root's `Debug`/`Release` directories, including after test CMake extraction.
Deploying Qt and media tools is a separate [ZIP packaging step](windows.md).

## Manual configure and install

Equivalent commands remain supported; presets do not replace CMake options:

```sh
cmake -S . -B out/build/custom -DCMAKE_BUILD_TYPE=Release \
  -DTRANSCRIBE_BUILD_GUI=ON -DTRANSCRIBE_WARNINGS_AS_ERRORS=ON
cmake --build out/build/custom --parallel 2
cmake --install out/build/custom --prefix /tmp/transcribe-install
```

Use `-DBUILD_TESTING=OFF` for application-only builds (no Python/Test requirement).
Multi-config generators use `--config Release` for build/install and `-C Release`
for CTest. In-source configuration fails without deleting source files.
Linux installation includes desktop entry and PNG icon; dynamically linked Qt
remains a runtime dependency. An SDK install may need its `LD_LIBRARY_PATH`,
`QT_PLUGIN_PATH` and `QML_IMPORT_PATH` to run; use an AppImage for a portable
user artifact. Windows Qt deployment is handled by its CMake install script.

## Linux CLI source installer

Install a C++23 GCC toolchain, CMake, Git, curl, FFmpeg, compatible yt-dlp,
sha256sum, flock, make and standard filesystem utilities using your distribution.
The current installer [prerequisite list](../../install.sh) is authoritative.
Then, as a regular user:

```sh
bash install.sh small
export PATH="$HOME/.local/bin:$PATH"
transcribe --version
transcribe --model small "lecture.wav"
```

This route builds the CLI and pinned whisper.cpp CPU engine, downloads a selected
model and VAD, and uses system FFmpeg/yt-dlp. It does not install the GUI or update
an AppImage. `TRANSCRIBE_HOME`, `TRANSCRIBE_BIN_DIR` and `TRANSCRIBE_BUILD_JOBS`
support an isolated installation; preserve the data override at runtime. Existing
changed/wrong-revision engine sources are rejected instead of overwritten.
Each binary rename is atomic, but engine/CLI/default-model replacement is not one
transaction. Use a separate data/bin directory when testing upgrades.
