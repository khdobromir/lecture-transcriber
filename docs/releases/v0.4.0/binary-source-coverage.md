# Материалы бинарного выпуска v0.4.0

Комплект исходников состоит из исходного архива Transcribe на release SHA и
отдельного `*.build-inputs.tar.gz` для каждого бинарного пакета. Последний
сохраняет original input bytes и build materials, проверяет SHA-256 до и после
архивирования и содержит manifest со списком файлов. Сам по себе он не содержит
всё дерево приложения; его `corresponding_sources_complete=false` не заменяет
оценку общего комплекта. При публикации оба архива должны быть доступны рядом
с соответствующим ZIP/AppImage, с checksums и точным source/package identity.

| Фактический компонент | Исходники и материалы | Notices / замена |
| --- | --- | --- |
| Transcribe CLI/GUI | Полный release source archive, CMake, упаковщики и инструкции | MIT; приложение можно пересобрать |
| Whisper CPU | Pinned whisper.cpp source, Windows Unicode patch и header, manifest | Original MIT notice; внешний backend заменяется |
| Qt 6.8.3 | Восемь Qt modules; SDK mkspecs, CMake files и generated configuration headers | Original Qt attributions и referenced notices; shared Qt заменяется в ZIP/извлечённом AppDir |
| Linux SDK ICU 73.2 | Original source archive, hashes трёх SDK libraries | Original ICU/third-party notices |
| Linux copied libraries, font, CA data | Exact Ubuntu source packages и package provenance, generator hash | Original package copyrights и common license texts |
| FFmpeg/ffprobe | Pinned FFmpeg source, BtbN recipes/patches/configuration и 95 dependency inputs | Original notices, включая FFTW/Chromaprint; shared libraries заменяются |
| FFmpeg generated/nested inputs | Opus generated C/H; zimg/JPEG XL/libplacebo Git tree proofs; shaderc DEPS; librsvg/rav1e registry archives | Original dependency notices; source hashes привязаны к parent/recipes |
| yt-dlp | Pinned project source, build scripts и Python dependency source archives | Original standalone THIRD_PARTY_LICENSES и distribution metadata |
| Python runtime | CPython 3.10.11 Windows / 3.14.7 Linux sources | Original PSF/third-party notices |
| Linux Readline | Exact CentOS 6.2-11 source RPM с upstream archive, восемью patches и spec; binary SHA/build ID совпали | GPLv3+ notice в standalone notices; источник binding находится в CPython |
| curl_cffi / curl-impersonate | Wrapper/CFFI sources; curl-impersonate 2.0.0 recipes/patches и восемь dependency archives с original CMake hashes | Original notices; полный исходный набор для пересборки wrapper |
| Linux libidn2 внутри curl | libidn2 2.3.7 с включёнными libunistring sources и original build-libidn2.sh | LGPL notices; source-based recompilation/relinking через сохранённый wrapper/CMake recipe |
| AppImage runtime | Pinned runtime/musl/mimalloc/zstd/zlib/libfuse/squashfuse sources и Alpine recipes | Original runtime notices; type-2 extraction не требует FUSE |
| Windows app-local CRT | Provenance Visual Studio redistributable SDK и условия Microsoft | Redistributable DLLs и original redistribution notice |

## Проверка последнего native source mapping

Windows standalone SHA-256 `66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a`
содержит `libcurl-impersonate-ec8ead0e422fe4ae62a99233a63f6007.dll`, SHA-256
`ec8ead0e422fe4ae62a99233a63f6007adc727caba97ff1f87385c00cafe200f`, с
`libcurl/8.21.0-IMPERSONATE`. Linux standalone SHA-256
`58162f9bfdc27458ea47bfcb311cf47028f17d8154a8bf7d689861d46399230a`
содержит `_wrapper.abi3.so`, SHA-256
`c2e1cdeea774da4391f404070051b5df5ab7d2368115a63e5005c2ea56b6ad8e`,
с той же версией и экспортами `idn2_*`. Это соответствует pinned
curl_cffi 0.16.0 → curl-impersonate 2.0.0 recipe. Архив libidn2 содержит
original LGPL texts и используемые included-unistring C sources; отключение NLS
в recipe не требует отдельного gettext runtime для этого компонента.

Восемь dependency archives проверены по `URL_HASH` original CMakeLists.txt.
BoringSSL source ZIP сохранён без преобразования. Source lock фиксирует
platform mappings; Linux libidn2 и Readline не добавляются к Windows inputs.

## Границы проверки

Подготовлены sources, patches, recipes, generated inputs и notices фактического
набора. Побайтовая воспроизводимость сторонних upstream binaries не заявляется.
В частности, rav1e recipe обновляет cc build tool: сохранён original lockfile
baseline, но точная версия обновлённого инструмента не установлена. Это не
причина добавлять SDK или compiler runtime к приложению либо архивировать все
неизменённые general-purpose build tools. Условия соответствующего исходного
кода и relinking описаны в [GPLv3 §1](https://spdx.org/licenses/LGPL-3.0-only.html)
и [LGPLv3 §4](https://spdx.org/licenses/LGPL-3.0-only.html); GCC runtime имеет
[отдельное исключение](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html).

Эта проверка закрывает подготовку материалов F7 для перечисленного состава.
Остаются проверки конечного clean source SHA, распознавание готовыми пакетами,
сопоставление приёмки с окончательными artifact hashes и проверка скачанных
после публикации assets. Эти результаты записываются отдельно и не выводятся
из наличия source archives.
