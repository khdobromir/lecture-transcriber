# Modernization implementation report

Approval baseline: [development roadmap](development-roadmap.md).
Branch: `chore/modernization-roadmap`, based on
`926366a1c4f7ec2d1cc188f8448591809cc3282c` (`master`).

## Completed stages

| Stage | Changes | Verification |
|---|---|---|
| Roadmap | Saved approved scope, order, task criteria and icon requirement before product edits | Source SHA equals remote master; staged whitespace check |
| D0.1 | Reconciled published 0.4.0 status; retained historical candidate evidence | Release API date/assets; exact-SHA run links; whitespace and wording review |
| D1.1 | Updated source/binary/model dependency inventory and removed stale source-only publication claim | Compared vendored license, platform manifests, notices and source-material paths; whitespace check |
| D0.2 | English download-first README, supplied icon, actual isolated QML screenshot, recorded CLI help; GitHub About/topics | QML startup case 3/3 QtTest checks; CLI help/version; local README links; whitespace |
| D0.3 | Canonical PNG in Qt resource; Windows CLI/GUI ICO; installed Linux PNG and AppImage icon paths | GCC Debug GUI build; CMake install/startup from Unicode cwd; 5/5 packaging/source/QML suites; PNG/6 ICO sizes inspected |
| E1.1 | Portable/POSIX test helpers, Windows unit registration, Unicode-safe fixtures and common corruption case | GCC GUI build; 7/7 unit/pipeline/install/preflight/GUI integration suites; CLI 22/22, audio 22/22, features 10/10 cases (all 53 original Linux cases retained) |
| E1.2 | Disabled ambient yt-dlp plugins in both downloader calls; strengthened portable/POSIX mocks; explicit offline plugin smoke | Regression failed before flag and passed after; actual yt-dlp 2026.08.19 positive marker loaded, isolated marker not loaded, no network |
| D1.2 | Migrated all 28 original documents, retained forwarding addresses, added index/archive/manual entry, updated full source-material paths in both packagers | Compared all 28 document bodies with pre-migration HEAD (only link relocation/banners differ); 191 local Markdown links resolve; 4/4 packaging/source contract suites |
| E1.3 | Extracted test registration with root/config output paths, added eight schema-2 configure/build/test presets and nondestructive in-source guard | CTest JSON comparison preserves all 23 existing contracts; Linux CLI Debug/Release 18/18 each (GCC/Clang), GUI Debug/Release 23/23 each; all_qmllint; application-only Qt-free build; guard rejected temporary fixture without changing sentinel |
| D1.3 | English Quick Start/GUI guide/CLI reference/troubleshooting; concise Russian first-run and user guide; README/index navigation | All 19 help options documented; source comparison of defaults/data/cache/path/privacy semantics; CLI help/version and 22/22 CLI unit cases; preceding full CTest covers local/preflight/cancel/result/mock-URL scenarios; 233 local Markdown links checked before developer-guide additions |
| D1.4a | English developer/source/build/packaging/testing/release guides, source map and concise CONTRIBUTING; historical full mixed guides preserved | Documented preset loops already built/tested on all four Linux configurations; source/CI/packager comparison; 362 local Markdown links; no new runtime features or reduced release gates |
| D1.4b | Existing component/media/commit lifecycle diagrams, English GUI ownership, two ADRs and benchmark methodology | Compared diagrams/states/commit/error/resource claims with owned implementation and tests; 413 local links; historical lecture identifies development 206cdb5 / version 0.2.1 before v0.3.0, so README now names it precisely rather than calling it a v0.3.0 measurement |
| E1.1 follow-up | MSVC-specific independent test environment reader with RAII; explicit UTF-8 expected output path | Native CI 38153408846 exposed C4996/getenv as error in the newly portable units; corrected without suppressing warnings. Linux rebuild and affected suites passed; final native MSVC Debug/Release CLI and GUI presets passed in run 38155832264 |
| E1.1 native expectation | Engine-directory regression expects the platform's actual missing-engine diagnostic | Windows preflight explicitly reports `Нет tools/whisper-cli.exe`, Linux reports `Нет whisper-cli`; same case retained with platform-specific expectation; Linux CLI units rerun |
| E1.4 | Offline Whisper pin consistency contract registered on both platforms | Four unittest methods include all seven single-declaration mutations, consistent upgrade and malformed/duplicate fixtures; dependency_pins/package_source_identity/static_project_contract 3/3 CTest suites; no network or build script execution |
| D2.1 / D0.2 metadata | SECURITY/private reporting, concise English issue/PR templates accepting Russian, homepage points to pushed Quick Start | GitHub API confirmed private-vulnerability-reporting enabled and homepage/description; no invented email/SLA; local links/whitespace checked |
| D0.3 native verification | Windows final-ZIP smoke compares both application PE icon groups and every referenced image with the tracked source ICO | Python syntax and PE/package helper tests passed locally; final Windows ZIP smoke in run 38155832264 verified both PE icon groups and all six images against the source ICO |
| E2.1 | Pinned clang-format 18.1.8 and wheel hashes, editor policy, changed-line checker and CI gate; isolated formatting of six changed test files | Wrong version and malformed changed/new lines rejected; fix/check pass; unchanged legacy/vendor/generated bytes preserved; pinned local venv matches CI tool; GUI Debug 24/24 and Clang CLI Release 19/19 CTest; local Markdown links, shell syntax and ShellCheck pass |
| E1.3 native preset coverage | Existing Windows CLI/GUI jobs now build/test all four development presets; separate Release ZIP build and smoke retained | All four native presets passed in run 38155832264: CLI 12/12 per configuration, GUI 17/17 per configuration; separate Release package CTest also 17/17 |
| E1.2 / D0.3 final artifact checks | Final AppImage/ZIP smoke runs offline marker checks with the extracted bundled downloader; AppImage launcher/installed/.DirIcon artwork checked against tracked PNG | System and both extracted bundled yt-dlp 2026.08.19 positive/negative checks passed without network; final AppImage icon/.DirIcon checks passed; final artifact metadata reviewed |
| E2.1 container follow-up | Formatter step scopes Git safe.directory to the exact checkout through step-local environment, without changing global/user Git configuration | CI 38155525587 exposed container checkout ownership rejection; GIT_TEST_ASSUME_DIFFERENT_OWNER reproduced rejection, scoped environment accepted the same 12 files; malformed/version/exclusion fixture remains green |

