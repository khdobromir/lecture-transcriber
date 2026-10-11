# Quick Start

[Documentation](../README.md) · [Русский](ru/quick-start.md) · [User guide](guide.md)

Transcribe recognizes Russian speech locally on CPU. The current application
interface is Russian; the labels below match the actual interface.

1. Download the application and checksum from [v0.4.0 Releases](https://github.com/khdobromir/lecture-transcriber/releases/tag/v0.4.0).
   Linux packages target x86_64/glibc 2.39+; Windows packages target Windows 11 x64.
   Models are separate downloads. Build-input/source archives are for developers.
2. Verify the application's SHA-256 against its `.sha256` file. Linux:
   `sha256sum Transcribe-0.4.0-linux-x86_64.AppImage`. Windows PowerShell:
   `Get-FileHash -Algorithm SHA256 -LiteralPath '.\Transcribe-0.4.0-windows-x64.zip'`.
3. Linux: make the AppImage executable and run it. If FUSE is unavailable, use
   the same file with `--appimage-extract-and-run` (needs temporary disk space):

   ```sh
   chmod +x Transcribe-0.4.0-linux-x86_64.AppImage
   ./Transcribe-0.4.0-linux-x86_64.AppImage
   # Alternative without FUSE:
   ./Transcribe-0.4.0-linux-x86_64.AppImage --appimage-extract-and-run
   ```

   Windows: extract the **entire** ZIP, preserve its directories and run
   `bin\transcribe-gui.exe`. No Qt SDK, Python, compiler or media-tool installation
   is required for either binary package.
4. On **Расшифровка**, choose `small` for an initial test. Open **Модели** and
   select **Скачать small и VAD**. Wait for successful verification. For an
   offline first run, import the exact model file and VAD from the
   [model manifest](../../scripts/models.tsv) using **Импорт small…** and
   **Импорт VAD…** instead. Choose the matching preset before importing.
5. Return to **Расшифровка**, choose a local speech recording and an output
   directory, then select **Начать расшифровку**. One chunk and automatic CPU
   settings are sufficient for the first test. Medium is the default preset
   on a fresh GUI and may take longer.
6. Wait for confirmed completion, then open **Каталог** or **Полный TXT**.
   Each run has `transcripts/transcript.txt`, `.srt`, `.vtt`, `logs/`, `source.txt`
   and `result.json`. Preview/100% alone does not confirm saved exports.

After model preparation, local-file recognition needs no network. URL download
and model download do. Use **Отменить** to stop a task and retain published
partial text. See [troubleshooting](../troubleshooting.md) for missing models,
download failures or incomplete packages. CLI installation/builds have a separate
[source guide](../../CONTRIBUTING.md) and [CLI reference](cli.md).
