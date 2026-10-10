"""Attribute Qt SDK third-party libraries to pinned source archives and notices."""
import copy
import json
from pathlib import PurePosixPath
import re
from package_notices import collect_qt_notices


def sdk_source_records(lock, qt_version):
    sdk = lock["linux_sdk_dependencies"]
    if sdk["qt"] != qt_version or not sdk["downloads"]:
        raise ValueError("SDK source inputs do not match Qt")
    return sdk["downloads"]


def collect_sdk_notices(provenance, sources, cache, licenses):
    # Validate all binary mappings before creating any files or changing evidence.
    libraries = {}
    for record in provenance["libraries"]:
        libraries.setdefault(PurePosixPath(record["payload"]).name, []).append(record)
    mapped = set()
    for source in sources:
        if not source["libraries"] or not source["required_notices"]:
            raise ValueError("Incomplete SDK source mapping")
        for name, expected in source["libraries"].items():
            if name in mapped:
                raise ValueError("Duplicate SDK library source mapping: " + name)
            mapped.add(name)
            matches = libraries.get(name, [])
            record = matches[0] if len(matches) == 1 else None
            if (not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", name)
                    or not re.fullmatch(r"[a-f0-9]{64}", expected) or record is None
                    or record["provider"] != "sdk-third-party" or record.get("original_sha256") != expected):
                raise ValueError("SDK library does not match pinned source input: " + name)
    destination = licenses / "Qt-sdk-source-notices"
    collect_qt_notices(sources, cache, destination)
    inventory = json.loads((destination / "notices.json").read_text(encoding="utf-8"))
    components = {entry["component"]: entry for entry in inventory["components"]}
    for source in sources:
        if any(name not in components[source["name"]]["files"] for name in source["required_notices"]):
            raise ValueError("Missing required SDK notice: " + source["name"])
    result = copy.deepcopy(provenance)
    resolved = set()
    for record in result["libraries"]:
        name = PurePosixPath(record["payload"]).name
        for source in sources:
            if name not in source["libraries"]:
                continue
            notice = source["required_notices"][0]
            record.update(source_input=source,
                          copyright="Qt-sdk-source-notices/" + source["name"] + "/" + notice,
                          copyright_sha256=components[source["name"]]["files"][notice])
            resolved.add(record["payload"])
    result["unresolved"] = [name for name in result["unresolved"] if name not in resolved]
    result["sdk_source_notices"] = "Qt-sdk-source-notices/notices.json"
    # Matching source versions is one part of F7; build configuration remains open.
    result["corresponding_sources_complete"] = False
    return result
