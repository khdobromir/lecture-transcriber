"""Restore still-pinned input bytes from a verified earlier source offer.

Upstream autobuild/continuous releases expire. This archive is a transport mirror,
not a dependency update: only members matching the current lock are restored.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tarfile
import tempfile
import urllib.request
from package_inputs import source_records

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def restore_archive(archive, cache, records, expected_sha256):
    if archive.is_symlink() or digest(archive) != expected_sha256:
        raise ValueError("Retained archive checksum mismatch")
    if cache.is_symlink():
        raise ValueError("Retained cache must not be a symlink")
    cache.mkdir(parents=True, exist_ok=True)
    wanted = {}
    for record in records:
        name = record["filename"]
        if Path(name).name != name or name in {"", ".", ".."} or "\\" in name or ":" in name:
            raise ValueError("Invalid retained input filename")
        if name in wanted and wanted[name] != record["sha256"]:
            raise ValueError("Conflicting retained input pins")
        wanted[name] = record["sha256"]
    restored, seen = 0, set()
    with tarfile.open(archive, "r|gz") as package:
        metadata = None
        for entry in package:
            if not entry.isfile():
                raise ValueError("Retained archive requires regular files")
            if metadata is None:
                if entry.name != "build-inputs.json":
                    raise ValueError("Missing retained input inventory")
                metadata = json.load(package.extractfile(entry))
                continue
            if not entry.name.startswith("inputs/"):
                continue
            name = entry.name[len("inputs/"):]
            if name not in wanted or metadata["files"].get(entry.name) != wanted[name]:
                continue
            if name in seen:
                raise ValueError("Duplicate retained input")
            seen.add(name)
            target = cache / name
            if target.is_symlink():
                raise ValueError("Retained input must not be a symlink")
            if target.exists():
                if digest(target) != wanted[name]:
                    raise ValueError("Retained cache checksum mismatch: " + name)
                continue
            with tempfile.NamedTemporaryFile(dir=cache, prefix="restore-", delete=False) as stream:
                partial = Path(stream.name)
                try:
                    with package.extractfile(entry) as source:
                        shutil.copyfileobj(source, stream, 1024 * 1024)
                    stream.close()
                    if digest(partial) != wanted[name]:
                        raise ValueError("Retained member checksum mismatch: " + name)
                    try:
                        os.link(partial, target)
                    except FileExistsError:
                        if target.is_symlink() or digest(target) != wanted[name]:
                            raise ValueError("Concurrent retained input changed: " + name)
                    restored += 1
                finally:
                    partial.unlink(missing_ok=True)
    return restored


def restore(platform, cache, records):
    if all((cache / r["filename"]).is_file() for r in records):
        return
    record = json.loads((ROOT / "packaging/retained-inputs.json").read_text())[platform]
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ("retained-" + platform + "-" + record["sha256"][:12] + ".tar.gz")
    if not archive.exists():
        with tempfile.NamedTemporaryFile(dir=cache, prefix="retained-", delete=False) as stream:
            partial = Path(stream.name)
            try:
                request = urllib.request.Request(record["url"], headers={"User-Agent": "Transcribe-package/1"})
                with urllib.request.urlopen(request, timeout=180) as response:
                    if not response.geturl().startswith("https://"):
                        raise ValueError("Retained archive redirect must use HTTPS")
                    shutil.copyfileobj(response, stream, 1024 * 1024)
                stream.close()
                if digest(partial) != record["sha256"]:
                    raise ValueError("Downloaded retained archive checksum mismatch")
                os.link(partial, archive)
            finally:
                partial.unlink(missing_ok=True)
    count = restore_archive(archive, cache, records, record["sha256"])
    print(f"Restored {count} inputs matching current dependency pins", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["linux", "windows"])
    parser.add_argument("cache", type=Path)
    args = parser.parse_args()
    lock = json.loads((ROOT / f"packaging/{args.platform}/dependencies.json").read_text())
    source_lock = json.loads((ROOT / "packaging/source-inputs.json").read_text())
    binaries = list(lock["downloads"].values()) if isinstance(lock["downloads"], dict) else lock["downloads"]
    restore(args.platform, args.cache, binaries + source_records(source_lock, binaries, lock["qt"]))


if __name__ == "__main__":
    main()
