"""Read notices from a pinned PyInstaller standalone without executing its code."""
import argparse
import email
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zlib


def collect_notices(binary, expected, destination):
    content = binary.read_bytes()
    if hashlib.sha256(content).hexdigest() != expected:
        raise ValueError("Standalone notice binary checksum mismatch")
    # PyInstaller 6 CArchive layout, documented in archive/readers.py and
    # archive/writers.py. Never unmarshal the embedded Python code objects.
    cookie = struct.Struct("!8sIIII64s")
    entry_header = struct.Struct("!IIIIBc")
    position = content.rfind(b"MEI\x0c\x0b\x0a\x0b\x0e")
    if position < 0 or position + cookie.size > len(content):
        raise ValueError("Missing standalone archive cookie")
    _, length, toc_offset, toc_length, python_version, python_library = cookie.unpack_from(content, position)
    start = position + cookie.size - length
    if start < 0 or toc_offset + toc_length != length - cookie.size:
        raise ValueError("Invalid standalone archive bounds")
    toc = content[start + toc_offset:start + toc_offset + toc_length]
    offset, total = 0, 0
    inventory, files, distributions, seen = [], {}, [], set()
    while offset < len(toc):
        if offset + entry_header.size > len(toc):
            raise ValueError("Truncated standalone archive entry")
        size, data_offset, compressed, uncompressed, flag, kind = entry_header.unpack_from(toc, offset)
        if size <= entry_header.size or offset + size > len(toc) or data_offset + compressed > toc_offset or flag not in (0, 1):
            raise ValueError("Invalid standalone archive entry bounds")
        name = toc[offset + entry_header.size:offset + size].rstrip(b"\0").decode("utf-8")
        offset += size
        if "\0" in name:
            raise ValueError("Invalid standalone archive entry name")
        normalized = name.replace("\\", "/")
        if kind != b"o":
            if normalized.casefold() in seen:
                raise ValueError("Duplicate standalone archive entry")
            seen.add(normalized.casefold())
        inventory.append(dict(name=name, type=kind.decode("ascii"), size=uncompressed))
        path = PurePosixPath(normalized)
        selected = (path.name.lower().startswith(("license", "copying", "copyright", "notice", "third_party_license"))
                    or "licenses" in [part.lower() for part in path.parts[:-1]]
                    or (path.name == "METADATA" and any(part.endswith(".dist-info") for part in path.parts)))
        if not selected:
            continue
        if (path.is_absolute() or ".." in path.parts or ":" in normalized or not path.parts
                or path.as_posix() != normalized or kind not in (b"x", b"b")):
            raise ValueError("Unsafe standalone notice path or type")
        total += uncompressed
        if uncompressed > 16 * 1024 * 1024 or total > 64 * 1024 * 1024:
            raise ValueError("Standalone notices exceed size limit")
        data = content[start + data_offset:start + data_offset + compressed]
        if flag:
            decompressor = zlib.decompressobj()
            data = decompressor.decompress(data, uncompressed + 1)
            if not decompressor.eof or decompressor.unused_data or decompressor.unconsumed_tail:
                raise ValueError("Invalid standalone notice compression")
        if len(data) != uncompressed:
            raise ValueError("Standalone notice size mismatch")
        files[path.as_posix()] = data
        if path.name == "METADATA":
            metadata = email.message_from_bytes(data)
            if not metadata["Name"] or not metadata["Version"]:
                raise ValueError("Incomplete standalone distribution metadata")
            distributions.append(dict(name=metadata["Name"], version=metadata["Version"]))
    if "THIRD_PARTY_LICENSES.txt" not in files:
        raise ValueError("Standalone archive lacks third-party notices")
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    result = dict(schema=1, binary_sha256=expected, corresponding_sources_complete=False,
                  python_version=python_version, python_library=python_library.rstrip(b"\0").decode("ascii"),
                  distributions=distributions, entries=inventory,
                  files={name: hashlib.sha256(data).hexdigest() for name, data in files.items()})
    (destination / "notices.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("sha256")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    collect_notices(args.binary, args.sha256, args.destination)


if __name__ == "__main__":
    main()
