# Approved development roadmap

Approved by the maintainer on 2026-10-11. This document records the implementation
plan before product changes. Progress and verification belong in
`docs/development-report.md`; they do not rewrite the approval baseline below.

## Baseline and constraints

- Repository: https://github.com/khdobromir/lecture-transcriber.
- Base: `master`, commit `926366a1c4f7ec2d1cc188f8448591809cc3282c`.
  Local HEAD/master/origin/master and the remote branch were checked equal.
- Release v0.4.0 is published; its annotated tag resolves to that commit.
- Work on a separate branch, with small Conventional Commits. Never merge into
  master, move a published tag, replace published assets, or publish a release
  without separate authorization.
- Preserve native C++23, Qt Quick, the independent CLI, Qt-free core,
  CPU-only execution, Linux/Windows compatibility, user data locations,
  command-line defaults, result layout, and JSONL protocol v1.
- Do not add web/TUI/cloud/LLM/GPU features or replace whisper.cpp.
- English README and current technical guides; short Russian Quick Start and
  user guide. Historical evidence stays in its original language.
- Keep process supervision, cancellation/completion boundaries, model hash/range
  checks, fail-closed bundles, package provenance, notices, and source materials.
- New maintainer requirement: preserve root `icon.png`, show it in README,
  embed a derived Windows icon in executables, and use it for Linux desktop,
  AppImage, and GUI window integration. This adds branding, not a new feature.

The read-only audit built Debug/GUI with GCC 16.2.1, CMake 4.4.4 and Qt 6.12.0.
22 of 23 CTest suites passed inside the sandbox; the localhost HTTP fixture in
`gui_controller` passed on rerun outside the socket-restricted sandbox.
QML lint, ShellCheck, shell syntax, and whitespace checks passed.
These are baseline observations, not proof for later commits or release bytes.

Exact-base successful GitHub runs:

