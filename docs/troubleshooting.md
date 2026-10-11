# Troubleshooting

[Quick Start](user/quick-start.md) · [User guide](user/guide.md)

| Symptom | Check / recovery |
|---|---|
| Linux AppImage cannot mount | Run the same file with `--appimage-extract-and-run`; check executable permission and free temporary disk space. This is the explicit no-FUSE route. |
| Old glibc / wrong architecture | Package target is Linux x86_64/glibc 2.39+; use a supported system or a source build for your supported platform. musl/ARM are not package targets. |
| Missing DLL/tool or incomplete bundle | Re-verify checksum and extract/replace the entire original package. Keep `bin/tools` and the marker intact; do not add arbitrary DLLs or rely on PATH fallback. |
| Model/default-model or VAD missing | Select the desired preset, download/import it and VAD in GUI. CLI source installs use `install.sh` and `scripts/download-model.sh`; custom model paths need their own valid file. Disable VAD only deliberately. |
| Model SHA-256 mismatch | Check the preset and exact filename/hash in `scripts/models.tsv`. A wrong imported model is rejected. Use the explicit backup-and-recover action for corrupt installed models; preserve your original copy. |
| Download TLS/network failure | Check connectivity, date/time, proxy and the actual certificate error. Do not disable TLS verification. Model operations support retry and validated partial downloads. |
| URL unavailable / login required | Confirm an authorized plain URL, supported extractor and the chosen cookies method. Supply only one cookies option. Logs may contain private URLs; redact them. Local-file transcription isolates ASR from download problems. |
| yt-dlp rejects `--no-plugin-dirs` | Source installation tool is too old/incompatible. Use a compatible release (bundled pin is 2026.08.19). Do not silently remove isolation. Custom extractor plugins are unsupported. |
| Slow processing / memory pressure | Start with small, one chunk and automatic threads. More workers load more models. Check available RAM/disk; speed/ETA is hardware and input dependent. |
| 100% but no successful result | Read task error and per-tool logs. Recognition progress precedes export/manifest verification. Retain partial output and retry after correcting the cause. |
| Cancelled result looks incomplete | Expected: TXT/work may be partial; final subtitle publication requires success. Retry starts again. Forced process termination is less predictable than Cancel/Ctrl+C. |
| Windows path-length error | Use a shorter input/output parent; explicit limits reserve space for external-tool paths. |
| Old CLI after an update | Linux: `type -a transcribe`, then `transcribe --version`. Releases do not replace an unrelated `~/.local/bin` installation automatically. |
| Export will not open in another app | Verify the file exists/readable, then check the OS file association. TXT/SRT/VTT are plain text. |
| Tests cannot bind localhost | GUI model tests use a local HTTP server. Run them in a development environment allowing loopback sockets; a sandbox denial is not a download implementation defect. |

For a bug report include version, OS/architecture, installation route, exact
reproduction, expected/actual behavior and a **redacted** relevant error/log
excerpt. Record whether input/model download or local recognition failed. Do not
attach model weights, private recordings, complete transcripts, tokens or cookies.
