# Dependency source materials

[Release procedure](releasing.md) · [Dependency inventory](../../THIRD_PARTY.md)
· [Pinned source mapping](../../packaging/source-inputs.json)

Collection retains untrusted source/recipe data without executing it. Hashes,
parent mappings and build settings are part of review; retaining archives alone
is not a promise of byte-reproducible upstream binaries or legal completeness.
The application source archive is distributed separately from build-inputs.

## Cargo registry sources inside FFmpeg dependencies

`source-inputs.json` maps each registry bundle to a parent library archive hash,
recipe and FFmpeg binary pins. `package_cargo_sources.py` creates plain canonical
TAR because the original `.crate` archives are already compressed. Cargo and
source code are not run while collecting. Each bundle contains:

- Original parent `Cargo.lock`.
- `registry-sources.json`: parent/lock hashes, full registry names/versions/checksums.
- `crates/*.crate`: original source TAR.gz verified against Cargo.lock.
- `licenses/<package-version>/…`: original notices and Cargo.toml.

The helper's `verify(parent_archive, bundle_archive)` checks the parent lock,
package set and every retained crate hash. Canonical TAR hash is pinned separately;
a modified bundle is rejected.

To rebuild, unpack the parent library and use the retained BtbN recipe snapshot.
A `.crate` is the complete published source package, not notices alone. librsvg's
recipe runs `cargo vendor --versioned-dirs` and writes `.cargo/config.toml`;
its pinned Cargo.lock fixes registry versions/checksums. Retained crates can
restore registry cache or directory sources with `.cargo-checksum.json` per
package. See [Cargo source replacement](https://doc.rust-lang.org/cargo/reference/source-replacement.html).
Then apply the original Meson/cross-file settings and retained recipe patches.

rav1e retains its original lock baseline, but its recipe runs `cargo update cc`
before `cargo cinstall`: the baseline does not establish that final resolution.
Record this limit rather than claiming upstream binary reproducibility. These
bundles do not themselves cover yt-dlp native dependencies or Qt SDK settings.

## Qt SDK settings

`materials/qt-sdk-build-settings.tar` retains actual SDK mkspecs, module CMake
files and generated feature/config headers. `settings.json` records Qt version
and original file hashes. Only SDK-internal symlinks are allowed and their targets
are recorded. Version mismatch, missing configuration or an escaping link stops
packaging. This complements Qt module sources, not byte-identical SDK reproduction.

## Readline in Linux yt-dlp

Linux inputs retain original CentOS `readline-6.2-11.el7.src.rpm` with SHA-256
`9678eb2f67b1784e5fba3ed0d59cba3ce6f3a3936936dfae956f7aed44b4cbf0`.
It contains `readline-6.2.tar.gz`, eight patches and `readline.spec`; `bsdtar`
can inspect/extract them, while rebuild follows that spec in an RPM environment.
Collection does not execute RPM hooks/specs/scripts.

The pinned standalone binary's `libreadline.so.6` matched CentOS RPM
`lib64/libreadline.so.6.2`: SHA-256
`2c5f7b8d6d8a257f08a05f931d6f2c0806dacb368755083f203149ea45222140`,
GNU build ID `16414a5fb6f903c12c28a12e2d2c3806f78bf097`. Source lock retains
binary/source RPM URLs/hashes and payload evidence. GPLv3+ terms are preserved in
original `yt-dlp-embedded-notices/THIRD_PARTY_LICENSES.txt`. Windows yt-dlp does
not contain Readline and does not receive this Linux input.

## curl-impersonate in yt-dlp

Both sets retain curl-impersonate 2.0.0 at
`ec41b71ce888806bfec56ada7a7258d333eb3d19`, patches/CMake/CI recipes and all
eight CMake inputs: curl 8.21.0, BoringSSL
`156c7b75ae9b8c3b3f847acf264f17594c3859fb`, Brotli 1.2.0, nghttp2 1.63.0,
ngtcp2 1.20.0, nghttp3 1.15.0, zlib 1.3.1 and zstd 1.5.7. Hashes match original
CMake `URL_HASH`; BoringSSL's original ZIP is retained unmodified. Recipes are
not executed during collection.

Linux additionally retains libidn2 2.3.7. `build-libidn2.sh` specifies
`--disable-shared --enable-static --with-pic --disable-nls --with-included-libunistring`;
the source contains the used libunistring code and LGPL notices. Linux
`_wrapper.abi3.so` exports `idn2_*`; Python wrapper and CFFI sources are in
`curl_cffi-0.16.0.tar.gz` and `cffi-2.1.1.tar.gz`. Use these sources,
curl-impersonate patches, CMake/Makefile and retained dependency archives for
rebuilding/relinking; upstream `scripts/build.py` describes link order. Windows
uses Win32 IDN and needs no Linux libidn2 input.

The complete source deliverable for a payload combines Transcribe's full source
archive (application, packagers, tests and instructions), build-input/source
archives, notices, manifests and checksums. Retained metadata's
`corresponding_sources_complete=false` does not automatically describe the whole
combined deliverable. Match actual inventory and preserve the
[historical investigation](../archive/dependency-source-materials-before-modernization.md).
