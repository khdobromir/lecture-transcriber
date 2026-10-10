"""Bind partial FFmpeg dependency sources/notices to its verified input and recipes."""
import argparse
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tarfile
from package_inputs import ffmpeg_source_records
from package_notices import collect_qt_notices, safe_path
from package_source import digest


def verified_input(record, cache):
    name = record["filename"]
    if Path(name).name != name or "\\" in name or ":" in name:
        raise ValueError("Unsafe FFmpeg input filename")
    path = cache / name
    if path.is_symlink() or digest(path) != record["sha256"]:
        raise ValueError("FFmpeg input checksum mismatch: " + name)
    return path


def collect_ffmpeg_notices(lock, binary, cache, licenses, configuration):
    sources = ffmpeg_source_records(lock, [binary])
    verified_input(binary, cache)
    recipes = [r for r in lock["downloads"] if r["name"] == "ffmpeg-build-recipes"
               and binary["sha256"] in r["for_binary_sha256"]]
    if len(recipes) != 1:
        raise ValueError("Missing/ambiguous FFmpeg build recipes")
    recipe_archive = verified_input(recipes[0], cache)
    flags = set(shlex.split(configuration))
    with tarfile.open(recipe_archive) as archive:
        members = archive.getmembers()
        if len({m.name for m in members}) != len(members):
            raise ValueError("Duplicate FFmpeg recipe entry")
        for record in sources:
            safe_path("source/" + record["recipe"])
            slot = record["recipe_slot"]
            if not re.fullmatch(r"[0-9]*", slot):
                raise ValueError("Unsafe FFmpeg recipe slot")
            matches = [m for m in members if m.isfile()
                       and PurePosixPath(m.name).parts[1:] == PurePosixPath(record["recipe"]).parts]
            if len(matches) != 1:
                raise ValueError("Missing/ambiguous FFmpeg dependency recipe")
            safe_path(matches[0].name)
            text = archive.extractfile(matches[0]).read().decode("utf-8")
            for variable, expected in [("SCRIPT_REPO", record["repository"]), ("SCRIPT_COMMIT", record["revision"])]:
                if re.findall(r'^' + variable + slot + r'="([^"\n]+)"$', text, re.MULTILINE) != [expected]:
                    raise ValueError("FFmpeg recipe does not match pinned dependency: " + record["name"])
            if any(flag not in flags or flag.replace("--enable-", "--disable-", 1) in flags
                   for flag in record["configure_flags"]):
                raise ValueError("FFmpeg configuration does not enable dependency: " + record["name"])
    verified_input(recipes[0], cache)
    collect_qt_notices(sources, cache, licenses / "FFmpeg-dependency-source-notices")
    evidence = dict(schema=1, ffmpeg_input_sha256=binary["sha256"], recipe_input=recipes[0], sources=sources,
                    configuration=configuration, corresponding_sources_complete=False)
    (licenses / "ffmpeg-source-provenance.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_lock", type=Path)
    parser.add_argument("binary_lock", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("licenses", type=Path)
    parser.add_argument("ffmpeg", type=Path)
    args = parser.parse_args()
    lock = json.loads(args.source_lock.read_text(encoding="utf-8-sig"))
    dependencies = json.loads(args.binary_lock.read_text(encoding="utf-8-sig"))["downloads"]
    binary = dependencies["ffmpeg"] if isinstance(dependencies, dict) else next(r for r in dependencies if r["name"] == "ffmpeg")
    verified_input(binary, args.cache)
    configuration = subprocess.check_output([str(args.ffmpeg), "-buildconf"], stderr=subprocess.STDOUT, text=True)
    collect_ffmpeg_notices(lock, binary, args.cache, args.licenses, configuration)


if __name__ == "__main__":
    main()
