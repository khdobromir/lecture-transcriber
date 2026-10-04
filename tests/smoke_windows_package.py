"""Check the deployed package with a Unicode path and the real bundled tools."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave

bin_dir = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="transcribe-package-smoke-") as temporary:
    root = Path(temporary) / "Проверка 😀"
    root.mkdir()
    package = root / "Transcribe"
    shutil.copytree(bin_dir.parent, package)
    copied = package / "bin"
    # Do not let the SDK or system tools mask missing DLLs in the package.
    environment = dict(os.environ, PATH=os.environ["SystemRoot"] + "\\System32")
    environment.pop("QT_PLUGIN_PATH", None)
    environment.pop("QML2_IMPORT_PATH", None)
    for command in [[copied / "transcribe.exe", "--version"],
                    [copied / "tools/whisper-cli.exe", "--help"],
                    [copied / "tools/yt-dlp.exe", "--version"]]:
        subprocess.run(list(map(str, command)), cwd=root, env=environment, check=True, timeout=30,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    audio = root / "Тест 😀.wav"
    with wave.open(str(audio), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(16000)
        out.writeframes(b"\0\0" * 1600)
    subprocess.run([str(copied / "tools/ffmpeg.exe"), "-nostdin", "-loglevel", "error", "-i", str(audio),
                    str(root / "Выход 😀.wav")], env=environment, check=True, timeout=30)
    settings = root / "settings.ini"
    settings.write_text("[General]\noutput=" + (root / "results").as_posix() + "\n", encoding="utf-8")
    environment.update(QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
                       TRANSCRIBE_HOME=str(root / "data"), TRANSCRIBE_GUI_SETTINGS_FILE=str(settings))
    child = subprocess.Popen([str(copied / "transcribe-gui.exe")], env=environment, cwd=root,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        else:
            raise RuntimeError(f"Deployed GUI exited early: {child.returncode}, {child.stderr.read()!r}")
    finally:
        child.terminate()
        _, diagnostics = child.communicate(timeout=10)
    for marker in [b"failed to load", b"is not installed", b"TypeError", b"ReferenceError", b"Binding loop"]:
        if marker in diagnostics:
            raise RuntimeError(f"Deployed QML error: {diagnostics!r}")
print("Package smoke passed: bundled tools, Unicode paths, deployed Qt/QML")
