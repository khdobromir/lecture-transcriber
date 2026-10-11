# v0.4.1

- **Windows 11 x64:** `Transcribe-0.4.1-windows-x64.zip`
- **Linux x86_64, glibc 2.39+:** `Transcribe-0.4.1-linux-x86_64.AppImage`

See the [verification record](validation.md) and the release's
`release-validation.json` for exact source and artifact identities.

This maintenance release improves application identity, downloader isolation,
cross-platform regression coverage and documentation. It keeps the independent
CLI, Qt Quick GUI and local CPU-only whisper.cpp pipeline.

## User changes and fixes

- Use the maintainer's application icon in Windows executables, Qt windows,
  the Linux desktop launcher/AppImage and README.
- Disable ambient yt-dlp plugin directories for metadata and media downloads.
  Built-in extractors, URL arguments and cookies options remain available.
- Derive GUI application-version metadata from CMake, matching CLI/package metadata.
- Provide shorter English first-run instructions, English technical guides and
  compact Russian user instructions, with links to existing engineering work.

Portable CLI/audio/features cases now run on native Windows as well as Linux.
Development presets, pinned formatting and offline dependency pin checks improve
maintenance; package smoke verifies the icon and yt-dlp plugin positive/negative
controls against the actual bundled executable.

## Compatibility and limitations

CLI defaults/flags, JSONL protocol v1, result layout, TXT/SRT/VTT and user-data
locations are unchanged. Extract a new complete Windows ZIP into a new directory
or replace the Linux AppImage; preserve external models, settings and results.
Models are not bundled. Source installations need a yt-dlp supporting
`--no-plugin-dirs` (pinned standalone version: 2026.08.19); custom extractor plugins
are outside the supported configuration.

Windows targets Windows 11 x64; Linux targets x86_64/glibc 2.39+ (Ubuntu 24.04+,
Debian 13, Arch/Omarchy). No GPU, ARM, macOS or musl package is provided. Medium
can run slower than real time; partial previews/ETA are not completed exports or
accuracy measurements. Live-service behavior depends on upstream extractors and
access rights. Native desktop and live-service scenarios are reported only when actually tested.

## Installation and integrity

Windows: extract the **whole ZIP**, preserving all directories, then run
`bin/transcribe-gui.exe`. Linux:

```sh
chmod +x Transcribe-0.4.1-linux-x86_64.AppImage
./Transcribe-0.4.1-linux-x86_64.AppImage
# Without FUSE:
./Transcribe-0.4.1-linux-x86_64.AppImage --appimage-extract-and-run
```

Qt, Whisper CPU, FFmpeg/ffprobe and standalone yt-dlp are included. Download or
import a recognition model and Silero VAD in **Модели**, then select your media.
See the [Quick Start](../../user/quick-start.md).

Uploaded-file SHA-256 digests are available directly on GitHub Releases and
through its release-assets API. Separate `.sha256` and `SHA256SUMS` files are no
longer published. Compare the digest with `sha256sum FILE` on Linux or
`Get-FileHash -Algorithm SHA256 -LiteralPath '.\FILE'` in PowerShell. Internal
model, dependency, source-tree and build/package integrity checks remain enabled.

## Sources, licenses and validation

Developers rebuilding or reviewing the packages need the combined deliverable:

- `Transcribe-0.4.1-source.tar.gz`: full Transcribe source, tests, packagers,
  application MIT license, vendored JSON notices and project patches.
- `Transcribe-0.4.1-windows-x64.zip.build-inputs.tar.gz` and
  `Transcribe-0.4.1-linux-x86_64.AppImage.build-inputs.tar.gz`: corresponding
  dependency sources, original notices, recipes, patches and SDK build settings.
- Adjacent `.zip.manifest.json`, `.zip.validation.json`, `.AppImage.json`,
  `linux-ci-artifact.json`, `windows-ci-artifact.json`,
  `source-offer-verification.json` and `release-validation.json`: source mapping,
  exact hashes, automated evidence, testing scope and known limitations.

Original GPL/LGPL and other dependency terms remain applicable; Transcribe's MIT
license does not replace them. Qt stays dynamically linked with replacement/
relinking materials retained. See [dependency materials](../../build/dependency-source-materials.md)
and [third-party inventory](../../../THIRD_PARTY.md). Retained source archives do
not establish byte-identical reproduction of upstream binaries; rav1e's recipe-time
`cargo update cc` remains an explicitly recorded upstream reconstruction limit.

Read [validation](validation.md) and the release's `release-validation.json`
for concrete source/package SHAs, CI runs, platform acceptance and known limits.
Package checks, including manual desktop testing, are optional under the
maintainer policy set on 2026-10-11. Untested scenarios remain explicitly recorded;
existing automatic smoke and internal integrity checks stay enabled.
