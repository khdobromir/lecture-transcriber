# Third-party components

The root [MIT license](LICENSE) covers Transcribe's own code. Speech recognition
is provided by whisper.cpp and pretrained models; Qt supplies the GUI framework.
Transcribe implements orchestration, process lifecycle, validation, exports and
its desktop interface. It does not implement the ASR model or those libraries.

## Distribution contents

| Component | Integration | Version / authoritative inputs | License and retained notices |
|---|---|---|---|
| nlohmann/json | Vendored single header in source; compiled into CLI/core | 3.12.0; [origin and SHA-256](third_party/README.md) | [MIT](third_party/nlohmann/LICENSE.MIT), also in the header |
| Qt Quick / QML / Network / Concurrent | Optional GUI build; shared libraries/plugins in binary packages | Minimum 6.8; packaged SDK 6.8.3, recorded in platform manifests | Qt LGPLv3 and applicable third-party notices; see package notices below |
| whisper.cpp / ggml | Separate CPU executable; built by installer/packagers | Commit `927cfce34f31707e17f2bff35c349632fb9e2c3a` (v1.9.4) | [MIT](https://github.com/ggml-org/whisper.cpp/blob/927cfce34f31707e17f2bff35c349632fb9e2c3a/LICENSE); packaged notices include the actual source license |
| FFmpeg / ffprobe | Separate media executables; system tools for source installs, bundled in AppImage/ZIP | Platform [Linux](packaging/linux/dependencies.json) / [Windows](packaging/windows/dependencies.json) manifests | FFmpeg LGPL/GPL terms depend on build and linked components; preserve actual package notices |
| yt-dlp | Separate downloader; standalone binary in AppImage/ZIP | Platform manifests; packaged 2026.08.19 | Upstream source uses Unlicense; standalone dependencies have additional terms, including GPLv3+; actual embedded notices are retained |
| Whisper GGML models | Downloaded/imported user data, never bundled | Revisions, filenames and SHA-256 in [models.tsv](scripts/models.tsv) | Supplier MIT terms: [Whisper](https://github.com/openai/whisper/blob/main/LICENSE), [model repository](https://huggingface.co/ggerganov/whisper.cpp) |
| Silero VAD GGML | Downloaded/imported user data | [models.tsv](scripts/models.tsv) | [Silero MIT](https://github.com/snakers4/silero-vad/blob/master/LICENSE), [GGML repository](https://huggingface.co/ggml-org/whisper-vad) |
| AppImage runtime, Linux libraries, fonts and CA certificates | Linux package payload | Package manifest and Linux provenance records | Original package/runtime notices retained in AppImage |
| MSVC runtime | Windows package payload | Build's redistributable SDK and package manifest | Microsoft redistributable terms; see Windows notices |

The source archive contains Transcribe, the vendored JSON header, license texts,
packaging manifests and project-specific patches. It does not contain Qt SDKs,
external executables, model weights or private input media. Models and ordinary
user results remain outside both binary packages.

## Binary packages and source materials

- [Linux notices](packaging/linux/THIRD-PARTY.md): AppImage libraries/tools,
  `usr/share/transcribe/licenses`, system-library provenance and SDK/runtime inputs.
- [Windows notices](packaging/windows/THIRD-PARTY.md): ZIP tools/DLLs,
  `licenses/`, Qt/FFmpeg/yt-dlp notices and redistributable runtime.
- [Source input mapping](packaging/source-inputs.json): exact archives, revisions,
  checksums and component relationships used by the packaging collectors.
- [Source materials guide](docs/build/dependency-source-materials.md) and
  [v0.4.0 coverage report](docs/releases/v0.4.0/binary-source-coverage.md): retained inputs,
  attribution and explicit rebuilding/coverage limitations.
- [v0.4.0 release](https://github.com/khdobromir/lecture-transcriber/releases/tag/v0.4.0):
  binary/source artifacts, checksums, manifests, build-inputs and validation records.

Use notices and source materials for the **actual distributed bytes**. The root
MIT license does not replace dependency licenses, and a link to this repository
alone is not the corresponding source for every bundled component. The coverage
reports distinguish retained materials from unresolved reconstruction details;
this inventory does not assert universal license compliance or bit-identical builds.

## Updates

Model downloads use full revisions rather than a moving branch and validate
SHA-256. When updating a model or tool, review its upstream terms and compatibility,
update pins and corresponding source mappings together, and rerun affected tests.
Do not replace only a binary hash while leaving its source/notice mapping stale.

Compilers, CMake, Git, Bash, Python used by tests/packaging, PowerShell and CI tools
are development/build dependencies, not the application's implementation stack.
The standalone yt-dlp package includes its own Python runtime; users of the binary
application do not need to install a separate Python interpreter.
