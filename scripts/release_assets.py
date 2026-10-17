"""Inventory an exact release set or verify uploaded GitHub asset digests.

Read-only: never uploads, tags, publishes, or rewrites artifacts. Automatically
generated GitHub source archives are not uploaded assets and are checked separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def asset_names(version):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-(?:alpha|beta|rc)\.[1-9][0-9]*)?", version):
        raise ValueError("Expected X.Y.Z or X.Y.Z-(alpha|beta|rc).N version")
    prefix = "Transcribe-" + version
    linux = prefix + "-linux-x86_64.AppImage"
    windows = prefix + "-windows-x64.zip"
    return sorted([linux, windows, prefix + "-source.tar.gz",
                   linux + ".build-inputs.tar.gz", windows + ".build-inputs.tar.gz",
                   linux + ".json", windows + ".manifest.json", windows + ".validation.json",
                   "linux-ci-artifact.json", "windows-ci-artifact.json",
                   "release-validation.json", "source-offer-verification.json"])


def inventory(directory, version):
    expected = asset_names(version)
    actual = sorted(path.name for path in directory.iterdir())
    if actual != expected:
        raise ValueError("Release asset set differs: missing=" + str(sorted(set(expected) - set(actual)))
                         + ", unexpected=" + str(sorted(set(actual) - set(expected))))
    result = []
    for name in expected:
        path = directory / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            raise ValueError("Expected nonempty regular asset: " + name)
        result.append(dict(name=name, size=path.stat().st_size, sha256=digest(path)))
    return result


def verify_github(local, release):
    remote = release["assets"]
    names = [item["name"] for item in remote]
    if len(set(names)) != len(names) or sorted(names) != sorted(item["name"] for item in local):
        raise ValueError("Uploaded GitHub asset set differs from local inventory")
    indexed = {item["name"]: item for item in remote}
    for item in local:
        uploaded = indexed[item["name"]]
        if uploaded.get("digest") != "sha256:" + item["sha256"] or uploaded.get("size") != item["size"]:
            raise ValueError("Missing/mismatched GitHub digest or size: " + item["name"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["inventory", "names", "verify-github"])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--github-json", type=Path)
    args = parser.parse_args()
    local = inventory(args.directory, args.version)
    if args.command == "verify-github":
        if args.github_json is None:
            parser.error("verify-github requires --github-json")
        verify_github(local, json.loads(args.github_json.read_text(encoding="utf-8")))
        print("Every uploaded asset matches its local SHA-256 and size.")
    elif args.command == "names":
        print("\n".join(item["name"] for item in local))
    else:
        print(json.dumps(dict(schema=1, version=args.version, assets=local), indent=2))


if __name__ == "__main__":
    main()
