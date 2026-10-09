#!/bin/sh
# Run inside an ordinary minimal Ubuntu/Debian container, with only the final
# AppImage and optional public speech/model mounted. No build tree or Qt SDK.
set -eu
for tool in ffmpeg ffprobe yt-dlp whisper-cli python3 qmake qmake6; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf 'Unexpected installed application dependency: %s\n' "$tool" >&2
        exit 1
    fi
done
if dpkg-query -W 'libqt6core6*' 2>/dev/null; then
    printf 'Unexpected installed Qt runtime\n' >&2
    exit 1
fi
test -x /inputs/Transcribe.AppImage
(cd /inputs && sha256sum -c SHA256SUMS)
if [ "${REQUIRE_ASR:-0}" = 1 ]; then test -s /inputs/audio.wav && test -s /inputs/model.bin; fi
export TRANSCRIBE_HOME=/tmp/transcribe-data
export TRANSCRIBE_GUI_SETTINGS_FILE=/tmp/settings.ini
export QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic QT_QUICK_BACKEND=software
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH APPDIR APPIMAGE APPIMAGE_EXTRACT_AND_RUN
/inputs/Transcribe.AppImage --appimage-extract-and-run --transcribe-cli --version
gui_code=0
timeout --signal=TERM --kill-after=5s 10s /inputs/Transcribe.AppImage --appimage-extract-and-run > /tmp/gui.log 2>&1 || gui_code=$?
if [ "$gui_code" -ne 124 ]; then cat /tmp/gui.log; exit 1; fi
if grep -E 'failed to load|is not installed|TypeError|ReferenceError|Binding loop' /tmp/gui.log; then exit 1; fi
if [ -f /inputs/audio.wav ]; then
    /inputs/Transcribe.AppImage --appimage-extract-and-run --transcribe-cli \
        --model /inputs/model.bin --no-vad --threads 2 --out /tmp/results -- /inputs/audio.wav > /tmp/cli.log 2>&1
    set -- /tmp/results/*
    test "$#" -eq 1 && test -d "$1"
    for extension in txt srt vtt; do test -s "$1/transcripts/transcript.$extension"; done
    grep '"status": "completed"' "$1/result.json" >/dev/null
fi
if [ -d /evidence ]; then
    cp /tmp/gui.log /evidence/gui.log
    cp /inputs/SHA256SUMS /evidence/SHA256SUMS
    if [ -f /tmp/cli.log ]; then
        cp /tmp/cli.log /evidence/cli.log
        cp -r /tmp/results /evidence/results
    fi
fi
printf 'Clean Linux package smoke passed (no Qt/Python/FFmpeg/yt-dlp installation).\n'
