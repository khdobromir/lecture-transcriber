"""Preserve verified upstream build inputs; this is not a complete source offer."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
from package_source import digest


def archive_inputs(records, cache, destination, materials=()):
    selected = []
    hashes = {}
    names = set()
    for record in records:
        name = record["filename"]
        if Path(name).name != name or "\\" in name or ":" in name or name in {"", ".", ".."} or name in names:
            raise ValueError("Invalid/duplicate build input filename")
        names.add(name)
        path = cache / name
        if path.is_symlink() or digest(path) != record["sha256"]:
            raise ValueError("Build input checksum mismatch: " + name)
        selected.append((path, "inputs/" + name))
        hashes["inputs/" + name] = record["sha256"]
    for path in materials:
        name = "materials/" + path.name
        if name in names or not path.is_file() or path.is_symlink():
            raise ValueError("Invalid/duplicate build material")
        names.add(name)
        selected.append((path, name))
        hashes[name] = digest(path)
    metadata = json.dumps(dict(schema=1, kind="verified-build-inputs", corresponding_sources_complete=False,
                               downloads=records, files=hashes), indent=2).encode()
    with destination.open("xb") as output, tarfile.open(fileobj=output, mode="w:gz") as archive:
        entry = tarfile.TarInfo("build-inputs.json"); entry.size = len(metadata)
        archive.addfile(entry, io.BytesIO(metadata))
        def public_owner(entry):
            entry.uid = entry.gid = 0
            entry.uname = entry.gname = ""
            entry.mtime = 0
            return entry
        for path, name in selected:
            archive.add(path, arcname=name, recursive=False, filter=public_owner)
    # Verify the retained bytes, including possible mutation during archival.
    with tarfile.open(destination) as archive:
        for name, expected in json.loads(metadata)["files"].items():
            checksum = hashlib.sha256()
            with archive.extractfile(name) as source:
                for block in iter(lambda: source.read(1024 * 1024), b""):
                    checksum.update(block)
            if checksum.hexdigest() != expected:
                raise ValueError("Archived build input changed: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("records", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--material", action="append", type=Path, default=[])
    args = parser.parse_args()
    records = json.loads(args.records.read_text(encoding="utf-8-sig"))["downloads"]
    archive_inputs(list(records.values()) if isinstance(records, dict) else records,
                   args.cache, args.destination, args.material)


if __name__ == "__main__":
    main()
