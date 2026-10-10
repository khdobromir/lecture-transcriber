"""Bind partial FFmpeg dependency sources/notices to its verified input and recipes."""
import argparse
import hashlib
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
    generated_inputs = []
    for record in sources:
        if "generated_input" not in record:
            continue
        parent = next(source for source in sources if source["name"] == record["generated_input"]["parent"])
        path = verified_input(parent, cache)
        with tarfile.open(path) as archive:
            scripts = {}
            for relative in ["autogen.sh", "dnn/download_model.sh"]:
                matches = [m for m in archive.getmembers() if m.isfile()
                           and PurePosixPath(m.name).parts[1:] == PurePosixPath(relative).parts]
                if len(matches) != 1 or matches[0].size > 1024 * 1024:
                    raise ValueError("Missing/ambiguous Opus build input reference")
                safe_path(matches[0].name)
                scripts[relative] = archive.extractfile(matches[0]).read()
        checksum = record["upstream_sha256"]
        url = "https://media.xiph.org/opus/models/opus_data-" + checksum + ".tar.gz"
        if (record["url"] != url
                or re.findall(rb'^dnn/download_model.sh "([a-f0-9]{64})"$', scripts["autogen.sh"], re.MULTILINE) != [checksum.encode()]
                or b'model=opus_data-$1.tar.gz\n' not in scripts["dnn/download_model.sh"]
                or b'https://media.xiph.org/opus/models/$model' not in scripts["dnn/download_model.sh"]):
            raise ValueError("Opus generated input does not match pinned source")
        verified_input(parent, cache)
        generated_inputs.append(dict(source=record["name"], parent=parent["name"], parent_sha256=parent["sha256"],
                                     upstream_sha256=checksum, retained_sha256=record["sha256"],
                                     reference_sha256={name: hashlib.sha256(body).hexdigest() for name, body in scripts.items()}))
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
            # A recipe may name a tag; the retained archive still uses its resolved full commit.
            recipe_revision = record.get("recipe_revision", record["revision"])
            revision_variable = "SCRIPT_REV" if record.get("svn_snapshot") is True else "SCRIPT_COMMIT"
            if record.get("svn_snapshot") is True:
                recipe_revision = record["revision"]
            for variable, expected in [("SCRIPT_REPO", record["repository"]), (revision_variable, recipe_revision)]:
                if re.findall(r'^' + variable + slot + r'="([^"\n]+)"$', text, re.MULTILINE) != [expected]:
                    raise ValueError("FFmpeg recipe does not match pinned dependency: " + record["name"])
            if any(flag not in flags or flag.replace("--enable-", "--disable-", 1) in flags
                   for flag in record["configure_flags"]):
                raise ValueError("FFmpeg configuration does not enable dependency: " + record["name"])
    verified_input(recipes[0], cache)
    collect_qt_notices(sources, cache, licenses / "FFmpeg-dependency-source-notices")
    evidence = dict(schema=1, ffmpeg_input_sha256=binary["sha256"], recipe_input=recipes[0], sources=sources,
                    configuration=configuration, generated_inputs=generated_inputs, corresponding_sources_complete=False)
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
