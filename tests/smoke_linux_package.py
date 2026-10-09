"""Exercise the final AppImage, with all Qt SDK environment overrides removed."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
from linux_package import digest, verify


def clean_environment():
    env = dict(os.environ)
    for name in ["LD_LIBRARY_PATH", "LD_PRELOAD", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
                 "CMAKE_PREFIX_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "APPDIR", "APPIMAGE", "APPIMAGE_EXTRACT_AND_RUN"]:
        env.pop(name, None)
    return env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--real-gui", type=Path)
    parser.add_argument("--prerequisites", type=Path)
    parser.add_argument("--artifacts", type=Path, help="New directory for persistent smoke evidence")
    args = parser.parse_args()
    image = args.image.resolve()
    artifacts = args.artifacts.resolve() if args.artifacts else None
    if artifacts:
        artifacts.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="transcribe-appimage-") as temporary:
        work = Path(temporary) / "Проверка 😀"; work.mkdir()
        # Actually copy the one final artifact. No build/source/installed tool paths.
        moved = work / "Transcribe.AppImage"; shutil.copy2(image, moved)
        env = clean_environment()
        env.update(TRANSCRIBE_HOME=str(work / "data"), TRANSCRIBE_GUI_SETTINGS_FILE=str(work / "settings.ini"),
                   QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="generic", QT_QUICK_BACKEND="software")
        with (work / "extract.log").open("wb") as log:
            subprocess.run([str(moved), "--appimage-extract"], cwd=work, env=env, stdout=log, stderr=log, check=True, timeout=180)
        root = work / "squashfs-root"; manifest = verify(root, args.source)
        version = subprocess.check_output([str(moved), "--appimage-extract-and-run", "--transcribe-cli", "--version"],
                                          cwd=work, env=env, text=True, timeout=120).strip()
        if version != "transcribe " + manifest["application_version"]:
            raise RuntimeError("CLI version differs from package manifest")
        for name, option in [("whisper-cli", "--help"), ("ffmpeg", "-version"), ("ffprobe", "-version"), ("yt-dlp", "--version")]:
            tool_env = dict(env, LD_LIBRARY_PATH=str(root / "usr/lib"))
            subprocess.run([str(root / "usr/bin/tools" / name), option], cwd=work, env=tool_env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True, timeout=120)
        child = subprocess.Popen([str(moved), "--appimage-extract-and-run"], cwd=work, env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
            else:
                raise RuntimeError("Portable GUI exited early: " + repr(child.communicate()))
        finally:
            # The runtime waits for a second process: stop the whole idle GUI
            # session before removing the extraction directory and libraries.
            try:
                os.killpg(child.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            _, diagnostics = child.communicate(timeout=20)
        for marker in [b"failed to load", b"is not installed", b"TypeError", b"ReferenceError", b"Binding loop"]:
            if marker in diagnostics:
                raise RuntimeError("Portable QML diagnostic: " + repr(diagnostics))
        if artifacts:
            (artifacts / "startup.log").write_bytes(diagnostics)
        if args.real_gui:
            if not args.prerequisites:
                parser.error("--real-gui requires --prerequisites")
            prerequisites = json.loads((args.prerequisites / "prerequisites.json").read_text())
            real_env = dict(env, **prerequisites["environment"], TRANSCRIBE_REAL_BINARY=str(root / "usr/bin/transcribe"),
                TRANSCRIBE_REAL_ARTIFACTS=str(work / "real-result"), LD_LIBRARY_PATH=str(root / "usr/lib"),
                QT_PLUGIN_PATH=str(root / "usr/plugins"), QML_IMPORT_PATH=str(root / "usr/qml"),
                TRANSCRIBE_CA_BUNDLE=str(root / "usr/share/transcribe/ca-certificates.crt"))
            result = subprocess.run([str(args.real_gui.resolve()), "-o", str(work / "real-gui.log") + ",txt"],
                                    cwd=work, env=real_env, check=False, timeout=360)
            if artifacts and (work / "real-gui.log").is_file():
                shutil.copy2(work / "real-gui.log", artifacts / "real-gui.log")
            if result.returncode:
                raise RuntimeError("Real GUI smoke failed:\n" + (work / "real-gui.log").read_text())
            evidence = json.loads((work / "real-result/validation.json").read_text())
            if not evidence["completed"] or not evidence["live_preview"]:
                raise RuntimeError("Real portable GUI smoke did not confirm completion/preview")
            if artifacts:
                for name in ["validation.json", "gui.png"]:
                    shutil.copy2(work / "real-result" / name, artifacts / name)
        summary = {"source": manifest["source"], "image_sha256": digest(image), "version": version,
            "qt": manifest["qt"], "different_unicode_cwd": True, "no_fuse_startup": True,
            "gui_startup": True, "all_bundled_tools": True, "real_gui": bool(args.real_gui)}
        if artifacts:
            (artifacts / "package-smoke.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(json.dumps(summary))


if __name__ == "__main__":
    main()
