"""Verify one diagnostic AppImage, its payload, startup and actual speech exports."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', type=Path, help='Exact .AppImage.json sidecar')
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--model-home', type=Path, required=True)
    parser.add_argument('--audio', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    record = json.loads(args.candidate.read_text())
    image = Path(record['filename'])
    assert digest(image) == record['sha256']
    work = args.artifacts.resolve() / 'Проверка 😀'
    work.mkdir(parents=True, exist_ok=False)
    moved = work / 'Transcribe.AppImage'
    import shutil
    shutil.copy2(image, moved)
    env = dict(os.environ)
    for key in ('LD_LIBRARY_PATH', 'LD_PRELOAD', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'CMAKE_PREFIX_PATH', 'APPDIR', 'APPIMAGE', 'APPIMAGE_EXTRACT_AND_RUN'):
        env.pop(key, None)
    env.update(TRANSCRIBE_HOME=str(work / 'data'), TRANSCRIBE_GUI_SETTINGS_FILE=str(work / 'settings.ini'),
               QT_QPA_PLATFORM='offscreen', QT_QPA_PLATFORMTHEME='generic', QT_QUICK_BACKEND='software', QT_IM_MODULE='compose')
    subprocess.run([str(moved), '--appimage-extract'], cwd=work, env=env, stdout=subprocess.DEVNULL, check=True, timeout=120)
    root = work / 'squashfs-root'
    manifest = json.loads((root / 'package-manifest.json').read_text())
    assert manifest['diagnostic'] and manifest['qt'] == '6.8.3'
    assert manifest['source_fingerprint'] == record['source_fingerprint']
    for entry in manifest['files']:
        path = root / entry['path']
        assert path.resolve().is_relative_to(root)
        assert digest(path) == entry['sha256'], entry['path']
    for link in root.rglob('*'):
        if link.is_symlink():
            assert link.resolve().is_relative_to(root), link
    version = subprocess.check_output([str(moved), '--appimage-extract-and-run', '--transcribe-cli', '--version'], env=env, cwd=work, text=True, timeout=60).strip()
    assert version == 'transcribe ' + record['application_version']
    startup = args.artifacts.resolve() / 'startup.log'
    with startup.open('w') as output:
        child = subprocess.Popen([str(moved), '--appimage-extract-and-run'], cwd=work, env=dict(env, LD_DEBUG='libs'), stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            time.sleep(6)
            assert child.poll() is None, 'Packaged GUI exited early'
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL); child.wait()
    diagnostics = startup.read_text()
    for message in ('failed to load component', 'TypeError', 'ReferenceError', 'Binding loop'):
        assert message not in diagnostics, message
    qt_loaded = re.findall(r'calling init: (.*libQt6[^\s]*)', diagnostics)
    assert qt_loaded and all('appimage_extracted_' in name for name in qt_loaded), qt_loaded
    tool_env = dict(env, LD_LIBRARY_PATH=str(root / 'usr/lib'), QT_PLUGIN_PATH=str(root / 'usr/plugins'), QML_IMPORT_PATH=str(root / 'usr/qml'))
    build = args.build.resolve()
    subprocess.run([str(build / 'gui/test_theme'), 'supportedDecoders', '-o', str(args.artifacts.resolve() / 'decoders.log') + ',txt'], env=tool_env, cwd=work, check=True, timeout=60)
    speech = args.artifacts.resolve() / 'real-speech'
    real_env = dict(tool_env, TRANSCRIBE_REAL_BINARY=str(root / 'usr/bin/transcribe'), TRANSCRIBE_REAL_AUDIO=str(args.audio.resolve()),
                    TRANSCRIBE_REAL_HOME=str(args.model_home.resolve()), TRANSCRIBE_REAL_MODEL='medium', TRANSCRIBE_REAL_ARTIFACTS=str(speech))
    subprocess.run([str(build / 'gui/test_gui_real'), 'installedCliWithRealSpeech', '-o', str(args.artifacts.resolve() / 'real-gui.log') + ',txt'], env=real_env, cwd=work, check=True, timeout=360)
    evidence = json.loads((speech / 'validation.json').read_text())
    assert evidence['completed'] and evidence['live_preview'] and all(name in evidence['exports'] for name in ('txt', 'srt', 'vtt'))
    summary = dict(candidate=str(image), sha256=record['sha256'], source_fingerprint=record['source_fingerprint'],
                   qt=manifest['qt'], verified_payload_files=len(manifest['files']), unicode_cwd=True, no_fuse_startup=True,
                   bundled_qt_only=True, qt_libraries=qt_loaded, packaged_decoders=True, real_speech=evidence,
                   windows='unverified')
    (args.artifacts / 'acceptance.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k not in ('real_speech', 'qt_libraries')}, indent=2))


if __name__ == '__main__':
    main()
