"""Copy original notices from checksum-verified archives without running source code."""
import argparse
import json
from pathlib import Path, PurePosixPath
import posixpath
import tarfile
from package_source import digest


def safe_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or len(path.parts) < 2:
        raise ValueError("Unsafe source notice path: " + name)
    return path


def collect_qt_notices(records, cache, destination):
    inventory = []
    for record in records:
        strip_components = record.get("strip_components", 1)
        if type(strip_components) is not int or strip_components not in (0, 1):
            raise ValueError("Invalid source archive prefix")
        def validate_notice_path(name):
            # Googlesource archives have no enclosing directory; retain their paths intact.
            if PurePosixPath(name).is_absolute():
                raise ValueError("Unsafe source notice path: " + name)
            safe_path(name if strip_components else "source/" + name)
        if Path(record["name"]).name != record["name"] or Path(record["filename"]).name != record["filename"]:
            raise ValueError("Invalid source input basename")
        source = cache / record["filename"]
        if source.is_symlink() or digest(source) != record["sha256"]:
            raise ValueError("Source notice archive checksum mismatch")
        with tarfile.open(source) as archive:
            members = {}
            for member in archive:
                if member.name in members:
                    raise ValueError("Duplicate source archive entry")
                members[member.name] = member
            selected = set()
            for name, member in members.items():
                if not member.isfile():
                    continue
                basename = PurePosixPath(name).name.lower()
                if (basename.startswith(("license", "copying", "copyright", "notice")) or basename == "qt_attribution.json"
                        or "licenses" in [part.lower() for part in PurePosixPath(name).parts[:-1]]):
                    validate_notice_path(name)
                    selected.add(name)
                if basename == "qt_attribution.json":
                    # Qt 6.8.3 contains literal newlines in attribution strings.
                    attributions = json.loads(archive.extractfile(member).read(), strict=False)
                    for attribution in attributions if isinstance(attributions, list) else [attributions]:
                        for field in ["LicenseFile", "CopyrightFile"]:
                            references = attribution.get(field, [])
                            if isinstance(references, str):
                                references = [references]
                            for reference in references:
                                if not reference:
                                    continue
                                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), reference))
                                validate_notice_path(resolved)
                                if resolved not in members or not members[resolved].isfile():
                                    raise ValueError("Missing source notice reference: " + resolved)
                                selected.add(resolved)
            # Some upstream notices live in README or source headers (e.g. zlib).
            # Retain the explicitly declared originals without running any code.
            for relative in record.get("required_notices", []):
                safe_path("source/" + relative)
                matches = [name for name, member in members.items() if member.isfile()
                           and PurePosixPath(name).parts[strip_components:] == PurePosixPath(relative).parts]
                if len(matches) != 1:
                    raise ValueError("Missing/ambiguous required source notice: " + relative)
                validate_notice_path(matches[0])
                selected.add(matches[0])
            if not selected:
                raise ValueError("Qt source archive contains no notices")
            module = destination / record["name"]
            module.mkdir(parents=True, exist_ok=False)
            hashes = {}
            for name in sorted(selected):
                validate_notice_path(name)
                relative = Path(*PurePosixPath(name).parts[strip_components:])
                target = module / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(members[name]) as stream, target.open("xb") as output:
                    while block := stream.read(1024 * 1024):
                        output.write(block)
                hashes[relative.as_posix()] = digest(target)
        if digest(source) != record["sha256"]:
            raise ValueError("Source notice archive changed during collection")
        inventory.append(dict(component=record["name"], source=record, files=hashes))
    (destination / "notices.json").write_text(json.dumps(dict(schema=1, components=inventory), indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_lock", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    lock = json.loads(args.source_lock.read_text(encoding="utf-8"))
    collect_qt_notices(lock["qt"]["downloads"], args.cache, args.destination)


if __name__ == "__main__":
    main()
