The application is MIT licensed. The ZIP includes separately executed tools and
dynamically linked libraries with their own licenses. Notices are in `licenses/`.
`licenses/yt-dlp-embedded-notices` retains the standalone executable's original
third-party license texts and distribution metadata, read without executing its
embedded Python code. `notices.json` records the binary hash and archive inventory.

| Component | Pinned input | License / source |
| --- | --- | --- |
| Qt | 6.8.3, shared MSVC x64 SDK | LGPLv3; https://download.qt.io/archive/qt/6.8/6.8.3/ |
| whisper.cpp | 927cfce34f31707e17f2bff35c349632fb9e2c3a | MIT; https://github.com/ggml-org/whisper.cpp/tree/927cfce34f31707e17f2bff35c349632fb9e2c3a |
| nlohmann/json | 3.12.0, vendored header | MIT; https://github.com/nlohmann/json/tree/v3.12.0 |
| FFmpeg | BtbN autobuild-2026-10-01-13-06, shared LGPL 8.1 | LGPL; https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-01-13-06 |
| yt-dlp.exe | 2026.08.19 | The bundled executable includes GPLv3+ dependencies; https://github.com/yt-dlp/yt-dlp/tree/2026.08.19 |
| MSVC runtime | From the build's Visual Studio redistributable SDK | Microsoft redistributable code; https://visualstudio.microsoft.com/license-terms/ |

`dependencies.json` pins downloadable binaries by SHA-256. whisper.cpp is built
from pinned source with the project's Unicode patch. CPU backends are dispatched
at runtime; the package is built with `GGML_NATIVE=OFF`. No model weights are bundled.
The Qt DLLs and FFmpeg shared libraries can be replaced by compatible builds.

Packaging is a local build operation, separate from publication. Before publicly
redistributing a binary ZIP, supply the corresponding source and build material
for the actual Qt, FFmpeg (including its linked dependencies), and yt-dlp bundled
components under their license terms. A link to this MIT project alone is not that
source distribution. Preserve notices and verify the exact package on Windows.
