"""Retain exact Ubuntu source packages for the copied system library payload.

APT indexes must include deb-src and have been authenticated by apt-get update.
Only archives are downloaded: sources are never unpacked or executed here.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
from package_linux_notices import query
from package_source import digest


def select_source(text, package, version):
    stanzas = []
    current, field = {}, None
    for line in (text + "\n\n").splitlines():
        if not line:
            if current:
                stanzas.append(current)
            current, field = {}, None
        elif line[0].isspace():
            if field is None:
                raise ValueError("Malformed APT source metadata")
            current[field] += "\n" + line.strip()
        else:
            field, separator, value = line.partition(":")
            if not separator or field in current:
                raise ValueError("Malformed APT source metadata")
            current[field] = value.strip()
    matches = [entry for entry in stanzas if entry.get("Package") == package and entry.get("Version") == version]
    if not matches:
        raise ValueError("Exact source version unavailable; enable matching deb-src indexes: " + package + "=" + version)
    selected = None
    for entry in matches:
        records, names = [], set()
        for line in entry.get("Checksums-Sha256", "").splitlines():
            if not line.strip():
                continue
            fields = line.split()
            if len(fields) != 3:
                raise ValueError("Malformed source SHA-256 entry")
            checksum, size, filename = fields
            if (not re.fullmatch(r"[a-f0-9]{64}", checksum) or not size.isascii() or not size.isdecimal()
                    or int(size) <= 0 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+~_-]*", filename)
                    or filename in names):
                raise ValueError("Unsafe/incomplete source SHA-256 entry")
            names.add(filename)
            records.append(dict(filename=filename, sha256=checksum, size=int(size)))
        if sum(record["filename"].endswith(".dsc") for record in records) != 1 or len(records) < 2:
            raise ValueError("Incomplete source package archives: " + package)
        records.sort(key=lambda record: record["filename"])
        if selected is not None and selected != records:
            raise ValueError("Conflicting source metadata: " + package + "=" + version)
        selected = records
    return selected


def verify_file(path, record):
    if (path.is_symlink() or not path.is_file() or path.stat().st_size != record["size"]
            or digest(path) != record["sha256"]):
        raise ValueError("Source checksum mismatch: " + record["filename"])


def collect_sources(provenance, cache):
    grouped = {}
    for record in provenance["libraries"] + provenance.get("data_files", []):
        if record["provider"] != "deb" and "source_package" not in record:
            continue
        package, version = record.get("source_package", ""), record.get("source_version", "")
        if (not re.fullmatch(r"[a-z0-9][a-z0-9+.-]+", package)
                or not re.fullmatch(r"[0-9][A-Za-z0-9.+:~_-]*", version)
                or not record.get("payload") or not re.fullmatch(r"[a-f0-9]{64}", record.get("payload_sha256", ""))):
            raise ValueError("Incomplete system source metadata")
        grouped.setdefault((package, version), []).append(record)
    packages, downloads, filenames = [], [], {}
    for (package, version), payloads in sorted(grouped.items()):
        metadata = query(["apt-cache", "showsrc", "--only-source", package])
        records = select_source(metadata, package, version)
        selection = package + "=" + version
        for record in records:
            previous = filenames.get(record["filename"])
            if previous is not None and previous != record:
                raise ValueError("Conflicting source archive filename: " + record["filename"])
            filenames[record["filename"]] = record
            target = cache / record["filename"]
            if target.exists() or target.is_symlink():
                verify_file(target, record)
        if any(not (cache / record["filename"]).exists() for record in records):
            with tempfile.TemporaryDirectory(prefix=".apt-sources-", dir=cache) as temporary:
                directory = Path(temporary)
                subprocess.run(["apt-get", "source", "--download-only", "--only-source", selection],
                               cwd=directory, check=True,
                               env=dict(os.environ, LC_ALL="C.UTF-8", LANGUAGE="C"))
                if {path.name for path in directory.iterdir()} != {record["filename"] for record in records}:
                    raise ValueError("Unexpected/incomplete downloaded source package: " + selection)
                for record in records:
                    verify_file(directory / record["filename"], record)
                for record in records:
                    source, target = directory / record["filename"], cache / record["filename"]
                    source.chmod(0o644)
                    try:
                        os.link(source, target)
                    except FileExistsError:
                        verify_file(target, record)
        packages.append(dict(source_package=package, source_version=version, payloads=payloads, files=records))
    for filename, record in sorted(filenames.items()):
        downloads.append(dict(record, name="ubuntu-source-" + filename, retrieval="apt-get source --download-only --only-source",
                              source_packages=[dict(name=entry["source_package"], version=entry["source_version"])
                                               for entry in packages if record in entry["files"]]))
    return dict(schema=1, corresponding_sources_complete=False, packages=packages, downloads=downloads)
