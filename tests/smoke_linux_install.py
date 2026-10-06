"""Smoke an actual cmake install prefix from a different Unicode cwd."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

prefix = Path(sys.argv[1]).resolve()
cli, gui = prefix / "bin/transcribe", prefix / "bin/transcribe-gui"
for path in [cli, gui, prefix / "share/applications/transcribe-gui.desktop",
             prefix / "share/icons/hicolor/scalable/apps/transcribe-gui.svg", prefix / "share/transcribe/models.tsv"]:
    if not path.is_file():
        raise RuntimeError("Missing installed file: " + str(path))
with tempfile.TemporaryDirectory(prefix="transcribe-install-smoke-") as temporary:
    cwd = Path(temporary) / "Проверка 😀"
    cwd.mkdir()
    environment = dict(os.environ, TRANSCRIBE_HOME=str(cwd / "data"),
                       TRANSCRIBE_GUI_SETTINGS_FILE=str(cwd / "settings.ini"), QT_QPA_PLATFORM="offscreen",
                       QT_QPA_PLATFORMTHEME="generic", QT_QUICK_BACKEND="software")
    version = subprocess.check_output([str(cli), "--version"], cwd=cwd, env=environment, text=True).strip()
    for executable in [cli, gui]:
        dependencies = subprocess.check_output(["ldd", str(executable)], text=True)
        if "not found" in dependencies:
            raise RuntimeError("Missing installed runtime dependency: " + dependencies)
    child = subprocess.Popen([str(gui)], cwd=cwd, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
        else:
            raise RuntimeError("Installed GUI exited early: " + repr(child.communicate()))
    finally:
        child.terminate()
        _, diagnostics = child.communicate(timeout=10)
    for marker in [b"failed to load", b"is not installed", b"TypeError", b"ReferenceError", b"Binding loop"]:
        if marker in diagnostics:
            raise RuntimeError("Installed QML diagnostic: " + repr(diagnostics))
print(json.dumps({"prefix": str(prefix), "version": version, "different_unicode_cwd": True,
                  "desktop_entry_icon_manifest": True, "runtime_dependencies": True, "gui_startup": True,
                  "qt_sdk_environment": "explicit caller environment"}))
