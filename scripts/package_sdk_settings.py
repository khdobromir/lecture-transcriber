"""Retain actual Qt SDK configuration and feature headers as build materials."""
import argparse
import io
import json
from pathlib import Path
import re
import tarfile

from package_source import digest


def retain_settings(root, version, destination):
    root = root.resolve(strict=True)
    required = root / "mkspecs/qconfig.pri"
    if required.is_symlink() or not required.is_file():
        raise ValueError("Qt SDK lacks original qconfig.pri")
    text = required.read_text(encoding="utf-8")
    if re.findall(r"^QT_VERSION\s*=\s*([^\s]+)\s*$", text, re.MULTILINE) != [version]:
        raise ValueError("Qt SDK settings version mismatch")
    paths = list((root / "mkspecs").rglob("*"))
    paths += list((root / "lib/cmake").rglob("*.cmake"))
    paths += [p for p in (root / "include").rglob("*.h")
              if p.name == "qconfig.h" or re.fullmatch(r"qt[a-z0-9]+-config(?:_p)?\.h", p.name)]
    files, links = {}, {}
    for path in sorted(set(paths)):
        if path.is_symlink():
            if not path.resolve(strict=True).is_relative_to(root):
                raise ValueError("Qt SDK settings symlink escapes SDK")
            links[path.relative_to(root).as_posix()] = path.readlink().as_posix()
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("Qt SDK setting exceeds size limit")
        files[relative] = digest(path)
    if not any(name.startswith("lib/cmake/") for name in files) or not any(name.startswith("include/") for name in files):
        raise ValueError("Qt SDK settings/configuration headers missing")
    metadata = dict(schema=1, qt_version=version, files=files, links=links)
    with destination.open("xb") as output, tarfile.open(fileobj=output, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for name, expected in files.items():
            path = root / name
            member = tarfile.TarInfo("sdk/" + name); member.size = path.stat().st_size; member.mode = 0o644
            with path.open("rb") as source:
                archive.addfile(member, source)
            if digest(path) != expected:
                raise ValueError("Qt SDK configuration changed during collection")
        body = (json.dumps(metadata, indent=2) + "\n").encode()
        member = tarfile.TarInfo("settings.json"); member.size = len(body); member.mode = 0o644
        archive.addfile(member, io.BytesIO(body))
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qt_root", type=Path); parser.add_argument("version")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    retain_settings(args.qt_root, args.version, args.destination)


if __name__ == "__main__":
    main()