## Historical first push checkpoint (2026-10-11, 5e0364d)

The first eight commits were pushed to `origin/chore/modernization-roadmap` at
the user's request. This paragraph preserves that intermediate state; the
remaining tasks were completed in the later stages recorded above and below.

The stricter downloader fixture initially masked the diagnostic expected by its
truncated-report regression. The follow-up fix validates report argument bounds
before isolation flags and before file operations; downloader isolation checks
remain enforced for valid calls. The failure was reproduced before the fix.

After rebuilding `mock_portable`, all 23 registered Linux CTest suites passed:
three affected integration suites and the remaining 20 suites in a separate run.
The latter ran outside the sandbox so the GUI controller's local HTTP server
could bind. These results cover the current Linux Debug GUI build; they do not
establish native Windows or final binary-package validation.

## Final scope review

All approved documentation/presentation tasks (D0.1–D0.3, D1.1–D1.4,
D2.1) and engineering tasks (E1.1–E1.4, E2.1) are implemented and their
automated checks passed. Native/final-artifact evidence and the remaining manual
acceptance boundaries are recorded separately below.

- The source tree stays in `src/`, `gui/`, `tests/`, `scripts/` and `packaging/`.
  Only CTest registration moved; names, helper locations, working directories,
  labels and timeouts were retained. No general architecture rewrite occurred.
- The Qt-free C++23 core, CPU engine integration, independent CLI, GUI-to-CLI
  JSONL v1 boundary, executable names, public flags/defaults, result format and
  user data locations remain. ASR/model authorship stays attributed upstream.
- The sole intentional runtime restriction is disabling yt-dlp plugin
  directories for both metadata and media download. Built-in extractors and
  URL/cookies argv remain. No GPU or additional user feature was introduced.
- Original `icon.png` bytes remain canonical; README and Qt use it directly.
  Tracked PNG/ICO derivatives require no converter during a normal build.
- English README/current technical and user guides have one documentation hub;
  Russian Quick Start/user guide are concise. All 28 original document addresses
  still forward to current guides or preserved history. Published validation is
  historical evidence, not a claim about this branch.
- Review found no additional blocker in the owned changes. Tracked-file checks
  found no build/runtime binaries, model weights or caches; concrete private-key
  and credential-pattern checks found no match. This is a scoped hygiene check,
  not a security certification. Requested icons and the isolated demo screenshot
  are intentional tracked assets.

## Verification commands and local results

