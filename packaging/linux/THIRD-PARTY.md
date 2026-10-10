# Linux AppImage components

Transcribe is MIT licensed. The AppImage contains separately executed tools and
dynamically linked libraries with their own licenses. Notices are under
`usr/share/transcribe/licenses`; exact versions and SHA-256 pins are recorded in
`dependencies.json` and the complete payload inventory in `package-manifest.json`.
`usr/share/transcribe/linux-library-provenance.json` records copied system
libraries, their binary/source package versions and copyright hashes. The
copyright files and common license texts are retained under `licenses/Linux-system`.
SDK third-party libraries and any unresolved mappings are listed explicitly;
this inventory alone is not a complete corresponding-source deliverable.
`linux-source-provenance.json` maps these system libraries to exact Ubuntu source
package versions and SHA-256-verified archives in the adjacent build-inputs
deliverable. The builder needs matching authenticated `deb-src` indexes and
downloads source packages without unpacking or executing them. An unavailable
exact version or checksum mismatch stops packaging.
The same provenance includes the DejaVu font and generated CA bundle, their
package/source versions, payload hashes and the package-owned CA generator hash.
`licenses/Qt-sdk-source-notices` retains ICU 73.2 notices from its verified source
archive. The source mapping requires exact original hashes for all three ICU
libraries from Qt 6.8.3; a different SDK payload stops packaging. This establishes
source/notice attribution; the full SDK build configuration is still required.
`licenses/AppImage-runtime-source-notices` retains notices for the pinned type-2
runtime and its musl, mimalloc, zstd, zlib, libfuse and squashfuse sources.
`licenses/appimage-runtime-provenance.json` ties those sources and Alpine recipe
snapshots to the runtime binary SHA-256. The adjacent build-inputs archive
retains all nine source/recipe archives, including the runtime's libfuse patch
and exact Alpine package patches. Updating the runtime without an updated
source mapping stops packaging. This remains a partial corresponding-source
deliverable; toolchain/runtime details and other bundled components still need
their complete build materials.
`licenses/yt-dlp-embedded-notices` retains the standalone tool's original license
texts and distribution metadata together with its binary hash and archive inventory.

| Component | Source | License |
| --- | --- | --- |
| Qt 6.8.3 shared libraries and QML/plugins | https://download.qt.io/archive/qt/6.8/6.8.3/ | LGPLv3; preserve replacement/relinking rights |
| ICU 73.2 from the Qt SDK | https://github.com/unicode-org/icu/releases/tag/release-73-2 | Original ICU LICENSE and included third-party notices |
| whisper.cpp, pinned commit | https://github.com/ggml-org/whisper.cpp/tree/927cfce34f31707e17f2bff35c349632fb9e2c3a | MIT |
| FFmpeg 8.1 LGPL build and linked dependencies | https://github.com/BtbN/FFmpeg-Builds/tree/master | LGPL; actual bundled dependency notices apply |
| yt-dlp Linux standalone, including Python and libraries | https://github.com/yt-dlp/yt-dlp/tree/2026.08.19 | Includes GPLv3+ dependencies; bundled notices apply |
| AppImage runtime | https://github.com/AppImage/type2-runtime | MIT, embedded runtime dependencies have their own notices |
| Public root certificates | Ubuntu ca-certificates package / Mozilla CA store | Bundled package copyright applies |
| nlohmann/json | https://github.com/nlohmann/json/tree/v3.12.0 | MIT |

Packaging is separate from public redistribution. Before publishing an AppImage,
provide the corresponding source and build materials for the **actual** bundled
Qt, FFmpeg, yt-dlp and their dependencies as their licenses require, and include
notices for the copied system libraries. This repository's source archive alone
does not contain that material. The LGPL libraries can be replaced in an extracted
AppDir. The current source-only release workflow does not publish AppImages.

Model weights are not bundled. They are downloaded or imported by the GUI, checked
against the pinned model manifest, and stored outside the read-only AppImage.
