# v0.4.1 candidate validation

This document fixes the acceptance contract before source freeze; it is not a
claim that a candidate has passed. The delivery's `release-validation.json`
records the actual candidate commit, source tree/fingerprint, artifact hashes,
concrete CI run links, performed checks, omissions and status. Keeping measured
evidence beside immutable packages avoids changing the source commit merely to
record its own SHA. A missing result is unverified, not a pass.

## Baseline and version

Compared with v0.4.0 (`926366a1c4f7ec2d1cc188f8448591809cc3282c`), 0.4.1 contains
maintenance, branding, yt-dlp isolation, cross-platform tests and developer/user
documentation. There are no new CLI flags, application workflows or data formats.
PATCH is appropriate; custom yt-dlp plugin extractors were never a supported
configuration. The CMake project version is the sole application version source.
The approved modernization roadmap is historical planning; actual code/tests and
[implementation evidence](../../development-report.md) establish completed scope.

## Required checks

| Gate | Required evidence |
|---|---|
| Clean source identity | Exact commit/tree/fingerprint, version 0.4.1, no dirty source or private files |
| CI | GCC/Clang/MSVC, Linux/Windows Debug/Release, Qt 6.8.3/6.11.2, CTest, QML lint, static analysis, ASan/UBSan/LSan, application-only build/install |
| Real ASR | CI `real_smoke=true`, Linux portable `real_smoke=true`, Real Whisper workflow on the same commit |
| Windows ZIP | Final extracted ZIP inventory, Unicode/minimal PATH, DLL/backend isolation positive control, model-free regression, icon/plugin isolation, real GUI/CLI ASR |
| Linux AppImage | One identical AppImage in builder and offline Ubuntu 24.04/Debian 13 without external runtime tools; real ASR, icon/plugin isolation, no-FUSE |
| Native acceptance | New Windows 11 and native Linux candidate hashes, actual models/import/download, cancel/close/retry/history/exports, keyboard/dialog/clipboard/display scope |
| Sources/licenses | Full application source plus exact build-input inventories, patches, recipes, original notices and Qt replacement/relinking materials |
| Asset policy | Exact 12-file inventory; no standalone checksum assets; all internal hashes preserved |

The [release procedure](../../build/releasing.md),
[candidate checks](../../testing/candidate-validation.md) and
[manual matrix](../../testing/manual.md) define the actual commands. Automated
tests cannot substitute native desktop acceptance. Native Windows 11, service
URLs/cookies, long media or model presets are unverified until separately recorded
with this candidate's hashes; no v0.4.0 waiver is transferred automatically.

## Delivery and publication boundary

Retain artifacts under `dist/releases/v0.4.1-<candidate-sha>/assets/` and complete
evidence beside that directory. `scripts/release_assets.py inventory` independently
computes local hashes and rejects extra/missing files, including checksum-only
assets. Package/build-input hashes are also recorded in technical manifests.
The external validation record must enumerate every required gate and blocker;
status is `BLOCKED` while any mandatory preparation check remains unmet.

No merge, tag or GitHub Release is part of preparation. After separate owner
authorization, verify final master SHA/CI and any source-tree changes before an
annotated tag. Changed build inputs require rebuilding affected packages and
renewed acceptance; old candidate evidence does not transfer automatically.
After upload, `release_assets.py verify-github` requires the GitHub SHA-256 digest
and size of **every uploaded asset** to match local bytes. Independently download,
rehash and smoke the public files. GitHub-generated source ZIP/TAR archives are
outside the uploaded asset set and require a separate tagged-tree comparison.

The actual 0.4.0 release documents/assets remain historical and unchanged by the
new no-checksum-asset policy. Source preservation does not imply a universal legal
or bit-reproducibility guarantee; record concrete coverage and upstream limits.
