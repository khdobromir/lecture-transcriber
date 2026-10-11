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
