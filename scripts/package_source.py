"""Shared source snapshot and release guards for Linux and Windows packagers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def source_identity(project):
    # Git -c is local to this command and permits a read-only Docker source mount.
    git = ["git", "-c", "safe.directory=" + str(project), "-C", str(project)]
    revision = subprocess.check_output(git + ["rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(git + ["status", "--porcelain"]))
    files = subprocess.check_output(git + ["ls-files", "-z", "--cached", "--others", "--exclude-standard"]).split(b"\0")
    fingerprint = hashlib.sha256()
    for name in sorted(set(files) - {b""}):
        path = project / os.fsdecode(name)
        fingerprint.update(name + b"\0")
        if path.is_symlink():
            fingerprint.update(os.fsencode(os.readlink(path)))
        elif path.is_file():
            fingerprint.update(digest(path).encode())
        else:
            fingerprint.update(b"deleted")
    return {"source": revision, "dirty": dirty, "source_fingerprint": fingerprint.hexdigest()}



def application_version(project):
    match = re.search(r"project\(transcribe VERSION (\d+\.\d+\.\d+)\b", (project / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not match:
        raise ValueError("Cannot determine application version")
    return match[1]


def validate_identity(project, expected=None, release=False, skip_tests=False):
    current = dict(source_identity(project), application_version=application_version(project))
    if expected is not None and current != expected:
        raise ValueError("Source changed during packaging; candidate was not published")
    if release and (current["dirty"] or skip_tests):
        raise ValueError("Release packaging requires clean sources and all tests")
    return current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--expect", type=Path)
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--skip-tests", action="store_true")
    args = parser.parse_args()
    expected = json.loads(args.expect.read_text(encoding="utf-8-sig")) if args.expect else None
    print(json.dumps(validate_identity(args.project, expected, args.release, args.skip_tests)))


if __name__ == "__main__":
    main()
