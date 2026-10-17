"""Package a local Material You preview using a verified release's unchanged tools.

This is a diagnostic Omarchy build, not the pinned release builder. No installed
file is modified and no platform compatibility claim is inherited from the base.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--base-sha256', required=True)
    parser.add_argument('--qt-root', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--appimagetool', required=True, type=Path)
    parser.add_argument('--destination', type=Path, default=ROOT / 'dist/material-you')
    args = parser.parse_args()
    base, sdk, build, tool = [p.resolve() for p in (args.base, args.qt_root, args.build, args.appimagetool)]
    if digest(base) != args.base_sha256:
        parser.error('Base AppImage checksum differs from the selected release')
    lock = json.loads((ROOT / 'packaging/linux/dependencies.json').read_text())
    if digest(tool) != lock['downloads']['appimagetool']['sha256']:
        parser.error('appimagetool checksum differs from the project lock')
    version = subprocess.check_output([str(sdk / 'bin/qmake'), '-query', 'QT_VERSION'], text=True).strip()
    if version != '6.8.3':
        parser.error('The preview requires Qt 6.8.3')
    dynamic = subprocess.check_output(['readelf', '-d', str(build / 'transcribe-gui')], text=True)
    if '(RPATH)' in dynamic or '(RUNPATH)' in dynamic:
        parser.error('Build the preview with -DCMAKE_SKIP_RPATH=ON')
    args.destination.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix='preview-', dir=args.destination)).resolve()
    print('Preview stage:', stage, flush=True)
    subprocess.run([str(base), '--appimage-extract'], cwd=stage, stdout=subprocess.DEVNULL, check=True)
    app = stage / 'squashfs-root'
    libraries = app / 'usr/lib'
    for name in ('transcribe', 'transcribe-gui'):
        shutil.copy2(build / name, app / 'usr/bin' / name)
    for library in sdk.glob('lib/libQt6*.so*'):
        target = libraries / library.name
        target.unlink(missing_ok=True)
        shutil.copy2(library, target, follow_symlinks=False)
    shutil.copytree(sdk / 'qml', app / 'usr/qml', dirs_exist_ok=True, symlinks=True)
    shutil.copytree(sdk / 'plugins/imageformats', app / 'usr/plugins/imageformats', dirs_exist_ok=True, symlinks=True)
    # The newly compiled application uses the host compiler runtime. Its actual
    # glibc requirement is recorded below rather than borrowing the base claim.
    for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
        target = libraries / name
        target.unlink(missing_ok=True)
        shutil.copy2(Path('/usr/lib') / name, target)
    env = dict(os.environ, LD_LIBRARY_PATH=str(libraries), ARCH='x86_64', APPIMAGE_EXTRACT_AND_RUN='1')
    excluded = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2', 'librt.so.1', 'libresolv.so.2', 'libutil.so.1'}
    for _ in range(10):
        added = 0
        for path in list((app / 'usr').rglob('*')):
            if not path.is_file() or path.is_symlink():
                continue
            with path.open('rb') as stream:
                if stream.read(4) != b'\x7fELF':
                    continue
            output = subprocess.run(['ldd', str(path)], env=env, text=True, capture_output=True)
            if 'not found' in output.stdout or 'version `' in output.stderr:
                raise RuntimeError(str(path) + '\n' + output.stdout + output.stderr)
            for name, origin in re.findall(r'^\s*(\S+) => (/\S+) \(', output.stdout, re.MULTILINE):
                if name in excluded or name.startswith('libnss_') or name.startswith('ld-linux'):
                    continue
                target = libraries / name
                if not target.exists():
                    shutil.copy2(Path(origin).resolve(), target)
                    added += 1
        if not added:
            break
    else:
        raise RuntimeError('Runtime dependency closure did not converge')
    license_dir = app / 'usr/share/licenses/transcribe'
    license_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / 'third_party/material-color-utilities/LICENSE', license_dir / 'Material-Color-Utilities-Apache-2.0.txt')
    source_files = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT).split(b'\0')
    fingerprint = hashlib.sha256()
    for filename in sorted(f for f in source_files if f):
        path = ROOT / os.fsdecode(filename)
        if path.is_file():
            fingerprint.update(filename + b'\0' + path.read_bytes())
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    glibc = set()
    for path in (app / 'usr').rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) != b'\x7fELF':
                    continue
            info = subprocess.run(['readelf', '--version-info', str(path)], text=True, capture_output=True)
            glibc.update(re.findall(r'GLIBC_(\d+\.\d+)', info.stdout))
    report = dict(schema=1, diagnostic=True, application_version='0.4.1', qt=version,
                  source=source, source_fingerprint=fingerprint.hexdigest(),
                  base_sha256=args.base_sha256, base_tools_reused=True,
                  glibc_required=max(glibc, key=lambda v: tuple(map(int, v.split('.')))),
                  tested_platform='Omarchy x86_64; acceptance recorded separately',
                  windows='unverified', material_color_utilities=json.loads((ROOT / 'third_party/material-color-utilities/UPSTREAM.json').read_text()))
    report['files'] = [dict(path=str(path.relative_to(app)), sha256=digest(path))
                       for path in sorted(app.rglob('*'))
                       if path.is_file() and not path.is_symlink() and path.name != 'package-manifest.json']
    (app / 'package-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    offset = int(subprocess.check_output([str(base), '--appimage-offset'], text=True).strip())
    runtime = stage / 'runtime'
    with base.open('rb') as stream:
        runtime.write_bytes(stream.read(offset))
    target = args.destination.resolve() / ('Transcribe-material-you-preview-' + fingerprint.hexdigest()[:12] + '-x86_64.AppImage')
    if target.exists():
        raise FileExistsError(target)
    subprocess.run([str(tool), '--appimage-extract-and-run', '--no-appstream', '--runtime-file', str(runtime), str(app), str(target)], env=env, check=True)
    report['inventory_files'] = len(report.pop('files'))
    report.update(filename=str(target), sha256=digest(target))
    target.with_suffix('.AppImage.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
