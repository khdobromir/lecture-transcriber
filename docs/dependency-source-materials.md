# Registry sources в материалах FFmpeg

`packaging/source-inputs.json` связывает каждый registry bundle с исходным
archive parent library, его SHA-256, recipe и FFmpeg binary pins. Bundles
создаются `scripts/package_cargo_sources.py`; это plain canonical TAR, поскольку
сохранённые `.crate` archives уже сжаты. Cargo и upstream source code при
подготовке материалов не выполняются.

В каждом bundle находятся:

- `Cargo.lock` — оригинальный parent lockfile;
- `registry-sources.json` — parent/lock hashes и полный список registry
  package names, versions и checksums;
- `crates/*.crate` — оригинальные source TAR.gz, каждый проверен по Cargo.lock;
- `licenses/<package-version>/…` — оригинальные notices и Cargo.toml.

Для проверки используйте `verify(parent_archive, bundle_archive)` из helper.
Она сравнивает parent lock, package set и SHA-256 каждого retained crate.
Общая контрольная сумма canonical TAR отдельно закреплена в source lock;
изменённый архив не допускается к упаковке.

Для сборки библиотеки распакуйте её исходный parent archive и используйте
сохранённый BtbN recipe из `ffmpeg-build-recipes`. `.crate` содержит весь
опубликованный исходный пакет, а не только notices. Исходный librsvg recipe
вызывает `cargo vendor --versioned-dirs` и записывает его configuration в
`.cargo/config.toml`; pinned Cargo.lock задаёт registry versions/checksums.
Сохранённые archives позволяют восстановить registry cache либо directory
source с `.cargo-checksum.json` для каждого package. Их обычный формат описан
в [Cargo source replacement](https://doc.rust-lang.org/cargo/reference/source-replacement.html).
После восстановления source directories применяются исходные Meson/cross-file
settings и patches из сохранённого recipe snapshot.

Для rav1e сохранён исходный lockfile baseline. Его recipe выполняет
`cargo update cc` перед `cargo cinstall`; точная final resolution build tool
этим baseline не устанавливается. Это ограничение записано в отчёте выпуска;
оно не подменяется утверждением о побайтовой воспроизводимости upstream binary.

Полнота corresponding sources всей раздачи проверяется отдельно: эти bundles
не покрывают сами по себе native dependencies yt-dlp и SDK build settings.

`materials/qt-sdk-build-settings.tar` сохраняет actual SDK mkspecs,
module CMake files и generated feature/config headers. `settings.json`
содержит Qt version и hashes оригинальных файлов; разрешены только SDK-internal
symlinks, их targets записаны отдельно. Version mismatch, missing configuration
и выход symlink за SDK root останавливают упаковку. Эти материалы дополняют
Qt module sources; они не являются обещанием побайтового воспроизведения SDK.

## Readline внутри Linux yt-dlp

В Linux build-inputs сохраняется оригинальный CentOS source RPM
`readline-6.2-11.el7.src.rpm` (SHA-256
`9678eb2f67b1784e5fba3ed0d59cba3ce6f3a3936936dfae956f7aed44b4cbf0`).
Он содержит upstream `readline-6.2.tar.gz`, восемь patch files и `readline.spec`
с настройками сборки. Содержимое можно прочитать/извлечь `bsdtar`; сборка
в RPM build environment следует сохранённому spec. При сборе материалов
RPM hooks, spec и upstream scripts не исполняются.

`libreadline.so.6` внутри закреплённого yt-dlp побайтово совпала с
`lib64/libreadline.so.6.2` оригинального бинарного CentOS RPM: SHA-256
`2c5f7b8d6d8a257f08a05f931d6f2c0806dacb368755083f203149ea45222140`,
GNU build ID `16414a5fb6f903c12c28a12e2d2c3806f78bf097`. Source lock
фиксирует оба RPM URL/hashes и payload evidence. Лицензия GPLv3+ уже
сохранена в оригинальном `yt-dlp-embedded-notices/THIRD_PARTY_LICENSES.txt`.
Windows yt-dlp этой библиотеки не содержит и этот input не получает.

## Curl-impersonate внутри yt-dlp

Оба комплекта сохраняют curl-impersonate 2.0.0
(`ec41b71ce888806bfec56ada7a7258d333eb3d19`), его patches, CMake/CI
recipes и все восемь source inputs из `CMakeLists.txt`: curl 8.21.0,
BoringSSL `156c7b75ae9b8c3b3f847acf264f17594c3859fb`, Brotli 1.2.0,
nghttp2 1.63.0, ngtcp2 1.20.0, nghttp3 1.15.0, zlib 1.3.1, zstd 1.5.7.
Каждый archive hash совпал с original CMake `URL_HASH`; original ZIP
BoringSSL сохраняется без преобразования. Рецепты не исполнялись при сборе.

Для Linux отдельно сохраняется libidn2 2.3.7: `build-libidn2.sh` задаёт
`--disable-shared --enable-static --with-pic --disable-nls
--with-included-libunistring`. Его source archive содержит используемые
libunistring sources и LGPL notices. Linux `_wrapper.abi3.so` экспортирует
`idn2_*`; исходники Python wrapper и CFFI уже находятся в retained
`curl_cffi-0.16.0.tar.gz` и `cffi-2.1.1.tar.gz`. Для пересборки/перелинковки
используйте эти sources, curl-impersonate patches, CMake/Makefile и сохранённые
dependency archives; upstream scripts/build.py показывает порядок linking.
Windows recipe использует Win32 IDN и не требует этого Linux input.

Build-inputs archive сам по себе не является всем source offer: полный
исходный архив Transcribe распространяется отдельно и содержит application
code, упаковщики, тесты и инструкции. Общий комплект для конкретного payload
состоит из обоих архивов и manifest/checksums; поле
`corresponding_sources_complete=false` в retained-input metadata не является
автоматическим утверждением о полноте всего такого комплекта.
