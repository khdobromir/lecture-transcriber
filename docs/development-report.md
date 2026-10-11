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

## Push checkpoint (2026-10-11)

The completed stages were pushed to `origin/chore/modernization-roadmap` at the
user's request. Remaining roadmap tasks are not implemented at this checkpoint.

The stricter downloader fixture initially masked the diagnostic expected by its
truncated-report regression. The follow-up fix validates report argument bounds
before isolation flags and before file operations; downloader isolation checks
remain enforced for valid calls. The failure was reproduced before the fix.

After rebuilding `mock_portable`, all 23 registered Linux CTest suites passed:
three affected integration suites and the remaining 20 suites in a separate run.
The latter ran outside the sandbox so the GUI controller's local HTTP server
could bind. These results cover the current Linux Debug GUI build; they do not
establish native Windows or final binary-package validation.

## Verification boundaries

No merge, tag movement, release publication or replacement of existing release
assets is authorized. Earlier master CI is baseline evidence, not verification
of this branch. Fresh implementation checks are recorded as stages complete.

Windows icon resource compilation and final AppImage/ZIP builds require their
native packaging environments; local PNG/ICO validation and Linux install smoke
do not establish those checks.
The Windows units are registered but have not yet executed on native MSVC here;
POSIX permission assertions and two symlink cases remain Linux-only. Existing
native private-directory checks remain in portable_platform.
