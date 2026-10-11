# Contributing

Read the [README](README.md), [developer guide](docs/developer/getting-started.md)
and relevant existing issues first. English and Russian contributions/reports are
welcome. This is a small native CPU-first application; keep changes focused and
avoid new frameworks or user features without discussing their purpose.

For bugs include version, OS/architecture, installation route, model/settings,
minimal reproduction and a redacted relevant error/log excerpt. Never attach
cookies, tokens, private URLs/media, complete transcripts or model weights.
Report suspected vulnerabilities through the [private security route](SECURITY.md).

## Development

Branch from current `master`; do not modify it directly. See
[prerequisites/build presets](docs/build/from-source.md) for Linux and Windows:

```sh
cmake --preset linux-cli-debug
cmake --build --preset linux-cli-debug --parallel 2
ctest --preset linux-cli-debug
```

GUI changes use a GUI preset, QML lint and appropriate controller/integration
checks. [Testing](docs/testing/README.md) explains units, native processes,
static analysis, sanitizers, real ASR and final package checks. Run installers only
with isolated temporary data/bin paths, without sudo. Ordinary CTest needs no
models or account credentials. Report tests not run and platform limitations.

## Formatting

Use clang-format **18.1.8**, pinned with Linux x86_64/Windows x64 wheel hashes.
Install it into a task-local virtual environment, not the application runtime:

```sh
python3 -m venv /tmp/transcribe-format-tool
/tmp/transcribe-format-tool/bin/python -m pip install --no-cache-dir --only-binary=:all: --no-deps --require-hashes -r scripts/format-requirements.txt
CLANG_FORMAT=/tmp/transcribe-format-tool/bin/clang-format python3 scripts/check-format.py --base origin/master
```

On Windows use `py -m venv out/format-tool` and its `Scripts/python.exe` for pip;
pass `--clang-format out/format-tool/Scripts/clang-format.exe` to the checker.
Add `--fix` to apply the selected ranges, review the diff, then rerun the check.
Only changed lines in directly owned `src`, `gui` and `tests` C++ files are
considered; vendor/generated/nested trees and unchanged legacy formatting are
excluded. Stage new C++ files before checking so Git includes them. With no
explicit base the script uses the merge-base with `origin/master`.
Keep style-only edits separate from logic and avoid full-tree formatting.

## Pull requests

- Use Conventional Commits for every new commit, including merges:
  `<type>[optional scope][!]: <description>`, e.g. `fix(process): preserve partial output on cancellation`.
- Describe the concrete problem, resulting behavior and actual verification.
  Add a reproducing regression for a bug at the appropriate boundary.
- Preserve CLI defaults, result layout, user paths and protocol v1 unless the
  change explicitly addresses compatibility and documentation.
- Keep source/build outputs separate; do not commit models, media, local caches,
  binaries or private results. Preserve original third-party notices/pins.
- Keep formatting separate from logic; avoid mass reformatting unrelated code.
- Version comes from CMake `project(... VERSION ...)`; record user-visible,
  dependency and version changes in CHANGELOG.

[Release preparation](docs/build/releasing.md) has separate exact-SHA, manual and
artifact/source evidence. A successful PR/build is not authorization to merge,
move a tag or publish assets.
