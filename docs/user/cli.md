# CLI reference

[User guide](guide.md) · [Source installation](../build/from-source.md)

```sh
transcribe [OPTIONS] "FILE_OR_URL"
transcribe --model small "lecture.wav"
transcribe --chunks 10 --jobs 2 --threads 3 --out "results" "lecture.mp4"
transcribe --model medium --prompt "Tensor, covariant, basis" "lecture.wav"
transcribe --no-progress "https://example.org/your-authorized-video"
transcribe -- --recording.wav
```

The URL is a placeholder, not a supported-site promise. Examples require real
tools/models and a readable input. In a Windows ZIP use
`.\bin\transcribe.exe --model small "C:\Recordings\lecture.wav"` from the
unpacked root. The Linux source installer places `transcribe` in `~/.local/bin`;
the AppImage normally launches the GUI.

| Option | Meaning / default |
|---|---|
| `--model small\|medium\|turbo\|PATH.bin` | Explicit preset/custom file; otherwise reads `TRANSCRIBE_HOME/default-model` (default data home if unset) |
| `--threads N` | Per-Whisper threads, 1–256; default `max(1, physical CPUs / jobs)` |
| `--chunks N` | Requested parts, 1–256; default 1 |
| `--jobs N` | Concurrent parts, 1–256, no greater than chunks; default `min(2, chunks, physical CPUs)` |
| `--out DIR` | Parent for a new result directory; default user-home `Transcriptions` |
| `--cache-dir DIR` | Override media-cache directory; platform defaults in [guide](guide.md#data-and-upgrades) |
| `--cache-limit-gib N` | Positive whole GiB, overflow checked; default 10 |
| `--no-cache` | Disable media-cache reads/writes |
| `--refresh-cache` | Download again and update cache; incompatible with `--no-cache` |
| `--cookies FILE` | Netscape-format cookies file; mutually exclusive with browser option |
| `--cookies-from-browser SPEC` | yt-dlp specification, e.g. `firefox` or `chromium:PROFILE` |
| `--prompt TEXT` | Short recognition context / terms, empty by default |
| `--no-vad` | Disable default speech detection; otherwise Silero VAD file is required |
| `--keep-audio` | Preserve working media after success; failure/interruption already preserves work |
| `--no-progress` | Suppress intermediate terminal progress, not stage/error output |
| `--machine` | UTF-8 JSONL events on stdout, cancellation commands on stdin |
| `--help` | Print current Russian help, exit 0 |
| `--version` | Print version, exit 0 |
| `--` | End options; subsequent tokens are positional |

Physical CPU detection accounts for available CPUs; values used for defaults
are clamped to 1–256. More workers each load a model and consume extra memory.
Explicit oversubscription warns but does not silently change requested values.
One input is accepted per process; Markdown `[text](URL)` is rejected.

Each run has ordered live TXT, final SRT/VTT, per-tool logs, `source.txt` and a
version-1 `result.json`. The original local input remains untouched. The normal
CLI reports stages/result locations rather than printing recognized speech.
No arguments prints usage and exits 2; successful processing exits 0. Task
errors generally exit 1; cancellation uses signal-style codes (explicit machine
cancel: 130; lost machine control channel before commit: 141). Do not infer a
successful saved result from progress alone.

`--machine` is an integration interface, not ordinary terminal mode: keep stdin
open and send `{"type":"cancel"}` followed by a newline to cancel. EOF means
lost control. stdout contains events, stderr diagnostics. Help/version retain
normal text output. Full fields, limits, error/commit semantics and independent
contract tests are in [protocol v1](../architecture/protocol-v1.md).

Sources of truth: [help](../../src/main.cpp), [parser/defaults](../../src/cli.cpp),
[options](../../src/cli.hpp), [platform data](../../src/platform.cpp),
[cache](../../src/media.cpp), [CLI unit cases](../../tests/test_unit_cli.cpp).
