# Developer guide

[Build prerequisites and presets](../build/from-source.md) · [Testing](../testing/README.md)
· [Architecture](../architecture/overview.md) · [Contributing](../../CONTRIBUTING.md)

Clone the repository and create a focused work branch from current `master`.
Use a clean build directory and ordinary user permissions. Do not test installers
against your real HOME/model directory. No user media or account access is needed
for the default deterministic test loop.

```sh
cmake --preset linux-cli-debug
cmake --build --preset linux-cli-debug --parallel 2
ctest --preset linux-cli-debug -L unit
ctest --preset linux-cli-debug
```

Use `linux-gui-debug` or a Windows preset for the frontend/native platform work.
GUI tests require loopback sockets for the local HTTP fixture. The development
binary does not provide an engine/models merely because CMake completed.

## Find the implementation

| Responsibility | Owned code | Useful evidence |
|---|---|---|
| CLI options and presentation | [cli.cpp](../../src/cli.cpp), [main.cpp](../../src/main.cpp) | [CLI unit tests](../../tests/test_unit_cli.cpp) |
| Pipeline, result commit, cancellation | [pipeline.cpp](../../src/pipeline.cpp), [cancellation.cpp](../../src/cancellation.cpp) | [pipeline cases](../../tests/test_pipeline.cpp), [machine harness](../../tests/test_machine.py) |
| Process/resource ownership | [process.hpp](../../src/process.hpp), [Linux supervisor](../../src/process_guard_linux.cpp), [Windows process](../../src/process_windows.cpp) | [portable platform](../../tests/test_portable.cpp), reliability pipeline cases |
| Media/cache/export validation | [media.cpp](../../src/media.cpp), [audio.cpp](../../src/audio.cpp), [result.cpp](../../src/result.cpp) | [audio/features units](../../tests/test_unit_audio.cpp), [sample bounds](../../tests/test_export_bounds.cpp), [real splits](../../tests/test_split.cpp) |
| Versioned frontend boundary | [protocol.cpp](../../src/protocol.cpp), [Backend](../../gui/backend.cpp) | [protocol cases](../../tests/test_protocol.cpp), [GUI/CLI integration](../../tests/test_gui_integration.cpp) |
| GUI workers, models/history | [models.cpp](../../gui/models.cpp), [history.cpp](../../gui/history.cpp), [workers.hpp](../../gui/workers.hpp) | [controller/model tests](../../tests/test_gui.cpp), [QML tests](../../tests/test_qml.cpp) |
| Distribution/provenance | [packagers](../../scripts), [platform inputs](../../packaging) | [package contracts](../../tests/test_package_source.py), final package smoke |

Core is Qt-free; GUI invokes the real CLI through QProcess and JSONL. Keep
platform code in the existing Linux/Windows adapters. Avoid splitting directories
or adding abstraction layers unless an actual ownership/testability problem
requires it. The source tree is intentionally small enough to navigate directly.

## Change and review

Preserve public CLI defaults, result format, protocol v1 and user paths unless
the task explicitly changes them. Add a regression at the lowest useful boundary;
process/lifetime defects often need an integration fixture, not a mocked method.
Keep independent POSIX process fixtures rather than using the production runner
to test itself. State platform exclusions and distinguish mocks from real ASR.

Compile with warnings-as-errors, run affected CTest suites, then the appropriate
broader platform/GUI checks. [Testing](../testing/README.md) explains static
analysis, sanitizers, QML lint and real/package checks. Record the exact
commit/configuration and any tests not run. Use Conventional Commits and small
reviewable changes; update user-facing docs/changelog for behavior changes.

## GUI translations and README languages

The main [README](../../README.md) is Russian; [README.en.md](../../README.en.md)
is its English counterpart. Update both when changing user-facing behavior.
Technical guides remain English, with short Russian user guides maintained in
parallel. Historical release records describe their original versions.

GUI source strings remain Russian. Qt LinguistTools extracts QML `qsTr`, C++
`tr` and `QT_TRANSLATE_NOOP` strings into the English TS catalog. After changing
GUI strings, run `cmake --build <gui-build> --target update_translations`, translate
new messages in `gui/i18n/transcribe_en.ts`, then rebuild normally. The normal
build compiles QM files and embeds them; it never updates tracked TS files.
The `gui_translations` CTest extracts strings into a temporary file and checks
coverage, finished translations and matching placeholders. No translator service
or runtime network access is needed.

Language selection is `ui/language=auto|ru|en` in the existing GUI settings file.
`LanguageController` owns application translators; `UiText` retains translatable
messages until display. Language changes retranslate the existing QML engine,
notify controller properties and update history status roles without rescanning
files or restarting workers. Backend diagnostics carry both variants in the
optional [JSONL extension](../architecture/protocol-v1.md#optional-bilingual-messages).
Do not infer translations by replacing substrings in paths, transcripts or
already-rendered messages. OS and external-tool details stay verbatim.
