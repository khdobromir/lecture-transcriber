# Release procedure

[Build](from-source.md) · [Candidate validation](../testing/candidate-validation.md)
· [Manual matrix](../testing/manual.md) · [Source materials](dependency-source-materials.md)

A package build is not permission to merge, tag or publish. Those are separate
maintainer actions. Never move an existing release tag or replace tested public
assets. v0.4.0 was published on 2026-10-11 from
`926366a1c4f7ec2d1cc188f8448591809cc3282c`; its
[final summary/journal](../releases/v0.4.0/validation.md) records actual evidence
and limitations. Earlier results do not validate a future release.

## Prepare and freeze

1. Finish code, dependency/packaging changes and `project(... VERSION ...)`, the
   single version source. Update README, CONTRIBUTING, CHANGELOG and
   `docs/releases/vX.Y.Z/{notes,validation}.md` before freeze.
2. Build GCC/Clang/MSVC with warnings-as-errors; run relevant CTest, QML lint,
   static analysis, ASan/UBSan/LSan, application-only build, install and help/version.
   Check data/settings/model/result preservation and legacy history upgrade.
3. Create small Conventional Commits and a clean candidate branch/PR. Record
   source SHA, tree/fingerprint and dirty flag. An RC name is not a readiness claim.

## Automated package evidence

For automated evidence, dispatch **CI** and **Linux portable GUI** on the exact candidate source with
`real_smoke=true`, plus **Real Whisper smoke**. Inspect the jobs and
actual real steps; a skipped step does not establish a pass. The CI matrix is
[defined here](../../.github/workflows/ci.yml); do not freeze a job-count claim
that can drift. Record source SHA and concrete run links.

```sh
bash scripts/package-linux.sh --container --release --real-smoke --result-file /output/candidate.json
python3 tests/check_linux_candidate.py dist/candidate.json
```

```powershell
./scripts/package-windows.ps1 -QtRoot C:/Qt/6.8.3/msvc2022_64 -Release -RealSmoke
```

Release mode forbids dirty source and skipped tests. Source identity is captured
before configure and rechecked after final smoke. Windows validates the extracted
final ZIP before publishing it to the output directory. Linux builds once and
checks that same AppImage SHA-256 in builder and offline clean Ubuntu/Debian,
without Qt/Python/FFmpeg/yt-dlp installed. Real mode uses the same public input/model
for local ASR there. No post-acceptance repacking or opportunistic glob selection.

Retain application/source/dependency/input/model/package hashes, toolchain,
manifest, test logs and verified build-input bytes. Moving continuous URLs are
not immutable sources. After a payload/source change, use a new candidate identity and refresh affected
evidence rather than assigning old test results to changed bytes.

## Manual scope and privacy

Optional desktop checks cover final ZIP/AppImage hashes on clean Windows 11 and native Linux:
Unicode unpack, first model import/download, local ASR, cancel/close/retry/history,
TXT/SRT/VTT, keyboard, dialogs, clipboard and display scales. Record small,
medium+VAD and turbo/chunks as actually exercised. Service URL/cache/network
checks are separate from local recognition. See the [manual checklist](../testing/manual.md).

The v0.4.0 maintainer decisions waived Ubuntu desktop, cookies and 10–15 minute
recording requirements while keeping clean-container checks and native acceptance.
The user confirmed VK/cache and a five-minute Windows 11 24H2 recording. Preserve
these dated decisions; do not claim waived cases passed or silently extend their
scope to later releases. Record each release's actual testing scope explicitly.

Every result names OS/build and source/package/input/model hashes. Private media,
URLs/cookies/transcripts remain local; CI uses pinned public fixtures. Server
2022 with SDK is not a clean Windows 11 desktop; offscreen is not native portal
testing; short ASR smoke is not WER. Package verification, including manual
desktop testing, is optional and is not a prerequisite for release publication.
This policy was set by the maintainer on 2026-10-11. Keep existing automated smoke
and internal integrity checks enabled; record missing checks and known limitations
without presenting them as passed.

## Binary source and notice materials

Map actual payload inventory to verified sources, recipes, patches, build settings
and license/notice texts. Preserve shared-library replacement/relinking materials.
The application source archive and build-input archives form a versioned set;
neither alone replaces the whole distribution source offer. Do not infer legal
completeness from a helper succeeding or from upstream package naming.

