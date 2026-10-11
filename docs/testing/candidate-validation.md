# Candidate validation

[Testing](README.md) · [Manual matrix](manual.md) · [Release procedure](../build/releasing.md)

This is a procedure, not a declaration that the current branch is release ready.
Historical observations and waivers are in the [v0.4.0 journal](../releases/v0.4.0/validation.md)
and [pre-modernization guidance](../archive/candidate-validation-before-modernization.md).

## Exact source and final artifacts

After the last source/dependency/packaging change, dispatch CI and Linux portable
on the candidate branch with `real_smoke=true`, and run Real Whisper smoke.
Inspect actual steps: a green run with skipped real checks is insufficient.
PR CI normally tests a merge tree; record the source SHA actually executed.
Package manifests include source/version/fingerprint and dirty flag. ZIP
validation records the SHA-256 of the inner application ZIP, not the GitHub
artifact container digest. Clean checks use the same final AppImage bytes.

Linux Qt 6.8.3 installs GUI/CLI to an isolated Unicode prefix and runs installed
startup plus real GUI/CLI/Whisper. Windows validates the extracted final ZIP with
minimal PATH before invoking the candidate-only real harness against its CLI.
The harness uses SDK Qt; independent deployed startup and a clean Windows 11
manual test remain distinct requirements.

Retain QtTest/CTest logs, CMake configuration, prerequisites/validation JSON,
sanitized screenshot, manifest and package checksums. Do not upload models,
private media, credentials or full private transcripts. The real GUI fixture
[russian-speech.json](../../tests/fixtures/russian-speech.json) pins public OGG,
license/attribution and the 30–60 second interval. Verify model/input hashes
before use; record decoded WAV hashes per OS. ASR text need not match byte-for-byte
across CPUs. This is integration evidence, not WER measurement.

## Required boundaries

| Boundary | Evidence |
|---|---|
| Native paths/processes | Linux/Windows Unicode, locks, descendant termination, clean DLL lookup |
| Protocol/cancel/commit | Shared parser vectors, machine control, GUI streaming, partial-output and late-cancel regressions |
| Model/GUI lifetime | Range/hash/recovery, worker barriers/generations, teardown and retry |
| Real exports | Actual engine/model/input versions and hashes, preview, completed manifest, bounded TXT/SRT/VTT |
| Final ZIP/AppImage | Hash/inventory/source policy, clean startup and bundled-tool smoke |
| Linux installed prefix | Separate prefix/cwd, runtime environment, desktop entry/icon, isolated legacy upgrade |
| Native desktop | User OS, DPI, dialogs, clipboard, open exports and keyboard checks |
| Final CI | All required jobs/real steps on exact final candidate source |

Sample/export checks allow Whisper timestamp-token quantization (20 ms) and
clip a segment crossing the audio end with contextual warning. Entirely external
segments fail. Preserve these cases and relative/absolute history aliases.
Windows patches and native QPA/theme settings address verified Unicode/Qt
integration defects; see [Windows packaging](../build/windows.md).

Mark absent evidence **not checked**, rather than inferring it from workflow
presence. Large Windows processor groups and unusual cgroup/affinity setups have
not been established by these ordinary checks. Live URLs/cookies and desktop
interaction are independent of mock cache and offscreen tests.

## Acceptance scope

For v0.4.0 the maintainer waived Ubuntu desktop acceptance (clean containers
remain), cookies and a 10–15 minute recording. The user confirmed VK/cache and
five-minute recognition on Windows 11 24H2. These are dated release decisions,
not passes for the waived scenarios or a blanket future waiver. A new release
records its own manual scope, failures and unverified scenarios with exact hashes.
