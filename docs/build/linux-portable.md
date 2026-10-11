# Linux portable package

[User installation](../user/quick-start.md) · [Source build](from-source.md)
· [Release procedure](releasing.md)

The packager builds one self-contained AppImage: Qt/QML/plugins, native GUI/CLI,
CPU Whisper, FFmpeg/ffprobe, standalone yt-dlp (including its embedded Python),
fonts and CA certificates. Models remain separate user data. The target is
**Linux x86_64, glibc 2.39+**: Ubuntu 24.04+, Debian 13 and Arch/Omarchy,
Wayland/X11. It still needs a normal desktop graphics stack/drivers; musl/Alpine,
ARM and older glibc are outside this package target.

Whisper is built with `GGML_NATIVE=OFF`, without CUDA/Vulkan/BLAS. x64, AVX and
AVX2 CPU modules dispatch at runtime; baseline does not require AVX2. The GUI
uses Qt Quick software rendering by default. Explicit `QT_QUICK_BACKEND=rhi`
is an optional driver-specific UI check, not ASR GPU acceleration.

## Build

With Docker and Compose installed on the build host, from the repository root:

```sh
bash scripts/package-linux.sh --container
```

The container uses pinned Ubuntu 24.04 image digest and Qt 6.8.3. Source is mounted
read-only; build dependencies are installed inside it. Outputs belong to the
invoking user. Network is required for initial verified downloads; ordinary
packaging does not download models.

- `dist/Transcribe-<version>-linux-x86_64-*.AppImage`: verified application.
- Adjacent `.json`: inventory, provenance and SHA-256 of the AppImage and build-input archive. No standalone checksum files are generated.
- `.build-inputs.tar.gz` and checksum: retained upstream source/tool inputs.
- `.cache/linux-package-container/`: downloads, staging and logs.

A candidate command, also used by CI, is:

```sh
bash scripts/package-linux.sh --container --release --real-smoke --result-file /output/candidate.json
python3 tests/check_linux_candidate.py dist/candidate.json
```

Release mode rejects dirty source and skipped tests. `--real-smoke` downloads
pinned public Russian speech and small model, then checks actual GUI → bundled
CLI → bundled Whisper, preview and exports. `candidate.json` identifies one exact
filename/hash/run: the offline Ubuntu/Debian checks use that same artifact,
without rebuilding or choosing another glob match. Real mode reuses the same
model/audio in the offline containers. Evidence is in `dist/clean-evidence`.

For an already prepared Ubuntu 24.04 build environment:

```sh
bash scripts/package-linux.sh --qt-root /path/to/Qt/6.8.3/gcc_64 --jobs 2
```

Required tools include C++23, CMake 3.24+, Python 3.12+, Git, binutils, patchelf,
desktop-file-utils, FFmpeg, fonts-dejavu-core, OpenGL headers and Qt 6.8.3.
[Dockerfile](../../packaging/linux/Dockerfile) is the complete build dependency
specification. `--work-directory`/`--destination` select output locations.
Diagnostic `--skip-tests` is recorded; startup/package smoke still runs.
Explicit `--build-directory` and `--whisper-build-directory` permit developer
reuse; CMake/tests run again and backend sources are checked against pinned input.
Defaults create fresh build directories. Previous output files are never replaced.

## Install and runtime contract

User launch, no-FUSE fallback, model import and data locations are in the
[Quick Start](../user/quick-start.md) and [user guide](../user/guide.md).
`install.sh` is unnecessary for AppImages. Replace the complete file to upgrade;
models/settings/history/results live outside it. An incomplete bundled tool set
fails closed, without system PATH/old-engine fallback. The installed desktop
PNG and embedded window icon derive from [the same supplied image](../../packaging/ICONS.md).

Model HTTPS uses the bundled public CA store and system certificates; TLS
verification stays enabled. A fully offline first run requires prior model/VAD
import. Extract-and-run needs temporary disk space and does not install packages;
automatic fallback on a FUSE error is not implemented.

## Verification and source materials

Manifest identity includes Git source SHA, dirty flag, source fingerprint,
versions, required glibc and file/symlink hashes/permissions. Source changes during
build are rejected. Smoke checks the final AppImage in a fresh Unicode path with
SDK overrides cleared, not just the staging directory.

Clean Ubuntu/Debian payload tests have no installed Qt/Python/FFmpeg/yt-dlp and
no network. Native desktop dialogs/portal/clipboard need separate manual checks.
A successful GUI build alone does not establish AppImage portability.

[dependencies.json](../../packaging/linux/dependencies.json) pins bytes. Moving
`continuous` URLs do not authorize changing bytes: a mismatch fails until a
reviewed update changes binary/source mappings and the package is revalidated.
Read-only AppImage tools do not self-update; ship a new complete build.

Before public binary distribution, verify actual payload notices and source/build
materials with [THIRD-PARTY](../../packaging/linux/THIRD-PARTY.md),
[source materials](dependency-source-materials.md) and the [release procedure](releasing.md).
Build inputs alone are not the complete source deliverable. Packaging does not
publish a GitHub Release; publication is a separate authorized maintainer action.