| Material / check | Owned implementation |
|---|---|
| Binary/source pins, platform/Qt mapping | [source-inputs.json](../../packaging/source-inputs.json), [selector](../../scripts/package_inputs.py) |
| Qt original sources/notices/settings and ICU SDK identity | [Qt notices](../../scripts/package_notices.py), [SDK sources](../../scripts/package_sdk_sources.py), [SDK settings](../../scripts/package_sdk_settings.py) |
| Standalone yt-dlp embedded notices without execution | [collector](../../scripts/package_standalone_notices.py) |
| Copied Linux libraries/fonts/CA payload and authenticated exact APT sources | [library provenance](../../scripts/package_linux_notices.py), [source provenance](../../scripts/package_linux_sources.py) |
| Runtime/libfuse/squashfuse/musl and exact Alpine recipes | [AppImage runtime sources](../../scripts/package_runtime_sources.py) |
| FFmpeg binary enable flags, parent/recipe/source identity and notices | [FFmpeg dependency collector](../../scripts/package_ffmpeg_sources.py) |
| Git/Gitiles/SVN original source retention | [Git](../../scripts/package_git_sources.py), [canonical TAR](../../scripts/package_canonical_sources.py), [SVN](../../scripts/package_svn_sources.py) |
| Cargo registry crates and native yt-dlp dependencies | [registry collector](../../scripts/package_cargo_sources.py), [materials guide](dependency-source-materials.md) |

Exact source versions/checksums are required: no nearest-version fallback. Cache
hash mismatch, unsafe paths/redirects, missing notices or unmatched SDK/binary
pins fail packaging. Preserve source data without executing it during collection.
Canonical Gitiles TARs intentionally pin normalized retained bytes; other archives
pin original bytes. Git/SVN helpers retain explicit revisions/properties and do
not run checkouts/hooks. Bounded transient-network retries never relax hashes.

[Platform notices](../../THIRD_PARTY.md) and
[v0.4.0 coverage](../releases/v0.4.0/binary-source-coverage.md) explain concrete
materials and residual upstream reproducibility limits. In particular rav1e's
recipe-time `cargo update cc` does not prove the final dependency resolution.
Retained-input `corresponding_sources_complete=false` is not automatically a
verdict about the full application-plus-input deliverable. Validate that actual
combined set, preserving explicit limitations and original legal texts.
The [pre-modernization inventory](../archive/releasing-before-modernization.md)
retains the detailed historical component/recipe investigation.

## Authorized merge and publication

### Asset integrity without standalone checksum files

Starting with 0.4.1, do not generate or upload `.sha256`, `SHA256SUMS`,
`SHA256SUMS.txt` or other checksum-only assets. Package/build-input digests remain
in the adjacent manifests/validation JSON. Clean-container smoke obtains expected
AppImage/audio/model hashes from the checked candidate record and verifies the
mounted bytes directly; it requires no checksum file.

Stage the already-tested bytes under canonical versioned names without repacking.
Keep the full application source archive and both platform build-input archives,
notices, technical manifests and validation/source-offer evidence. The exact
12-file set is defined by [release_assets.py](../../scripts/release_assets.py).
Keep additional CI logs and local inventory outside `assets/`; never include
private recordings, model weights or credentials in public evidence.

```sh
python3 scripts/release_assets.py inventory "$delivery/assets" --version "$version" > "$delivery/asset-inventory.json"
```

After **separate publication authorization**, upload only that checked set.
Query GitHub's release JSON after upload and require every asset's `digest`
(`sha256:<hex>`) and size to match the freshly recomputed local inventory:

```sh
gh api "repos/khdobromir/lecture-transcriber/releases/tags/v$version" > "$delivery/github-release.json"
python3 scripts/release_assets.py verify-github "$delivery/assets" --version "$version" --github-json "$delivery/github-release.json"
```

On a missing digest, mismatch, extra/missing asset or incomplete upload, the
verification command fails; do not silently use an old checksum file as fallback. This uses
[GitHub's release-asset digest API](https://docs.github.com/en/rest/releases/assets#get-a-release-asset).
GitHub's automatically generated `Source code (zip/tar.gz)` links are not uploaded
assets; independently compare those archives with the checked tagged tree.
Preserve historical release checksum files and their dated instructions.

### Authorized owner actions

1. After maintainer authorization, merge the candidate into master
   with a Conventional Commit, e.g. `chore(release): merge vX.Y.Z into master`.
2. Record final master SHA and CI results. If the code/payload tree changed,
   rebuild affected artifacts and refresh evidence; old candidate results do not
   describe changed bytes.
3. Match exact-SHA manifests, checksums, manual scope and complete material set.
4. Confirm the tag is absent. Create an annotated `vX.Y.Z` on checked master;
   compare `git rev-parse vX.Y.Z^{commit}` and the remote peeled tag.
5. Prepare notes and the already-checked assets; exclude private logs/media/model
   weights. Publish those bytes without repacking. There is no automatic release
   publication workflow in this repository.
6. Independently download/rehash public assets to a new directory and smoke them.
   Check the GitHub source archive and its tagged tree separately.
7. Record final-SHA runs, tag, release and hashes in validation. Clean only
   provenance-checked task-owned temporary outputs, preserving retained evidence,
   build inputs and all user data. Do not remove unrelated branches or files.
