# Windows build and ZIP packaging

[Source build](from-source.md) · [User installation](../user/quick-start.md)
· [Manual acceptance](../testing/manual.md)

The distribution target is Windows 11 x64. CI executes MSVC/Qt on Windows Server
2022 runners; that does not replace a clean Windows 11 desktop check. Local
cross-compilation proves compilation only, not native process/DLL behavior.

Install Visual Studio 2022 C++ workload/Windows SDK, CMake 3.24+, Git, Python
3.11+ and official shared Qt 6.8.3 MSVC x64. In PowerShell from the repository:

```powershell
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64
# A clean release candidate with additional public-fixture real ASR:
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64 -Release -RealSmoke
```

The packager creates its own work tree, builds/tests Transcribe, deploys Qt/QML,
builds pinned whisper.cpp, verifies downloaded FFmpeg/yt-dlp hashes, and validates
the **final ZIP** after extracting it into a fresh Unicode path. It does not
install an application globally or publish a Release. `-WorkDirectory` and
`-Destination` select workspace/output. Diagnostic-only `-SkipTests` is recorded
in the manifest; release mode rejects it and a dirty source tree.

`dist` contains a uniquely named version/source/run ZIP, validation JSON with
ZIP/build-input SHA-256 hashes, and retained `.build-inputs.tar.gz`. No standalone
checksum files are generated. Existing artifacts are not
overwritten. Identity is captured before configure and rechecked before packing
and after smoke. A final release needs both the complete application source and
the verified input/source-material set, not build inputs alone.

## Runtime and native integration

`bin/transcribe-gui.exe` and `bin/transcribe.exe` sit beside Qt dependencies;
`bin/tools` has whisper-cli/CPU DLLs, FFmpeg, ffprobe, standalone yt-dlp and the
bundle marker. Models remain external user data. The supplied icon is embedded
in CLI and GUI PE resources; [icon sources](../../packaging/ICONS.md) describe it.
Manifest flags enable PerMonitorV2/long paths/asInvoker; Transcribe still reserves
external-tool path space (240 UTF-16 input, 169 output-parent units).

Whisper uses `GGML_NATIVE=OFF` and runtime CPU dispatch, without CUDA/Vulkan.
The [pinned Unicode/loader patch](../../scripts/patch-whisper-windows.py) checks
original file hashes before editing engine sources: wide argv/audio APIs,
UTF-8 filesystem exports and non-BMP model/VAD paths. It does not patch an
already installed Linux engine.

Backend variants load only beside whisper-cli; `GGML_BACKEND_PATH` is ignored.
Dependency lookup is restricted to the DLL's directory and System32. Bundled
tools receive cleaned GGML/PATH environment; a missing marked bundle fails closed.
MSVC `/DEPENDENTLOADFLAG:0xA00` restricts import-time DLL lookup before `wmain`.
Native smoke checks the PE flag and a benign foreign-DLL positive/negative
marker fixture. Do not weaken these checks to simplify packaging.

## Validation boundaries

The final ZIP smoke verifies manifest/file hashes, CRT placement, PE flags,
Unicode unpack and startup with minimal PATH and cleared SDK overrides.
`-RealSmoke` adds QML/Backend → extracted CLI → bundled Whisper with pinned public
Russian speech/small model and bounded exports. That harness itself uses the Qt
SDK; the independent deployed startup and clean desktop check are distinct.
Tests preserve diagnostics on failure; private recordings/models are not public
artifacts. Read [candidate validation](../testing/candidate-validation.md),
[notices](../../packaging/windows/THIRD-PARTY.md) and
[release gates](releasing.md) before distribution.