| Check | Command / configuration | Actual result |
|---|---|---|
| Preset inventory | `cmake --list-presets=all` | Eight configure/build/test presets parse |
| Four Linux configurations | `cmake --preset NAME`, `cmake --build --preset NAME --parallel 2`, `ctest --preset NAME` | All four built/tested during E1.3: CLI 18/18, GUI 23/23 before adding dependency_pins |
| Final C++ style changes | GCC 16 GUI Debug; Clang 23.1.1 CLI Release; same build/test preset commands | GUI 24/24 and CLI 19/19 after adding dependency_pins and changed-line formatting |
| CLI compatibility | `transcribe --version`, `transcribe --help`, CLI units | Version remains 0.4.0; all 19 documented options, 22 CLI cases |
| Qt/QML | `cmake --build out/build/linux-gui-release --target all_qmllint`; GUI startup/model/integration suites | Lint passed; actual isolated GUI startup and controller tests passed |
| Application-only build | Separate `/tmp` configure/build with `-DBUILD_TESTING=OFF` and GUI off | Qt-free CLI built, version 0.4.0 |
| CTest migration | JSON inventory before/after extraction | All 23 prior contracts preserved; dependency_pins is the added suite |
| In-source guard | Configure a temporary fixture containing a sentinel | Rejected before project setup; sentinel/source files unchanged |
| Pins | `python tests/test_dependency_pins.py` | Four methods passed, including all seven single-declaration mutations and a consistent upgrade |
| Downloader isolation | `python tests/check_ytdlp_plugins.py /usr/bin/yt-dlp` | 2026.08.19; marker loads in positive control, does not load with isolation; no network |
| Formatter | Pinned venv, `python scripts/check-format.py --base origin/master` | Twelve changed owned C++ files; zero formatting differences |
| Formatter regressions | Temporary Git fixture, version mismatch and simulated different owner | Bad changed/new lines rejected, fix accepted, vendor/generated/untouched bytes retained; exact-checkout trust resolves ownership rejection |
| Shell checks | `bash -n install.sh scripts/*.sh`; `shellcheck -x install.sh scripts/*.sh` | Passed |
| Documentation | Local Markdown path/heading checks, staged tracked-path checks | 419 local links in 83 main/docs files; one additional SECURITY link; five heading links; no missing target |
| Packaging helpers | CTest package/source suites, `python tests/test_linux_package.py`, `python tests/test_pe_policy.py` | Passed; retry/error messages are intentional mocked HTTP fixtures |

Local GUI HTTP/download tests ran outside the execution sandbox so they could
bind localhost. No public-account credentials, private media or model files
were added to Git. Python syntax checks cover the changed final-package harnesses.
The local machine has no native MSVC/PowerShell runner; remote results below
establish the Windows checks instead.

## Remote verification

Implementation checkpoint for the final package checks:
`e6c2a8156696390416bdc8bdd5284c20d4ed6a17`.
The subsequent `df2b451827c8d9f435a84966b178dc00f152d04f` changes only
formatter-step Git trust and the stage report. Later commits clarify the
Unreleased/published package boundary in README/EN/RU guides, record this
report and clarify the VS 2022 generator minimum (CMake 3.21). They change no
runtime/test code, CMake, CI, dependency locks or packaging scripts. These final
documentation commits were checked for links/whitespace; binaries were not
rebuilt from their new documentation-only SHAs.