- [CI](https://github.com/khdobromir/lecture-transcriber/actions/runs/38140262175)
- [Linux portable GUI](https://github.com/khdobromir/lecture-transcriber/actions/runs/38140264088)
- [Real Whisper smoke](https://github.com/khdobromir/lecture-transcriber/actions/runs/38140266652)

Published binary bytes were not independently downloaded/rehashed in the audit;
Windows was not manually tested in that session. Current tracked-file secret
signatures are not a complete Git-history secret audit. Preserving build inputs
does not establish bit-for-bit reproducible binaries.

## Findings and reference decisions

The main urgent defect is contradictory release status, not a demonstrated
critical runtime failure. README has useful detail but no concise download-first
entry, screenshot, or engineering map. Active instructions and historical reports
are mixed in flat docs. Root THIRD_PARTY describes the older source-only model.
Some portable unit cases are excluded on Windows because support.hpp includes a
POSIX harness. yt-dlp ignores config but still permits ambient plugin loading.
Build commands repeat; presets and an in-source configure guard are absent.

Keep src/, gui/, scripts/, packaging/, third_party/ and existing test translation
units in place. Do not merge packaging helpers merely because there are many.
Vendored nlohmann/json, source-inputs, license texts, and Windows patches are
intentional inputs. No confirmed dead-code deletion is approved.

Reference snapshots were inspected as untrusted text, never executed:

| Project and immutable snapshot | Adopt and why | Cost | Do not copy |
|---|---|---|---|
| [Flameshot](https://github.com/flameshot-org/flameshot/tree/019bea68a4a2e4e0d311cce80eda0652c55b81df): src/CMakeLists.txt, src/main.cpp, docs/CONTRIBUTING.md, docs/dev/mkdocs.yml, workflows | Demo, component map and developer entry reduce navigation cost | S/M | All package formats, IPC architecture and platform matrix |
| [bat](https://github.com/sharkdp/bat/tree/d9559c69f541873f8c47ac8ad81d985f4441edf6): README, CHANGELOG, tests/integration_tests.rs, doc/release-checklist.md | Concrete CLI examples and executable contracts make behavior reviewable | S/M | Rust/Cargo, syntax assets and unsupported install channels |
| [OBS](https://github.com/obsproject/obs-studio/tree/7d98bebe1115b12608837b622164ea5fe6d9483a): libobs, frontend, CMakePresets.json, CODESTYLE.md | Describe engine/frontend boundary; presets reduce build-command drift | S/M | GPU/plugin architecture, SDK scale, destructive in-source cleanup |
| [Gum](https://github.com/charmbracelet/gum/tree/879f048103adf0214b85943b52d8d65b08d772c5): README, input/command.go, build/release config | Real visuals and short progressive examples improve onboarding | S | TUI, decorative complexity, floating remote workflow/PAT dependencies |
| [uv](https://github.com/astral-sh/uv/tree/5cc7f15c359b83eed2c68c5578c1000bfd27e0da): mkdocs.yml, CONTRIBUTING, integration tests, test/release workflows | Separate user/developer/reference/history; explain environment isolation | M | Workspace/release scale, package-manager stack |
| [GitHub CLI](https://github.com/cli/cli/tree/ec5b512045db67e5a2a4ff4a1b02660b2fb24390): docs/project-layout.md, command-development.md, testing.md, release docs | Source map, injected seams and unit/live distinction clarify evidence | S/M | API/auth architecture, Go frameworks, full signing infrastructure |
| [qBittorrent](https://github.com/qbittorrent/qBittorrent/tree/f608dc38556583ac81ff9a1256a3ab1de5114d6f): src/base, src/gui, test/CMakeLists.txt, CODING_GUIDELINES.md | Component ownership, compile policy and concrete bug reports | S/M | WebUI, private Qt API, huge translation/dependency matrix, source-tree codegen |
| [AI Video Transcriber](https://github.com/wendy7756/AI-Video-Transcriber/tree/7f1c6197766937adc98596a2641109477a1a8ee4): backend, static UI, CLI, tests, Docker | Clear first-run stages, recovery and output presentation | S/M | Python/web architecture, LLM/subtitle/MCP features just for parity |

The comparison project also uses CPU (faster-whisper int8); it is not evidence
that switching engines improves Transcribe. Its shared pipeline helpers are not
the sole orchestration owner. Its lower-bound requirements and lack of GitHub
workflows/releases at inspection are not better reproducibility examples.

### Twelve modernization directions

| Direction | Current gap | Approved practice and expected effect | Cost/risk |
|---|---|---|---|
| README | Long first route, no demo | Gum/Flameshot/bat: concise download-first examples | M; false claims/privacy |
| Structure | Mixed docs | uv/CLI: task navigation and archive | M; broken paths |
| Architecture | Decisions hard to discover | OBS/qBittorrent: documented existing boundaries | M; avoid needless refactor |
| Documentation | Stale release status | uv/CLI: active guides separate from evidence | L; translation drift |
| Code standards | No formatter policy | OBS/Flameshot: fixed formatter, touched lines | M; churn |
| Testing | Linux-only portable cases | bat/CLI: cross-platform contracts | M; lost cases |
| CI/CD | Job purposes not explained | uv/OBS: evidence map, preserve checks | S; do not reduce coverage |
| Install/build | Source-first presentation, no presets | CLI/OBS: binary install separate from development | M; path compatibility |
| Releases | Docs lag publication | bat/CLI: final status linked to exact evidence | S; immutable assets |
| Community | Scattered contribution instructions | Flameshot/qBittorrent: concise templates | S; no unnecessary governance |
| Security | Ambient yt-dlp plugins | Isolate environment, retain process/package checks | M; plugin compatibility |
| Portfolio | No engineering map | Link claims to native implementation and tests | M; no borrowed-credit claims |

Recruiters should understand purpose/stack/platform/readiness in 30–60 seconds.
Engineers should reach process, protocol, cancellation, model and package work in
5–15 minutes. Machine readers must distinguish owned implementation, external
ASR, service languages, tested environments and limitations. No keyword stuffing.

## Target layout and migration

```text
README.md, CHANGELOG.md, CONTRIBUTING.md, LICENSE, THIRD_PARTY.md
CMakeLists.txt, CMakePresets.json, icon.png
src/                         existing core/CLI, unchanged layout
gui/                         existing C++/QML, unchanged layout
tests/CMakeLists.txt
tests/support.hpp            compatibility include for POSIX suites
tests/support_portable.hpp
tests/support_posix.hpp
tests/test_dependency_pins.py
scripts/, packaging/, third_party/   preserve existing responsibilities
docs/README.md
docs/development-roadmap.md   this approval baseline
docs/development-report.md    implementation evidence and limitations
docs/assets/                 real GUI screenshot and CLI session
docs/user/{quick-start,guide,cli}.md
docs/user/ru/{quick-start,guide}.md
docs/developer/getting-started.md
docs/architecture/{overview,protocol-v1,gui-states}.md
docs/architecture/adr/{0001-cli-process-boundary,0002-cpu-runtime-and-bundles}.md
docs/build/{from-source,linux-portable,windows,releasing,dependency-source-materials}.md
docs/testing/{README,integration-coverage,candidate-validation,manual}.md
docs/troubleshooting.md
docs/benchmarks/README.md
docs/releases/v0.4.0/{notes,validation,binary-source-coverage}.md
docs/archive/                original historical reports
```

| Existing path | Destination | Required updates |
|---|---|---|
| Root CMake test blocks | tests/CMakeLists.txt | add_subdirectory; preserve target names, labels, timeouts, dependencies and root/config output paths |
| tests/support.hpp generic/POSIX code | support_portable.hpp / support_posix.hpp | Unit includes and fixtures; old support.hpp includes both for integration compatibility |
| docs/protocol-v1.md, gui-states.md | docs/architecture/ | README/CONTRIBUTING/relative links |
| docs/linux-portable.md | docs/build/linux-portable.md | User steps to user guide; links |
| docs/windows-gui.md | user/guide, build/windows, architecture/overview, testing/README | Split purposes; old page becomes navigation |
| docs/releasing.md | docs/build/releasing.md | Links and final status |
| docs/dependency-source-materials.md | docs/build/dependency-source-materials.md | Both packagers must retain the full new document, not the legacy stub |
| docs/integration-coverage.md, candidate-validation.md | docs/testing/ | Links and current commands |
| stabilization-scenarios, windows-11-manual-validation | Active instructions to testing/manual; original reports to archive | Preserve dated results and waivers |
| release-notes-v0.4.0, release-validation-v0.4.0, binary-source-coverage-v0.4.0 | docs/releases/v0.4.0/ | Final summary plus preserved historical journal |
| lecture-validation-2026-10-03 | docs/archive/ | Link with version/hardware/input limitations from benchmarks guide |
| Other historic documents | docs/archive/original-name | Archival banner, navigation, relative links |

Other historic files: dev-validation, feature-validation, interface-options,
linux-portable-validation, release-review-v0.3.0, stabilization-validation,
test-validation, release notes 0.1.0/0.2.0/0.2.1/0.3.0 and release validation
0.2.0/0.2.1/0.3.0. Keep old moved-page addresses as short forwarding documents
for at least one next release. Do not alter old Git tags. GUI target output paths
stay unchanged; relocated test targets keep existing root Debug/Release paths.

## README and documentation specification

README: approximately 150–200 lines, English, with product description, icon,
real GUI/CLI demo, 5–7 verified features, Quick Start, installation matrix,
2–3 examples, small component diagram, Engineering Highlights, performance and
limitations, development/testing, contributing, license. Detailed option tables,
source prerequisites and packaging commands belong in linked guides.

Document Linux x86_64/glibc 2.39+ and Windows 11 x64 package targets. Models are
separate; first offline use requires prior model import. Do not infer Windows 10,
all Linux, byte-reproducibility, speed or accuracy guarantees.

Engineering Highlights link owned code/tests: Linux supervisor/Windows Job
Object; Qt workers/lifetimes; versioned JSONL/completion; sample/timestamp bounds;
verified model publication; clean-environment package checks. Credit whisper.cpp,
models, Qt and media tools as dependencies.

User guides cover GUI, models/data, history/results, cancellation, partial files,
cookies/privacy and troubleshooting. CLI reference is checked against main/cli.
Developer guide covers prerequisites, quick test loop, source map and PR policy.
Testing separates unit/integration, real/network, manual and final package checks.
Benchmarks explain methodology and explicitly historical 0.3.0 results; no WER
or universal 0.4.0 performance claim is approved.

Architecture needs only three useful diagrams: components, media flow,
task/completion/cancellation lifecycle. Reuse protocol and state documents.
Explain error/resource ownership and two ADRs: process boundary/one task per
backend, and CPU/external tools/separate models/fail-closed bundles. No class UML
catalogue, new service framework, multi-task server or speculative Options split.

## Tasks: documentation and presentation line

### D0.1 — P0 — Published v0.4.0 status (S)

- Problem/evidence: CHANGELOG.md, docs/releasing.md and release-validation-v0.4.0
  still say unpublished/not admitted although GitHub Release is published.
- Change: final date/SHA/run/assets summary; preserve historical decisions and
  manual limitations, correct current headings.
- Why: trustworthy readiness and traceable release evidence.
- Dependencies: none. Risk: rewriting history; published bytes/tags stay intact.
- Acceptance/check: current documents agree with Release/API; links resolve to
  the exact commit/runs; search and inspect remaining candidate wording.

### D0.2 — P0 — README and GitHub presentation (M)

- Problem: README is long, source-first, Russian, no visual/engineering map;
  About/topics/homepage are empty.
- Change: implement README specification; real sanitized screenshot/session;
  About `Local CPU transcription of audio and video, with a native C++23 CLI and
  Qt Quick desktop GUI.` Topics cpp, cpp23, qt, qml, cmake, speech-to-text,
  whisper-cpp, linux, windows, offline. Homepage points to completed Quick Start.
- Why: recruiter/user entry and reviewable technical achievements.
- Depends D0.1. Risks: privacy, false demos/benchmarks/platform claims.
- Acceptance/check: first screen shows purpose, interfaces, stack, download and
  readiness; claims have implementation/test links; examples are exercised.

### D0.3 — Maintainer-approved addition — Application icon (M)

- Source: root icon.png supplied by maintainer; preserve original bytes.
- Change: README image, Qt window icon, Windows executable ICO resource,
  Linux installed desktop icon and AppImage icon from the same image.
- Depends D0.2. No ASR or CLI behavioral changes.
- Acceptance/check: validate PNG/ICO resources, Qt resource lookup, CMake install
  and packager paths; build GUI and inspect installed Linux layout. Windows PE
  icon checked on a native Windows build when available; disclose if untested.

### D1.1 — P1 — Dependency inventory (S)

- Problem: THIRD_PARTY.md describes source-only layout and misses JSON/Qt.
- Change: English owned/vendor/runtime/model inventory; source vs AppImage vs ZIP;
  link pins, licenses, full notices and source materials.
- Dependencies none; runs alongside D0.1. No legal-compliance guarantee.
- Acceptance/check: compare tracked contents, dependency manifests and notices;
  no false claim that the source archive contains no third-party libraries.

### D1.2 — P1 — Documentation migration and navigation (M)

- Problem: active guidance and evidence share flat docs/.
- Change: migration table above, docs/README, archival banners, old-path stubs;
  update README/CONTRIBUTING/CHANGELOG and both source-material packaging paths.
- Depends D0.1; update D0.2 links. Risks: missing links/source materials.
- Acceptance/check: all 28 original documents accounted for without information
  loss; local links resolve; old paths classified; packaging contracts and
  material lists pass. New binary release still needs ordinary package gates.

### D1.3 — P1 — EN/RU user guides and CLI reference (M)

- Problem: user instructions spread across README/windows-gui/linux-portable.
- Change: English Quick Start/guide/CLI and concise Russian Quick Start/guide;
  troubleshooting; flags/defaults, models/data/offline, cookies, cancellation,
  partial results and log redaction. Do not translate every technical archive.
- Depends D1.2. Risks: drift and duplicated instructions.
- Acceptance/check: binary first run separate from developer build; compare
  flags/help/parser; local file, missing model, cancel/result and URL-auth error
  scenarios; Windows instructions match package/manual checklist evidence.

### D1.4 — P1 — Developer/architecture/testing/build guides (L)

- Problem: engineering decisions, evidence levels and prerequisites hard to find.
- Change: English active guides, three diagrams, two ADRs, source/CI map; concise
  CONTRIBUTING; benchmark methodology with historical limitations.
- Depends D1.2. May use two commits for developer/testing/build and architecture.
- Risks: invented architecture, duplicated stale commands.
- Acceptance/check: CLI/GUI and quick tests reproducible from guides; diagrams
  agree with code/CMake; error/ownership/cancellation boundaries have links.

### D2.1 — P2 — Security reporting and English templates (S)

- Problem: no private security-report route; Russian-only templates.
- Change: enable GitHub private vulnerability reporting and short SECURITY.md;
  English issue/PR templates accepting Russian reports; request version/OS/install
  route/repro/redacted logs, never private media/cookies/tokens.
- Depends D1.3. No invented email/SLA; CoC/governance not mandatory.
- Acceptance/check: private route accessible and links/templates correct.

## Tasks: engineering line

No runtime P0 fix was established by the audit. Documentation line can complete
without architecture changes. Engineering PRs include necessary companion docs.

### E1.1 — P1 — Portable unit suites on Windows (M)

- Problem/evidence: root CMake NOT WIN32 gate and tests/support.hpp POSIX includes
  exclude CLI/audio/features cases that are substantially platform independent.
- Change: portable/POSIX helpers; preserve independent fork harness and old
  support.hpp include; portable runner/temp/files/WAV helpers; adapt Unicode and
  engine fixture paths; Linux keeps permissions/symlink cases; Windows uses its
  own reparse/ACL cases rather than false POSIX assertions.
- Dependencies none. Risks: case loss, Windows privilege/path differences.
- Acceptance/check: Windows runs common suites; Linux case inventory retained;
  MSVC and GCC/Clang, process/GUI/portable/package checks. Explicit OS exclusions.

### E1.2 — P1 — Disable ambient yt-dlp plugins (M)

- Problem: src/media.cpp --ignore-config does not disable upstream plugin loader.
  This is a hardening opportunity, not demonstrated URL remote-code execution.
- Change: shared downloader argv gets --no-plugin-dirs; both metadata/download
  checked. Bundled 2026.08.19 supports it; system installs need this option.
  No unsafe fallback. Document unsupported custom extractor plugins.
- Dependencies none. Risk: intentional custom-plugin compatibility restriction.
- Acceptance/check: argv regression for both calls and URL -- separator; isolated
  marker plugin not executed; pinned bundled/system tool, builtin extraction and
  URL failure/cookies argument checks without personal credentials.

### E1.3 — P1 — Presets, CTest organization, in-source guard (M)

- Problem: duplicated build commands, no presets/guard, mixed root test config.
- Change: tests/CMakeLists; preserve names/labels/timeouts/dependencies/output
  paths, including root multi-config test DLLs. Presets schema 2: Linux CLI/GUI
  Debug/Release (Unix Makefiles), Windows CLI/GUI Debug/Release (VS 2022 x64),
  out/build/<preset>; normal Qt discovery, no local paths. Fail nondestructively
  on in-source configure. Keep CLI CMake 3.20, GUI CMake 3.24/Qt 6.8 minimums.
- Depends E1.1. Risks: multi-config/helper DLL and package harness paths.
- Acceptance/check: CLI without Qt, GUI, Debug/Release/MSVC, ctest -N inventory,
  static contract and packaging paths; guard tested only in a temporary fixture.

### E1.4 — P1 — Dependency-pin consistency contract (S)

- Problem: repeated whisper revision in installer, smoke, workflow/manifests
  currently agrees but can drift on upgrade.
- Change: Python unittest test_dependency_pins.py registered with CTest compares
  specific declarations without executing scripts/network. Keep existing source
  mapping checks and installer runtime prerequisites. Platform media-tool pins
  do not have to equal each other.
- Depends E1.3. Risk: brittle matching; fail with precise declaration diagnostics.
- Acceptance/check: baseline accepted, isolated one-pin mutation rejected,
  consistent update accepted; Linux/Windows, no downloads.

### E2.1 — P2 — Formatting policy without mass churn (M)

- Problem: no formatter/editor policy despite static analysis.
- Change: clang-format 18 close to existing four-space/braces/compact style;
  .editorconfig UTF-8/LF/final newline. Check changed owned C++ lines against base;
  no vendor/generated files or wholesale formatting. Separate style from logic.
- Depends E1.1–E1.4. Risks: formatter version mismatch and irrelevant churn.
- Acceptance/check: malformed changed line rejected; vendor/generated excluded;
  local/CI version matches, existing unmodified code not forced to reformat.

## Execution order and verification

1. Save this roadmap first, on a branch from current master.
2. D0.1 and D1.1; then D0.2 and added D0.3. E1.1/E1.2 independent.
3. D1.2; E1.3 after E1.1.
4. D1.3 and D1.4; E1.4 after E1.3.
5. D2.1 and E2.1 after their prerequisites.
6. Final requirements review and development report with commit history,
   actual commands/results, unverified platform/package checks and residual risks.

One task is a small coherent commit (or two for D1.4); apply code review, repository
hygiene and fresh verification before committing. Do not claim Windows/new-SHA CI
success from old master runs. Pushing/PR creation are not needed to finish local
implementation; no merge or release is authorized.

| Change | Required verification |
|---|---|
| Documentation | Local links, source claims, commands, EN/RU consistency, demo privacy |
| Packaging material paths | Material lists and source/package contract tests |
| Portable helpers | Retained Linux cases, added Windows cases, GCC/Clang/MSVC |
| CMake/presets | CLI/GUI Debug/Release, multi-config layout, CTest/static contract |
| yt-dlp | Metadata/download argv, marker plugin, builtin URL/error behavior |
| Pins | Baseline and inconsistent/consistent mutation fixtures |
| Future binary release | Exact-final-SHA CI, real Whisper, final AppImage/ZIP hashes, clean smoke, notices/source inputs |

Keep cancellation, crashes, partial output, Unicode/spaces, protocol faults,
corrupt models, HTTP resume, cache and incomplete bundle scenarios. Do not
replace these with superficial tests or weaken release gates.

Completion of P0/P1 means truthful README/metadata, EN/RU first run, one docs
navigation entry, preserved archival evidence, portable Windows units without
Linux loss, disabled ambient plugins, working presets/path compatibility and pin
checks. The product remains native CPU-first C++/Qt, with unchanged public CLI,
result format, JSONL v1, executable/install names and user data paths. Plugin
isolation is the sole intentional supported-behavior restriction and is recorded
under Unreleased, never by modifying the published 0.4.0 assets.
