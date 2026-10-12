# User guide

[Quick Start](quick-start.md) · [CLI](cli.md) · [Русский](ru/guide.md)

## Inputs and models

Supply one local audio/video file or one plain HTTP(S) URL. Conversion uses
FFmpeg; URLs use yt-dlp and depend on the site's current extractor/access rules.
Only use material you are authorized to process. Live streams are rejected.
Current-source GUI builds support **Автоматически / Automatic**, **Русский**
and **English** in the top language selector. Automatic chooses Russian for a
Russian system and English otherwise; explicit selection is saved. Switching is
immediate and preserves the current task, inputs, downloads and results. Native
OS dialogs may use the system language. Published v0.4.1 has a Russian-only GUI.

The recognition language is Russian. `--prompt`/the terms field supplies
context to the model; it does not guarantee correct terminology.

The presets map to quantized models: `small` → `small-q5_1`, `medium` →
`medium-q5_0`, `turbo` → `large-v3-turbo-q5_0`. The GUI defaults to medium unless
a valid prior selection exists. Speed, memory and accuracy depend on the CPU
and input; small is a useful first test, not an accuracy guarantee.

Select a preset on **Transcription / Расшифровка**, then download it with VAD on **Models / Модели**.
Download and import verify the exact SHA-256 in [models.tsv](../../scripts/models.tsv).
Import recognition and VAD files separately, matching the selected preset.
An arbitrary GGML file cannot be imported as a known preset. Advanced GUI/CLI
also accept a custom model path; the preset manager's hash guarantee does not
extend to arbitrary custom paths. VAD is enabled by default and needs its own
model; disable it explicitly if unavailable.

Downloads can reuse valid partial files through HTTP Range. An installed corrupt
model is preserved; the explicit backup-and-recover action copies it before
replacement. A wrong imported file does not replace the installed model.

## Tasks, results and history

The GUI runs one task at a time. Input/model controls lock while a task or model
operation is active. Advanced settings expose threads, chunks, workers, VAD,
terms, cookies, cache and working-audio retention. GUI thread/worker value `0`
means automatic; CLI explicit `--threads`/`--jobs` require positive integers.

Live TXT grows in recording order, even with parallel chunks. The GUI shows up
to the last 512 KiB; **Full TXT / Полный TXT** opens the complete file on disk. Subtitles
are finalized after successful recognition/merge. The result is complete only
after the backend, manifest and all three exports agree. A progress bar at 100%
can still be followed by export/validation failure.

Use **Cancel / Отменить** or CLI Ctrl+C. Cancellation before completion preserves partial
text and working files when available; it does not resume ASR at the previous
position. Retry starts a fresh task with the same in-memory settings. Late cancel
cannot undo a saved success. Forced termination, OS shutdown and Windows console
close do not guarantee final file publication.

History scans the selected output roots in the background. Selecting an older
result does not change the current task; **Show current task result / Показать результат текущей задачи**
returns to it. Removed results appear unavailable. History also understands older
`source.txt` results; it does not duplicate the transcript in settings.
Keyboard shortcuts: Ctrl+O opens input, Ctrl+Enter starts, Ctrl+Q closes, F5
refreshes history. Native export opening needs an associated OS application.

## Data and upgrades

| Data | Linux | Windows |
|---|---|---|
| Models/default-model | `~/.local/share/transcribe` | `%LOCALAPPDATA%\Transcribe` |
| Results, default parent | `~/Transcriptions` | `%USERPROFILE%\Transcriptions` |
| Downloaded-media cache | `$XDG_CACHE_HOME/transcribe/media` if absolute, otherwise `~/.cache/transcribe/media` | `%LOCALAPPDATA%\Transcribe\cache\media` |
| GUI settings/history paths | Qt user AppConfigLocation, `settings.ini` | Qt user AppConfigLocation, `settings.ini` |

`TRANSCRIBE_HOME` overrides model/application data on both platforms. It does
not override Linux cache or result locations. `TRANSCRIBE_GUI_SETTINGS_FILE`
selects a separate settings INI for isolated tests; output and cache have their
own controls. Set overrides before starting the GUI/CLI.

Replace the complete ZIP/AppImage to upgrade the application. User data lives
outside it and is retained. Keep the bundled CLI/tools together: incomplete
packages deliberately fail instead of using tools from PATH. Do not copy new
executables into an older package. Linux `install.sh` is a separate CLI source
installation route, not an AppImage updater.

On Windows use short parent paths: input paths over 240 UTF-16 units and result
parents over 169 units are rejected to leave room for external-tool suffixes.
Long-path manifests do not remove these deliberate integration limits.

## URL cache and authentication

Downloaded media is reused by default, with a 10 GiB managed-cache limit.
**Download again / Скачать заново** / `--refresh-cache` bypasses a hit and refreshes it;
`--no-cache` disables reads/writes. The cache is local media, not a recognition
resume store. Source files are never deleted by working-file cleanup.

For authorized restricted sources, use either a Netscape cookies file or a
yt-dlp browser/profile specification (`firefox`, `chromium:PROFILE`). The two
authentication options are mutually exclusive. Cookies and prompts are not
saved in GUI settings; retry retains task parameters in memory. Tool logs and
source metadata can still contain private URLs or paths: redact before sharing.

Downloader calls ignore ambient config/cache directories and disable playlists.
Version 0.4.1 also disables plugin directories; published v0.4.0 is unchanged.
Custom extractor plugins are unsupported. Source installation needs
a yt-dlp supporting `--no-plugin-dirs` (bundled version: 2026.08.19). An extractor
failure is not proof of an ASR failure. See [troubleshooting](../troubleshooting.md).
