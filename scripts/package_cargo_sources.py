"""Retain Cargo.lock registry sources and original notices without running Cargo."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import tarfile
import tempfile
import tomllib
import urllib.request

from package_source import digest
from package_notices import safe_path


def registry_inputs(parent):
    with tarfile.open(parent) as archive:
        members = [m for m in archive if m.isfile() and PurePosixPath(m.name).parts[1:] == ("Cargo.lock",)]
        if len(members) != 1 or members[0].size > 4 * 1024 * 1024:
            raise ValueError("Missing/ambiguous Cargo.lock")
        safe_path(members[0].name)
        body = archive.extractfile(members[0]).read()
    packages = []
    for package in tomllib.loads(body.decode("utf-8"))["package"]:
        if "source" not in package:
            continue
        if (package["source"] != "registry+https://github.com/rust-lang/crates.io-index"
                or not re.fullmatch(r"[A-Za-z0-9_-]+", package["name"])
                or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-+][A-Za-z0-9.+-]+)?", package["version"])
                or not re.fullmatch(r"[a-f0-9]{64}", package.get("checksum", ""))):
            raise ValueError("Unsupported Cargo registry input")
        packages.append({key: package[key] for key in ["name", "version", "checksum"]})
    packages.sort(key=lambda p: (p["name"], p["version"]))
    if not packages or len({(p["name"], p["version"]) for p in packages}) != len(packages):
        raise ValueError("Missing/duplicate Cargo registry package")
    return body, packages


def fetch(url, target, expected):
    if target.is_file():
        if target.is_symlink() or digest(target) != expected:
            raise ValueError("Cargo source cache checksum mismatch")
        return target
    request = urllib.request.Request(url, headers={"User-Agent": "Transcribe-package-sources/1"})
    if not url.startswith("https://"):
        raise ValueError("Cargo source URL must use HTTPS")
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix="cargo-download-", delete=False) as temporary:
        partial = Path(temporary.name)
    try:
        with urllib.request.urlopen(request, timeout=45) as source, partial.open("wb") as out:
            if not source.geturl().startswith("https://"):
                raise ValueError("Cargo source redirect must use HTTPS")
            total = 0
            while block := source.read(1024 * 1024):
                total += len(block)
                if total > 128 * 1024 * 1024:
                    raise ValueError("Cargo source exceeds size limit")
                out.write(block)
        if digest(partial) != expected:
            raise ValueError("Downloaded Cargo source checksum mismatch")
        try:
            os.link(partial, target)
        except FileExistsError:
            if target.is_symlink() or digest(target) != expected:
                raise ValueError("Cargo source cache checksum mismatch")
    finally:
        partial.unlink(missing_ok=True)
    return target


def crate_name(package):
    return package["name"] + "-" + package["version"] + ".crate"


def notice_files(path, package):
    prefix = package["name"] + "-" + package["version"]
    result = {}
    with tarfile.open(path) as archive:
        seen = set()
        total = 0
        for member in archive:
            relative = PurePosixPath(member.name)
            if (relative.is_absolute() or ".." in relative.parts or "\\" in member.name
                    or ":" in member.name or not relative.parts or relative.parts[0] != prefix
                    or member.name in seen):
                raise ValueError("Unsafe/duplicate Cargo archive path")
            seen.add(member.name)
            name = relative.name.lower()
            if not member.isfile() or not (name.startswith(("license", "copying", "copyright", "notice"))
                                          or relative.parts[1:] == ("Cargo.toml",)):
                continue
            total += member.size
            if member.size > 8 * 1024 * 1024 or total > 32 * 1024 * 1024:
                raise ValueError("Cargo notices exceed size limit")
            result["licenses/" + relative.as_posix()] = archive.extractfile(member).read()
    if "licenses/" + prefix + "/Cargo.toml" not in result:
        raise ValueError("Cargo source lacks original package metadata")
    return result


def retain(parent, cache, destination, expected=None):
    body, packages = registry_inputs(parent)
    cache.mkdir(parents=True, exist_ok=True)
    def download(package):
        name = crate_name(package)
        return fetch("https://static.crates.io/crates/" + package["name"] + "/" + name,
                     cache / name, package["checksum"])
    with ThreadPoolExecutor(max_workers=8) as pool:
        paths = list(pool.map(download, packages))
    metadata = dict(schema=1, parent_sha256=digest(parent), lock_sha256=hashlib.sha256(body).hexdigest(), packages=packages)
    files = {"Cargo.lock": body, "registry-sources.json": (json.dumps(metadata, indent=2) + "\n").encode()}
    for package, path in zip(packages, paths):
        files.update(notice_files(path, package))
    with tempfile.NamedTemporaryFile(dir=destination.parent, prefix="cargo-bundle-", delete=False) as temporary:
        partial = Path(temporary.name)
    try:
        # Crate archives are already compressed. A plain canonical TAR avoids
        # dependence on the packager's zlib version on Linux and Windows.
        with partial.open("wb") as output:
            with tarfile.open(fileobj=output, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for name, content in sorted(files.items()):
                    member = tarfile.TarInfo(name); member.size = len(content); member.mode = 0o644
                    archive.addfile(member, io.BytesIO(content))
                for package, path in zip(packages, paths):
                    member = tarfile.TarInfo("crates/" + crate_name(package)); member.size = path.stat().st_size; member.mode = 0o644
                    with path.open("rb") as source:
                        archive.addfile(member, source)
        if expected is not None and digest(partial) != expected:
            raise ValueError("Retained Cargo source archive checksum mismatch")
        os.link(partial, destination)
    finally:
        partial.unlink(missing_ok=True)
    return metadata


def verify(parent, bundle):
    body, packages = registry_inputs(parent)
    with tarfile.open(bundle) as archive:
        members = {m.name: m for m in archive.getmembers()}
        if len(members) != len(archive.getmembers()) or any(not m.isfile() for m in members.values()):
            raise ValueError("Invalid Cargo source bundle entries")
        metadata = json.loads(archive.extractfile("registry-sources.json").read())
        expected = dict(schema=1, parent_sha256=digest(parent), lock_sha256=hashlib.sha256(body).hexdigest(), packages=packages)
        if metadata != expected or archive.extractfile("Cargo.lock").read() != body:
            raise ValueError("Cargo source bundle does not match parent lock")
        if {n for n in members if n.startswith("crates/")} != {"crates/" + crate_name(p) for p in packages}:
            raise ValueError("Cargo source bundle package set mismatch")
        for package in packages:
            with archive.extractfile("crates/" + crate_name(package)) as source:
                if hashlib.file_digest(source, "sha256").hexdigest() != package["checksum"]:
                    raise ValueError("Retained Cargo crate checksum mismatch")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url"); parser.add_argument("parent_sha256")
    parser.add_argument("destination", type=Path); parser.add_argument("sha256")
    args = parser.parse_args()
    if args.destination.exists():
        if args.destination.is_symlink() or digest(args.destination) != args.sha256:
            raise ValueError("Cargo source bundle checksum mismatch")
        return
    with tempfile.TemporaryDirectory(prefix="cargo-sources-", dir=args.destination.parent) as temporary:
        root = Path(temporary)
        parent = fetch(args.url, root / "parent.tar.gz", args.parent_sha256)
        retain(parent, root / "crates", args.destination, args.sha256)
        verify(parent, args.destination)


if __name__ == "__main__":
    main()
