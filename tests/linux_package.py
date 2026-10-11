"""Verify the complete extracted Linux payload, including its symlinks."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def verify(root, expected_source):
    manifest = json.loads((root / "package-manifest.json").read_text())
    if manifest.get("schema") != 1 or manifest.get("source") != expected_source:
        raise ValueError("Linux package provenance mismatch")
    claimed = set()
    for entry in manifest["files"]:
        name = entry["path"]
        path = PurePosixPath(name)
        if (not path.parts or path.is_absolute() or ".." in path.parts or "\\" in name
                or name in claimed or name == "package-manifest.json"):
            raise ValueError("Unsafe or duplicate manifest path")
        claimed.add(name)
        target = root / Path(*path.parts)
        if not target.resolve().is_relative_to(root.resolve()):
            raise ValueError("Package path escapes root")
        if "link" in entry:
            if not target.is_symlink() or os.readlink(target) != entry["link"] or not target.exists():
                raise ValueError("Package symlink mismatch: " + name)
            if Path(entry["link"]).is_absolute():
                raise ValueError("Absolute package symlink")
        elif target.is_symlink() or not target.is_file() or digest(target) != entry["sha256"]:
            raise ValueError("Package checksum mismatch: " + name)
        elif target.stat().st_mode & 0o777 != entry["mode"]:
            raise ValueError("Package mode mismatch: " + name)
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file() or path.is_symlink()}
    actual.discard("package-manifest.json")
    if actual != claimed:
        raise ValueError("Linux manifest does not cover the whole package")
    for name in ["AppRun", "usr/bin/transcribe", "usr/bin/transcribe-gui", "usr/bin/tools/.transcribe-bundle",
                 "usr/bin/tools/whisper-cli", "usr/bin/tools/whisper-cli.bin",
                 "usr/bin/tools/libggml-cpu-x64.so", "usr/bin/tools/libggml-cpu-sandybridge.so",
                 "usr/bin/tools/libggml-cpu-haswell.so", "usr/bin/tools/ffmpeg", "usr/bin/tools/ffprobe", "usr/bin/tools/yt-dlp"]:
        if not (root / name).is_file():
            raise ValueError("Missing Linux package component: " + name)
    return manifest
