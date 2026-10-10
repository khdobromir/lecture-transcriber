"""Preserve verified upstream build inputs; this is not a complete source offer."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile
import re
from package_source import digest
from package_git_sources import selected_paths as selected_git_paths
from package_svn_sources import validate_mapping as validate_svn_mapping
from package_canonical_sources import selected_paths as selected_canonical_paths


def ffmpeg_source_records(lock, binaries):
    available = {record["sha256"] for record in binaries}
    group = lock["ffmpeg_dependencies"]
    supported = set(group["for_binary_sha256"])
    core = [record for record in lock["downloads"] if record["name"] == "ffmpeg-source"]
    if (len(core) != 1 or supported != set(core[0]["for_binary_sha256"])
            or len(available.intersection(supported)) != 1):
        raise ValueError("FFmpeg dependency sources do not match pinned binary")
    selected = []
    for record in group["downloads"]:
        if not record["for_binary_sha256"] or not set(record["for_binary_sha256"]).issubset(supported):
            raise ValueError("FFmpeg dependency source has an unknown binary mapping")
        is_svn = record.get("svn_snapshot") is True
        if (record["kind"] != "ffmpeg-dependency-source" or not record["required_notices"]
                or not record["configure_flags"]
                or not re.fullmatch(r"[1-9][0-9]*" if is_svn else r"[a-f0-9]{40}", record["revision"])
                or ("svn_snapshot" in record and (not is_svn or "git_snapshot" in record or "canonical_tar" in record))
                or ("canonical_tar" in record and record["canonical_tar"] is not True)
                or ("git_snapshot" in record and (record["git_snapshot"] is not True
                    or record.get("canonical_tar") is True
                    or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", record.get("git_archive_prefix", ""))))
                or any(not re.fullmatch(r"--enable-[a-z0-9-]+", flag) for flag in record["configure_flags"])):
            raise ValueError("Incomplete FFmpeg dependency source mapping")
        if "git_archive_paths" in record:
            if record.get("git_snapshot") is not True:
                raise ValueError("Git source selection requires a Git snapshot")
            selected_git_paths(record)
        if "canonical_tar_paths" in record or "upstream_sha256" in record:
            if (record.get("canonical_tar") is not True
                    or not re.fullmatch(r"[a-f0-9]{64}", record.get("upstream_sha256", ""))):
                raise ValueError("Canonical source selection requires a pinned upstream archive")
            selected_canonical_paths(record.get("canonical_tar_paths"))
        if sum(key in record for key in ["generated_input", "git_submodule", "deps_input"]) > 1:
            raise ValueError("Ambiguous nested FFmpeg source mapping")
        if "deps_input" in record:
            reference = record["deps_input"]
            parents = [parent for parent in group["downloads"] if isinstance(reference, dict)
                       and parent["name"] == reference.get("parent")
                       and not any(key in parent for key in ["generated_input", "git_submodule", "deps_input"])]
            if (not isinstance(reference, dict) or set(reference) != {"parent", "path"}
                    or len(parents) != 1 or is_svn
                    or any(record.get(key) != parents[0].get(key) for key in
                           ["recipe", "recipe_slot", "recipe_revision", "for_binary_sha256", "configure_flags"])):
                raise ValueError("Incomplete FFmpeg DEPS source mapping")
            selected_git_paths({"git_archive_paths": [reference["path"]]})
        if "generated_input" in record:
            generated = record["generated_input"]
            parents = [parent for parent in group["downloads"] if isinstance(generated, dict)
                       and parent["name"] == generated.get("parent") and "generated_input" not in parent]
            if (not isinstance(generated, dict) or set(generated) != {"kind", "parent"}
                    or generated.get("kind") != "opus-dnn" or len(parents) != 1
                    or record.get("canonical_tar") is not True or record.get("strip_components") != 0
                    or not re.fullmatch(r"[a-f0-9]{64}", record.get("upstream_sha256", ""))
                    or not record.get("canonical_tar_paths")
                    or any(not path.startswith("dnn/") or not path.endswith((".c", ".h"))
                           for path in record["canonical_tar_paths"])
                    or any(record.get(key) != parents[0].get(key) for key in
                           ["revision", "repository", "recipe", "recipe_slot", "recipe_revision",
                            "for_binary_sha256", "configure_flags"])):
                raise ValueError("Incomplete generated FFmpeg source mapping")
        if "git_submodule" in record:
            module = record["git_submodule"]
            parents = [parent for parent in group["downloads"] if isinstance(module, dict)
                       and parent["name"] == module.get("parent")
                       and "git_submodule" not in parent and "generated_input" not in parent]
            if (not isinstance(module, dict)
                    or set(module) != {"parent", "path", "commit_base64", "trees_base64"}
                    or len(parents) != 1 or "generated_input" in record or is_svn
                    or not isinstance(module["commit_base64"], str)
                    or not isinstance(module["trees_base64"], dict)
                    or any(record.get(key) != parents[0].get(key) for key in
                           ["recipe", "recipe_slot", "recipe_revision", "for_binary_sha256", "configure_flags"])):
                raise ValueError("Incomplete FFmpeg submodule source mapping")
            selected_git_paths({"git_archive_paths": [module["path"]]})
        if is_svn:
            validate_svn_mapping(record)
        if available.intersection(record["for_binary_sha256"]):
            selected.append(record)
    if not selected:
        raise ValueError("Incomplete FFmpeg dependency source set")
    return selected


def source_records(lock, binaries, qt_version):
    """Reject stale source mappings after a binary dependency is updated."""
    binaries = list(binaries)
    available = {record["sha256"] for record in binaries}
    for record in lock["downloads"]:
        if not available.intersection(record["for_binary_sha256"]):
            raise ValueError("Source input does not match pinned binary: " + record["name"])
    if lock["qt"]["version"] != qt_version or any(record["revision"] != qt_version for record in lock["qt"]["downloads"]):
        raise ValueError("Qt source input does not match pinned SDK")
    selected = lock["downloads"] + lock["qt"]["downloads"]
    if "standalone_dependencies" in lock:
        standalone = lock["standalone_dependencies"]
        supported = set(standalone["for_binary_sha256"])
        tools = [record for record in lock["downloads"] if record["name"] == "yt-dlp-source"]
        if len(tools) != 1 or available.intersection(tools[0]["for_binary_sha256"]) != available.intersection(supported):
            raise ValueError("Standalone dependency sources do not match pinned binary")
        for record in standalone["downloads"]:
            if not record["for_binary_sha256"] or not set(record["for_binary_sha256"]).issubset(supported):
                raise ValueError("Standalone dependency source has an unknown binary mapping")
            if available.intersection(record["for_binary_sha256"]):
                selected.append(record)
    return selected + ffmpeg_source_records(lock, binaries)


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
    commands = parser.add_subparsers(dest="command", required=True)
    select = commands.add_parser("select-sources", help="Validate source mappings and print inputs for one platform")
    select.add_argument("sources", type=Path)
    select.add_argument("binaries", type=Path)
    archive = commands.add_parser("archive", help="Retain checksum-verified inputs")
    archive.add_argument("records", type=Path)
    archive.add_argument("cache", type=Path)
    archive.add_argument("destination", type=Path)
    archive.add_argument("--material", action="append", type=Path, default=[])
    args = parser.parse_args()
    if args.command == "select-sources":
        lock = json.loads(args.binaries.read_text(encoding="utf-8-sig"))
        binaries = lock["downloads"]
        records = source_records(json.loads(args.sources.read_text(encoding="utf-8-sig")),
                                 list(binaries.values()) if isinstance(binaries, dict) else binaries, lock["qt"])
        print(json.dumps(dict(downloads=records)))
        return
    records = json.loads(args.records.read_text(encoding="utf-8-sig"))["downloads"]
    archive_inputs(list(records.values()) if isinstance(records, dict) else records,
                   args.cache, args.destination, args.material)


if __name__ == "__main__":
    main()
