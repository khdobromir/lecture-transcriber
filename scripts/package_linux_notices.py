"""Attribute deployed Linux libraries and retain their Debian copyright files."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from package_source import digest


def query(command):
    return subprocess.check_output(command, text=True, encoding="utf-8",
                                   env=dict(os.environ, LC_ALL="C.UTF-8", LANGUAGE="C"),
                                   stderr=subprocess.PIPE)


def package_owner(original):
    # Ubuntu's merged /usr can expose a different path in ldconfig and dpkg.
    paths = [original, original.resolve()]
    for path in list(paths):
        name = str(path)
        if name.startswith("/usr/lib/"):
            paths.append(Path(name[4:]))
        elif name.startswith("/lib/"):
            paths.append(Path("/usr" + name))
    for path in dict.fromkeys(paths):
        try:
            output = query(["dpkg-query", "-S", str(path)])
        except subprocess.CalledProcessError:
            continue
        owners = set()
        for line in output.splitlines():
            if ": " in line:
                owner, filename = line.rsplit(": ", 1)
                if filename == str(path):
                    owners.update(owner.split(", "))
        if len(owners) == 1:
            owner = owners.pop()
            if re.fullmatch(r"[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?", owner):
                return owner
    raise ValueError("Cannot uniquely attribute system library: " + str(original))


def collect_system_notices(appdir, licenses, qt_libs, system_root=Path("/")):
    """Record unresolved libraries explicitly; this is not a complete source offer."""
    originals = {}
    for line in query(["ldconfig", "-p"]).splitlines():
        if "x86-64" in line and " => " in line:
            name, original = line.strip().split(" => ", 1)
            originals.setdefault(name.split()[0], Path(original))
    destination = licenses / "Linux-system"
    destination.mkdir(parents=True, exist_ok=False)
    common = system_root / "usr/share/common-licenses"
    if not common.is_dir():
        raise ValueError("Missing system common license texts")
    shutil.copytree(common, destination / "common-licenses")
    records, unresolved = [], []
    for payload in sorted((appdir / "usr/lib").rglob("*")):
        if payload.is_symlink() or not payload.is_file():
            continue
        with payload.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        relative = payload.relative_to(appdir).as_posix()
        record = dict(payload=relative, payload_sha256=digest(payload), provider="unresolved")
        sdk_original = Path(qt_libs) / payload.name
        if sdk_original.is_file():
            record.update(provider="qt-sdk" if payload.name.startswith("libQt6") else "sdk-third-party",
                          original_sha256=digest(sdk_original))
            if record["provider"] == "sdk-third-party":
                unresolved.append(relative)
        elif payload.name in originals:
            original = originals[payload.name]
            record["original_sha256"] = digest(original)
            try:
                owner = package_owner(original)
                fields = query(["dpkg-query", "-W", "-f",
                                "${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n", owner]).strip().split("\t")
                if len(fields) != 4 or not all(fields) or fields[0] != owner:
                    raise ValueError("Incomplete system package metadata: " + owner)
                record.update(binary_package=fields[0], binary_version=fields[1],
                              source_package=fields[2], source_version=fields[3])
                package = owner.split(":", 1)[0]
                notice = system_root / "usr/share/doc" / package / "copyright"
                if not notice.is_file():
                    raise ValueError("Missing system package copyright: " + owner)
                target = destination / package / "copyright"
                target.parent.mkdir(exist_ok=True)
                if not target.exists():
                    shutil.copyfile(notice, target)
                if digest(target) != digest(notice):
                    raise ValueError("System package copyright changed: " + owner)
                record.update(provider="deb", copyright=target.relative_to(licenses).as_posix(),
                              copyright_sha256=digest(target))
            except (subprocess.CalledProcessError, ValueError) as error:
                # Preserve useful partial evidence without claiming complete coverage.
                record["reason"] = str(error)
                unresolved.append(relative)
        else:
            record["reason"] = "No original library in Qt SDK or ldconfig"
            unresolved.append(relative)
        records.append(record)
    result = dict(schema=1, corresponding_sources_complete=False, libraries=records, unresolved=unresolved,
                  common_licenses={path.relative_to(licenses).as_posix(): digest(path)
                                   for path in sorted((destination / "common-licenses").rglob("*")) if path.is_file()})
    return result


def write_provenance(result, destination):
    destination.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
