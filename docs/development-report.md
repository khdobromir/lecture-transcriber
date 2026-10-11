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

## Verification boundaries

No merge, tag movement, release publication or replacement of existing release
assets is authorized. Earlier master CI is baseline evidence, not verification
of this branch. Fresh implementation checks are recorded as stages complete.

Windows icon resource compilation and final AppImage/ZIP builds require their
native packaging environments; local PNG/ICO validation and Linux install smoke
do not establish those checks.
