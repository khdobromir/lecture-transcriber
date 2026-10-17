# v0.5.0-beta.1 validation scope

This is a prerelease for the Material You / Material 3 GUI. The maintainer
authorized PR, merge and beta publication on 2026-10-17. Stable v0.5.0 remains
conditional on manual Windows acceptance. A beta label is not a claim that
unperformed checks passed.

## Source and version

CMakeLists.txt defines numeric project version 0.5.0 and suffix `-beta.1`.
The resulting `0.5.0-beta.1` is shared by CLI, GUI and package manifests.
Release assets must come from the final clean merge commit on master, with an
annotated `v0.5.0-beta.1` tag pointing to that commit. The release is a GitHub
prerelease and must not replace v0.4.1 as latest stable.

The versioned notes and this file define the scope before the source freeze.
The uploaded `release-validation.json` records final commit/tree, exact CI run
links, actual outcomes, package hashes and open checks without changing the
source commit merely to record its own identity.

## Verification gates

| Area | Required evidence |
| --- | --- |
| Source | Clean exact merge SHA, tree/fingerprint, no private or generated payload |
| PR and master | Completed CI on the current PR head and final master SHA; no unresolved failures |
| GUI | Qt build/tests, translations, QML lint, palette contrast, persistence, responsive thresholds and component states |
| Runtime | CLI version/help, actual packaged startup and bundled tools, real speech with TXT/SRT/VTT |
| Linux | One exact AppImage, integrity manifest and retained build inputs; local acceptance scope is Omarchy x86_64 / Wayland |
| Windows | Final ZIP and automated build/package checks; manual Windows 11 acceptance explicitly open |
| Provenance | Matching application sources, pinned dependency/build-input archives, notices and package hashes |
| Publication | Annotated tag peeled SHA, prerelease/non-latest state, all uploaded sizes and SHA-256 digests, tagged source archive |

The implementation's earlier local evidence covers Qt 6.8.3, 27 CTest suites,
177 complete English translations, QML lint, ASan/UBSan with leak detection,
synthetic page/component screenshots, native Omarchy startup and real medium-model
speech in a diagnostic preview. That preview predates final release preparation;
its hashes and results must not be attributed to the beta binaries.

Existing CI environments remain automated infrastructure. They do not expand
the claimed local desktop acceptance scope. Native Windows, service-specific
URLs/cookies, long recordings and every model/display combination remain
unverified unless new evidence is recorded for the exact final package.

## Delivery

Retain the 12-file package/source/build-input/evidence inventory defined by
`scripts/release_assets.py`, using full prerelease versions in filenames.
Do not publish model weights, private recordings, cookies or standalone checksum
assets. Validate the GitHub SHA-256 digest and size for every uploaded file.
Preserve the final delivery and compact evidence; clean only disposable artifacts
created by this release task, keeping installed applications and user data intact.
