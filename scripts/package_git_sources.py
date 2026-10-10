"""Retain a pinned source TAR directly from an HTTPS Git repository."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlsplit
from package_source import digest


def selected_paths(record):
    if "git_archive_paths" not in record:
        return []
    paths = record["git_archive_paths"]
    if (not isinstance(paths, list) or not paths
            or any(not isinstance(path, str) or not re.fullmatch(r"[A-Za-z0-9._][A-Za-z0-9._/-]*", path)
                   or any(part in {"", ".", ".."} for part in path.split("/")) for path in paths)
            or len(set(paths)) != len(paths)):
        raise ValueError("Invalid Git source path selection")
    return paths


def fetch_git_source(record, destination):
    paths = selected_paths(record)
    url = urlsplit(record["url"])
    revision = record["revision"]
    prefix = record["git_archive_prefix"]
    expected = record["sha256"]
    if (url.scheme != "https" or not url.netloc or url.username is not None
            or url.password is not None or url.query or url.fragment
            or not re.fullmatch(r"[a-f0-9]{40}", revision)
            or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", prefix)
            or not re.fullmatch(r"[a-f0-9]{64}", expected)):
        raise ValueError("Invalid Git source mapping")
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file() or digest(destination) != expected:
            raise ValueError("Git source cache checksum mismatch")
        return destination
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="git-source-") as temporary:
        root = Path(temporary)
        empty = root / "empty"; empty.mkdir()
        config = root / "empty.gitconfig"; config.write_bytes(b"")
        environment = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        environment.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(config),
                           GIT_TERMINAL_PROMPT="0", GIT_CEILING_DIRECTORIES=str(root))
        command = ["git", "-c", "core.hooksPath=" + str(empty), "-c", "init.templateDir=" + str(empty),
                   "-c", "credential.helper=", "-c", "protocol.allow=never", "-c", "protocol.https.allow=always",
                   "-c", "http.followRedirects=false", "-c", "tar.umask=0022"]

        def run(arguments):
            result = subprocess.run(command + arguments, env=environment, cwd=root,
                                    capture_output=True, timeout=300 if paths else 120, check=False)
            if result.returncode:
                raise RuntimeError("Git source command failed for " + destination.name + ": "
                                   + result.stderr.decode("utf-8", errors="replace").strip())
            return result.stdout

        repository = root / "repository.git"
        run(["init", "--bare", str(repository)])
        run(["-C", str(repository), "fetch", "--depth=1", "--no-tags", record["url"], revision])
        actual = run(["-C", str(repository), "rev-parse", "FETCH_HEAD^{commit}"]).decode("ascii").strip()
        if actual != revision:
            raise ValueError("Git source commit mismatch")
        archive = root / "input.tar"
        run(["-C", str(repository), "archive", "--format=tar", "--prefix=" + prefix + "/",
             "--output=" + str(archive), revision] + (["--"] + paths if paths else []))
        if archive.is_symlink() or digest(archive) != expected:
            raise ValueError("Git source checksum mismatch: " + destination.name)
        try:
            os.link(archive, destination)
        except FileExistsError:
            if destination.is_symlink() or not destination.is_file() or digest(destination) != expected:
                raise ValueError("Git source cache checksum mismatch")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("revision")
    parser.add_argument("prefix")
    parser.add_argument("destination", type=Path)
    parser.add_argument("sha256")
    parser.add_argument("--path", action="append", dest="paths")
    args = parser.parse_args()
    record = dict(url=args.url, revision=args.revision, git_archive_prefix=args.prefix, sha256=args.sha256)
    if args.paths is not None:
        record["git_archive_paths"] = args.paths
    fetch_git_source(record, args.destination)


if __name__ == "__main__":
    main()
