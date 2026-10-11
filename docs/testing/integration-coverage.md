# Integration coverage

[Testing](README.md) · [CI source](../../.github/workflows/ci.yml)

`gui_cli_integration` runs Backend with the actual built `transcribe`. Only
FFmpeg, Whisper and yt-dlp are replaced by native deterministic tool fixtures.
The WAV and Unicode/space/emoji paths are controlled. Workers announce readiness
and PID after publishing partial text; barriers synchronize cancellation/races
instead of relying only on elapsed sleeps.

| Scenario | Linux | Windows |
|---|---|---|
| GUI → CLI → preview → completed/exports | GUI CI | GUI package CI |
| Multiple parts/workers, ordered TXT and subtitle offsets | GUI/pipeline CI | GUI package CI |
| URL temporary/final directory and cache reuse | Mock downloader | Mock downloader |
| Cancel after partial, descendants gone, retry | Native process fixtures | Native Job Object fixtures |
| Tool error 17, old saved result intact, retry | GUI CI | GUI package CI |
| History restart and independent preview | GUI CI | GUI package CI |
| Actual FFmpeg 1/2/17/256 splits and sample bounds | System FFmpeg | Pinned package FFmpeg |
| CLI/audio/features unit cases | Common cases plus POSIX permissions/symlinks | Common portable cases |
| Installer/signals/supervisor owner SIGKILL | Independent POSIX suites | Platform/Job Object cases instead |
| Protocol/model Range/recovery/state boundaries | Shared vectors and local HTTP | Shared vectors and local HTTP |

Equal suite counts are not required. Real ASR, deployed packages, live services,
optional cookies and native UI acceptance are distinct layers, not consequences
of mock integration passing.

CI builds with warnings-as-errors: GCC/Clang on Linux and native MSVC on Windows.
GUI includes Qt 6.8.3 on both platforms and Qt 6.11.2 on Linux. Static analysis
selects only owned src/tests/gui TUs. clang-tidy 18 receives an analyzer-only Qt
include adjustment for LLVM #62985 (overloaded delete double-counting in system
headers); normal compilation/other SDKs and the NewDelete checker remain intact.
Cppcheck uses actual SDK MOC revision/helper definitions and the Qt library model.
Registration-specific suppressions are local and explained, not global GUI exclusions.

Linux sanitizer CI instruments application/tests with ASan/UBSan and enables
LSan; the Qt SDK is not rebuilt. A sandbox ptrace restriction must be recorded
separately from a sanitizer pass. CI retains diagnostics/configuration on failure.
Final candidate checks must execute on their exact source and actual package;
[older baseline results](../releases/v0.4.0/validation.md) are not new-branch evidence.
