"""Attribute the Linux AppImage runtime to pinned sources and build recipes."""
import json
import re
from package_notices import collect_qt_notices
from package_source import digest


def runtime_source_records(lock, runtime):
    group = lock["linux_appimage_runtime"]
    expected = group["for_binary_sha256"]
    if not re.fullmatch(r"[a-f0-9]{64}", expected) or runtime["sha256"] != expected or not group["downloads"]:
        raise ValueError("AppImage runtime sources do not match pinned binary")
    for record in group["downloads"]:
        if record["for_binary_sha256"] != [expected]:
            raise ValueError("AppImage runtime dependency has an inconsistent binary mapping")
        if record["kind"] not in {"runtime-build-recipes", "runtime-source"}:
            raise ValueError("Unknown AppImage runtime source kind")
        if record["kind"] == "runtime-source" and not record.get("required_notices"):
            raise ValueError("AppImage runtime source requires original notices")
    return group["downloads"]


def collect_runtime_notices(lock, runtime, binary, cache, licenses):
    records = runtime_source_records(lock, runtime)
    if binary.is_symlink() or digest(binary) != runtime["sha256"]:
        raise ValueError("AppImage runtime binary checksum mismatch")
    destination = licenses / "AppImage-runtime-source-notices"
    collect_qt_notices([record for record in records if record["kind"] == "runtime-source"], cache, destination)
    evidence = dict(schema=1, binary_sha256=runtime["sha256"],
                    source_inputs=lock["linux_appimage_runtime"],
                    notices="AppImage-runtime-source-notices/notices.json",
                    corresponding_sources_complete=False)
    (licenses / "appimage-runtime-provenance.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
