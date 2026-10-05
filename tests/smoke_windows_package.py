"""Check the deployed package with a Unicode path and the real bundled tools."""
import argparse
import os
from pathlib import Path
import json
import ctypes
import xml.etree.ElementTree as ET
from package_archive import extract_verified, digest
import subprocess
import sys
import tempfile
import wave

def verify_executable_manifest(executable):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.SizeofResource.restype = ctypes.c_uint32
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(executable), None, 0x2 | 0x20)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        resource = kernel.FindResourceW(module, 1, 24)
        if not resource:
            raise RuntimeError("Missing executable manifest: " + str(executable))
        size = kernel.SizeofResource(module, resource)
        pointer = kernel.LockResource(kernel.LoadResource(module, resource))
        if not pointer or not size:
            raise RuntimeError("Unreadable executable manifest")
        manifest = ET.fromstring(ctypes.string_at(pointer, size))
        level = manifest.find(".//{*}requestedExecutionLevel")
        if level is None or level.get("level") != "asInvoker":
            raise RuntimeError("Package requests elevation")
        if "PerMonitorV2" not in (manifest.findtext(".//{*}dpiAwareness") or ""):
            raise RuntimeError("Missing PerMonitorV2 manifest")
        if (manifest.findtext(".//{*}longPathAware") or "").lower() != "true":
            raise RuntimeError("Missing long-path manifest")
    finally:
        kernel.FreeLibrary(module)


parser = argparse.ArgumentParser()
parser.add_argument("archive", type=Path)
parser.add_argument("source")
parser.add_argument("--real-gui", type=Path)
parser.add_argument("--workspace", type=Path)
args = parser.parse_args()
archive = args.archive.resolve()
expected_source = args.source
with tempfile.TemporaryDirectory(prefix="transcribe-package-smoke-") as temporary:
    root = Path(temporary) / "Проверка 😀"
    root.mkdir()
    package, manifest = extract_verified(archive, root, expected_source)
    copied = package / "bin"
    for name in ["transcribe.exe", "transcribe-gui.exe"]:
        verify_executable_manifest(copied / name)
    # Do not let the SDK or system tools mask missing DLLs in the package.
    environment = dict(os.environ, PATH=os.environ["SystemRoot"] + "\\System32")
    for key in ["QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH",
                "QT_QML_IMPORT_PATH", "QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE", "QT_QUICK_CONTROLS_STYLE",
                "QTDIR", "QT_ROOT", "Qt6_DIR", "CMAKE_PREFIX_PATH"]:
        environment.pop(key, None)
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
    # No model download is needed to prove that Whisper accepts Unicode input:
    # the missing model must fail initialization (3), not discard the input (2).
    preflight = subprocess.run([str(copied / "tools/whisper-cli.exe"), "--no-gpu", "--file", str(audio),
                                "--model", str(root / "missing-model.bin")], cwd=root, env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if preflight.returncode != 3 or b"failed to initialize whisper context" not in preflight.stderr:
        raise RuntimeError(f"Whisper Unicode input preflight failed: {preflight.returncode}, {preflight.stderr!r}")
    settings = root / "settings.ini"
    settings.write_text("[General]\noutput=" + (root / "results").as_posix() + "\n", encoding="utf-8")
    environment.update(QT_QPA_PLATFORM="windows", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
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
    if args.real_gui:
        if not args.workspace:
            raise RuntimeError("Real smoke requires an isolated workspace")
        preparer = Path(__file__).with_name("prepare_real_smoke.py")
        subprocess.run([sys.executable, str(preparer), str(args.workspace), "--ffmpeg", str(copied / "tools/ffmpeg.exe"),
                        "--engine", str(copied / "tools/whisper-cli.exe")], check=True, timeout=600)
        prerequisites = json.loads((args.workspace / "prerequisites.json").read_text(encoding="utf-8"))
        real_environment = dict(os.environ, **prerequisites["environment"], QT_QPA_PLATFORM="windows",
                                QT_QPA_PLATFORMTHEME="generic", QT_QUICK_BACKEND="software",
                                TRANSCRIBE_REAL_BINARY=str(copied / "transcribe.exe"),
                                TRANSCRIBE_REAL_ARTIFACTS=str(args.workspace / "result"))
        # The test harness uses the SDK; the deployed executables were checked
        # independently above with a minimal PATH. No SDK is copied into the ZIP.
        subprocess.run([str(args.real_gui), "-o", str(args.workspace.resolve() / "gui-real.log") + ",txt"],
                       env=real_environment, cwd=root, check=True, timeout=360)
evidence = {"source": expected_source, "dirty": manifest["dirty"], "zip_sha256": digest(archive), "verified_files": len(manifest["files"]),
            "checks": ["final ZIP extraction and every file SHA-256", "app-local CRT in bin and tools", "embedded manifests, DPI and asInvoker", "minimal PATH",
                       "bundled tools and Whisper Unicode input preflight", "Unicode paths and different cwd", "deployed native Windows Qt/QML startup"],
            "real_gui_cli_speech": bool(args.real_gui), "manual_clean_windows_11": "unverified"}
archive.with_suffix(archive.suffix + ".validation.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print("Final ZIP smoke passed: file checksums, CRT, bundled tools, Unicode paths, deployed Qt/QML")