| Run | Scope | Result |
|---|---|---|
| [38153408846](https://github.com/khdobromir/lecture-transcriber/actions/runs/38153408846) | Intermediate dd9d0bf CI | Failed: new portable test helper used MSVC-deprecated getenv under /WX; fixed with scoped RAII _dupenv_s reader |
| [38154635296](https://github.com/khdobromir/lecture-transcriber/actions/runs/38154635296) | Intermediate 2c1df29 CI | Native MSVC CLI and several Linux jobs passed; run superseded before remaining package/static jobs completed |
| [38155525587](https://github.com/khdobromir/lecture-transcriber/actions/runs/38155525587) | e6c2a81 CI, real_smoke=true | Formatter hit container Git ownership rejection; corrected in df2b451; native CLI Debug/Release presets passed; run superseded for corrected full CI |
| [38155522183](https://github.com/khdobromir/lecture-transcriber/actions/runs/38155522183) | e6c2a81 final Linux AppImage, real_smoke=true | Passed: final payload/icon/bundled plugin checks, real GUI speech, same selected checksum in offline clean Ubuntu 24.04 and Debian 13 |
| [38155832264](https://github.com/khdobromir/lecture-transcriber/actions/runs/38155832264) | df2b451 corrected CI, real_smoke=true | Passed: 10/10 jobs, all native presets, Linux compiler/Qt/static/sanitizer checks, installed GUI speech and final Windows ZIP |
| [38155525058](https://github.com/khdobromir/lecture-transcriber/actions/runs/38155525058) | e6c2a81 real Whisper bootstrap/exports | Passed |

Cancelled/superseded runs and skipped steps are not counted as full passes.
All ten jobs in the corrected CI run and both separate workflows completed
successfully. Actual real-smoke steps ran. Retrieved metadata confirms matching
AppImage/source hashes across builder smoke and both clean offline containers;
Windows metadata confirms 2,689 verified files, both PE icons, minimal PATH,
bundled downloader isolation and real GUI/CLI speech. Native Windows 11 manual
acceptance remains explicitly unverified in that metadata.

| Candidate / retained inputs | Source | SHA-256 |
|---|---|---|
| Linux AppImage | `e6c2a8156696390416bdc8bdd5284c20d4ed6a17` | `00fd87efd65448d707e96595524beb479eab60bce073ccf929a0748dfe9b7b4e` |
| Linux build-inputs TAR.gz | Same candidate | `9c83dfaf046303acba96b37c114d646abcd1b8e66b0d39d6a5d9edcdd6b85854` |
| Windows ZIP | `df2b451827c8d9f435a84966b178dc00f152d04f` | `c0678898f4d4deb73e681fc07e3e8360e96ec4b94bf6aaa03d7d8db288dddc88` |
| Windows build-inputs TAR.gz | Same candidate | `376de272faa65b2464a8d0a8f492a82cf2ec274ad39b194c88efc7686f57bacf` |

Artifact IDs: Linux `11708905776`, Windows `11708504933` in the linked runs.
These are temporary 14-day Actions artifacts, not GitHub Releases. Only small
JSON/checksum entries were retrieved for review through HTTP range requests;
the multi-gigabyte artifact sets were not downloaded to this workstation.

## Known limitations and handoff

- Native Windows 11 interactive acceptance and Omarchy dialogs/portal/clipboard
  were not repeated in this session. Headless CI on Windows Server 2022 and
  offscreen Linux cannot establish those manual desktop scenarios.
- No personal-account/live VK/cookies acceptance was attempted. Mock URL argv,
  built-in offline extractor handling and public speech fixtures have distinct
  evidence; none is a universal live-service compatibility guarantee.
- No new speed, RAM or WER claim was produced. Historical benchmark records
  retain their original hardware/input/revision qualifications.
- Formatter installation supports the pinned Linux x86_64/Windows x64 wheels;
  it is a development tool, not bundled application code. Existing old C++ lines
  are not subject to a wholesale reformat.
- Two symlink cases and POSIX permissions remain Linux-only; native Windows
  ACL/reparse/handle checks remain in portable_platform.
- GitHub private reporting is enabled; About/topics/homepage were verified.
  Homepage currently targets this pushed branch's Quick Start. Change it to
  `master` only after a separately authorized merge.
- No merge, PR, tag movement, release publication or replacement of v0.4.0
  assets was performed. Branch artifacts are test candidates, not a new release.
  A later release still requires the complete exact-final-SHA/manual release gates.

## Local cleanup

Removed the four task-created `out/build/linux-{cli,gui}-{debug,release}`
directories, original temporary audit build, temporary application-only build and
icon install prefix after recording the checks above. These seven explicit
paths contained 4,919 entries, approximately 515 MiB apparent data (527 MiB
allocated-block estimate; filesystem compression/sharing can change actual free
space). They contained no tracked files and had no active process/file mapping
or nested mount. Installed CLI and canonical icon hashes matched before/after;
model/settings/history/result roots, Git/agent configuration and existing release
validation files were excluded. The task-local formatter/checker tools and
small downloaded CI metadata are removed after final documentation verification
and commit; unrelated earlier release-validation files are retained.

## Commit history

Conventional Commits in implementation order, excluding this final report update:

```text
802e070 docs: record approved modernization roadmap
e05d60b docs(release): reconcile published v0.4.0 status
26e60e1 docs: describe source and binary dependencies accurately
874e996 docs: present the desktop app and CLI in English
0fefdb4 feat(desktop): use the supplied application icon on both platforms
9a9d4b3 test: run portable unit suites on Windows
507ecd7 fix(media): disable ambient yt-dlp plugins
5e0364d fix(test): validate downloader report bounds before isolation flags
8fdaa7a docs: separate current guides from historical evidence
16a4194 build: add portable presets and isolate CTest configuration
5c14c4a docs: add English and Russian user onboarding guides
4881eba docs: document developer builds testing and release gates
dd9d0bf docs: explain architecture decisions and benchmark evidence
c725571 fix(test): support strict MSVC environment and Unicode paths
62b2b22 test: enforce consistent Whisper revisions across build entry points
b50a224 docs: add private security reporting and contributor templates
0e174e1 fix(test): expect the Windows bundle preflight diagnostic
2c1df29 test(windows): verify executable icons in the final ZIP
749117c build: check changed C++ lines with pinned clang-format
77f4b90 ci(windows): exercise all development presets natively
e6c2a81 test(packaging): check bundled plugin isolation and Linux icons
df2b451 fix(ci): scope formatter Git trust to the checkout
6b17f5e docs: distinguish unreleased changes from published packages
```

Use `git log --reverse --oneline master..chore/modernization-roadmap` for the
complete branch history, including the report/evidence handoff commit.
