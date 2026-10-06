"""Validate a final ZIP before running its contents; also tested on Linux."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def extract_verified(archive, destination, expected_source):
    with zipfile.ZipFile(archive) as package:
        names = set()
        for entry in package.infolist():
            path = PurePosixPath(entry.filename)
            if (entry.filename in names or path.is_absolute() or not path.parts or path.parts[0] != "Transcribe"
                    or ".." in path.parts or "\\" in entry.filename or ":" in entry.filename
                    or stat.S_ISLNK(entry.external_attr >> 16)):
                raise ValueError("Unsafe or duplicate package member")
            names.add(entry.filename)
        package.extractall(destination)
    root = destination / "Transcribe"
    manifest = json.loads((root / "package-manifest.json").read_text(encoding="utf-8-sig"))
    if (manifest.get("version") != 1 or manifest.get("source") != expected_source
            or not re.fullmatch(r"[0-9a-f]{40}", expected_source)):
        raise ValueError("Package source/version mismatch")
    claimed = set()
    for entry in manifest["files"]:
        path = PurePosixPath(entry["path"])
        if (not path.parts or path.is_absolute() or ".." in path.parts or "\\" in entry["path"]
                or ":" in entry["path"] or entry["path"] in claimed):
            raise ValueError("Invalid manifest path")
        claimed.add(entry["path"])
        if digest(root / Path(*path.parts)) != entry["sha256"]:
            raise ValueError("Package file checksum mismatch: " + entry["path"])
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()}
    actual.discard("package-manifest.json")
    if claimed != actual:
        raise ValueError("Package manifest does not cover every file")
    for directory in ["bin", "bin/tools"]:
        for runtime in ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]:
            if not (root / directory / runtime).is_file():
                raise ValueError("Missing app-local CRT: " + directory + '/' + runtime)
    return root, manifest
