"""Pin source TAR content independently of upstream archive timestamps and compression."""
import argparse
import os
from pathlib import Path, PurePosixPath
import re
import tarfile
import tempfile
from package_source import digest


def selected_paths(paths):
    if paths is not None and (not isinstance(paths, list) or not paths
            or any(not isinstance(path, str) or not re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", path)
                   or any(part in {".", ".."} or part.startswith("-") for part in path.split("/")) for path in paths)
            or len(set(paths)) != len(paths)):
        raise ValueError("Invalid canonical source selection")
    return paths


def canonicalize_tar(source, destination, paths=None):
    paths = selected_paths(paths)
    if source.is_symlink():
        raise ValueError("Source archive must not be a symlink")
    with tarfile.open(source) as archive:
        entries = {}
        for member in archive:
            path = PurePosixPath(member.name)
            if (not path.parts or path.is_absolute() or ".." in path.parts
                    or "\\" in member.name or ":" in member.name):
                raise ValueError("Unsafe canonical source path")
            name = path.as_posix()
            if name in entries:
                raise ValueError("Duplicate canonical source entry")
            if not member.isfile() and not member.isdir():
                raise ValueError("Unsupported canonical source entry type")
            if set(member.pax_headers) - {"mtime", "atime", "ctime", "path", "uid", "gid", "uname", "gname"}:
                raise ValueError("Unsupported canonical source metadata")
            entries[name] = member
        if paths is not None:
            if any(path not in entries or not entries[path].isfile() for path in paths):
                raise ValueError("Missing canonical source selection")
            entries = {path: entries[path] for path in paths}
        with destination.open("xb") as output, tarfile.open(fileobj=output, mode="w", format=tarfile.PAX_FORMAT) as normalized:
            for name, member in sorted(entries.items()):
                entry = tarfile.TarInfo(name)
                entry.type = tarfile.REGTYPE if member.isfile() else tarfile.DIRTYPE
                entry.size = member.size if member.isfile() else 0
                entry.mode = member.mode & 0o7777
                entry.mtime = entry.uid = entry.gid = 0
                entry.uname = entry.gname = ""
                if member.isfile():
                    with archive.extractfile(member) as stream:
                        normalized.addfile(entry, stream)
                else:
                    normalized.addfile(entry)


def canonicalize_verified_tar(source, destination, expected, paths=None, upstream_sha256=None):
    if not re.fullmatch(r"[a-f0-9]{64}", expected):
        raise ValueError("Invalid canonical source checksum")
    if upstream_sha256 is not None and (not re.fullmatch(r"[a-f0-9]{64}", upstream_sha256)
                                       or source.is_symlink() or digest(source) != upstream_sha256):
        raise ValueError("Upstream source checksum mismatch")
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="canonical-source-") as temporary:
        normalized = Path(temporary) / "input.tar"
        canonicalize_tar(source, normalized, paths)
        if digest(normalized) != expected:
            raise ValueError("Canonical source checksum mismatch: " + destination.name)
        try:
            # Do not replace a previous input, including during concurrent downloads.
            os.link(normalized, destination)
        except FileExistsError:
            if destination.is_symlink() or not destination.is_file() or digest(destination) != expected:
                raise ValueError("Canonical source cache checksum mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("download", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("sha256")
    parser.add_argument("--path", action="append", dest="paths")
    parser.add_argument("--upstream-sha256")
    args = parser.parse_args()
    canonicalize_verified_tar(args.download, args.destination, args.sha256, args.paths, args.upstream_sha256)


if __name__ == "__main__":
    main()
