# Testing

[Developer guide](../developer/getting-started.md) · [Integration matrix](integration-coverage.md)
· [Candidate gates](candidate-validation.md) · [Manual checks](manual.md)

Use a build preset from [source instructions](../build/from-source.md). Run
`ctest --preset linux-gui-debug -N` to see the actual suite inventory: platform,
GUI and FFmpeg availability affect it. Models/account credentials are unnecessary
for ordinary CTest. Linux installer tests substitute network/build tools and use
temporary HOME; run them without sudo.

| Layer | What it establishes | What it does not establish |
|---|---|---|
| `unit` | CLI/audio/features, protocol/export boundaries, package helper/static-source contracts | Full engine accuracy, deployed package startup |
| `integration` | Native platform/processes, real FFmpeg where available, mock-Whisper pipeline, machine JSONL, GUI with real CLI | Live service compatibility or real model output |
| `installation`, `preflight` | Isolated installer/upgrade/error behavior, smoke prerequisite handling | Your existing installation/data compatibility without a matching scenario |
| `gui` | Controller/model local-HTTP cases, real QML window/keyboard, DPI 100/150/200% | Native dialogs/portal/clipboard on each desktop |
| Explicit real smoke | Pinned engine/model/input and checked exports across process/frontend boundary | WER or a general speed/accuracy guarantee |
| Final package smoke | Actual AppImage/ZIP hashes, clean environment, bundle/source/notice policy | An untested native desktop or a rebuilt artifact |

```sh
ctest --preset linux-cli-debug -L unit
ctest --preset linux-gui-debug
TEST_CASE_FILTER=reliability_ ctest --preset linux-cli-debug -R '^pipeline$' --repeat until-fail:20
cmake --build out/build/linux-gui-debug --target all_qmllint
bash -n install.sh scripts/*.sh
shellcheck -x install.sh scripts/*.sh
```

`TEST_CASE_FILTER` selects the C++ harness's case-name substring; no match fails
instead of passing silently. QtTest accepts its own function-name arguments.
Keep Linux permission/symlink/signal assertions alongside portable Windows
cases; equal suite counts are not a portability requirement.

## Static analysis and sanitizers

Static CI also checks changed owned C++ ranges with clang-format 18.1.8.
The local command and tool setup are in [CONTRIBUTING](../../CONTRIBUTING.md#formatting).
PR uses its base SHA, push uses the previous SHA, manual dispatch uses the
merge-base with `origin/master`. Vendor/generated trees and untouched old code
are excluded. Formatting does not replace compilation or static analysis.

`dependency_pins` compares the installer, CLI smoke, GUI workflow fetch/checkout
and both platform Whisper manifests offline. Fixture tests reject one changed
declaration and accept a consistent update. This complements existing source/hash
mapping checks; a real dependency upgrade must also update clone tags, archive
hashes, Windows patch baselines and corresponding source mappings as applicable.

Install developer tools explicitly. The scripts do not install them:

```sh
cmake -S . -B out/build/analysis -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release -DTRANSCRIBE_BUILD_GUI=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
CLANG_TIDY=clang-tidy-18 bash scripts/check-static.sh out/build/analysis
```

The CI versions are clang-tidy 18/cppcheck. Analysis selects owned src/tests/gui
translation units, excluding generated/vendor code; fixes are disabled. A narrow
Qt-include adjustment in an analyzer-only compilation database works around
LLVM #62985 without disabling the checker or changing normal compilation.
Qt MOC markers come from the actual SDK. Suppressions need local explanations.

For Linux ASan/UBSan use a separate Clang build with
`-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -O1'`
and `-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'`. Run CTest with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Qt SDK is not instrumented.
Investigate sanitizer diagnostics rather than adding global suppressions. Local
sandbox ptrace/loopback restrictions must be reported, not presented as passes.

## Real and network checks

Linux `smoke_real` uses installed medium/VAD and whisper.cpp `samples/jfk.wav`:

```sh
out/build/linux-cli-debug/smoke_real --binary out/build/linux-cli-debug/transcribe \
  --app-home /path/to/isolated/data --artifacts /absolute/new/evidence
```

The parent of the new evidence directory must exist. The five-second English
sample runs with recognition language `ru`: this tests processes/exports, not
Russian accuracy. The harness does not download or install prerequisites.
`scripts/ci-real-smoke.sh` is the explicit pinned-source/network bootstrap for
this separate check. Installed GUI real smoke uses
[russian-speech.json](../../tests/fixtures/russian-speech.json), the public
30–60 second interval, its license and small-model/input hashes.

Run `python tests/check_ytdlp_plugins.py /absolute/path/to/yt-dlp` explicitly to
check plugin isolation offline: an owned marker must load in the positive control
and stay unloaded after `--no-plugin-dirs`. This is not a live extractor test.
Live URLs, optional cookies, cache refresh and native UI acceptance have separate
results. Never use personal credentials/media in public CI artifacts.

## CI and release evidence

[CI](../../.github/workflows/ci.yml) covers Ubuntu GCC/Clang 18, Debian, Arch,
native MSVC CLI, Windows Qt ZIP, Linux Qt 6.8.3/6.11.2, static analysis and
ASan/UBSan/LSan. Reliability/cache repeats run on Ubuntu GCC. Test logs and
configuration are retained even on failure. Native Windows jobs exercise all four
CLI/GUI Debug/Release development presets before the separate Release ZIP build.
GUI tests deploy the matching Qt configuration beside their executables; adding
the SDK to PATH alone does not satisfy their DLL loading policy.
[Real Whisper smoke](../../.github/workflows/real-smoke.yml) runs explicitly and
on its weekly schedule; [Linux portable](../../.github/workflows/linux-portable.yml)
builds and tests a single final AppImage, including offline Ubuntu/Debian payloads.

PR runs normally test the merge tree. For candidate acceptance dispatch workflows
on the exact final source SHA with real checks enabled and confirm actual steps
ran, rather than inferring success from workflow files or old green runs.
Merge/release/publication require separate maintainer authorization and the
[release procedure](../build/releasing.md).
