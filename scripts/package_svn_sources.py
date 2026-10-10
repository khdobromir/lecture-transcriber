"""Retain raw SVN files and properties from an immutable HTTPS DAV baseline."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import http.client
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import ssl
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from package_source import digest

DAV = "{DAV:}"
SVN = "{http://subversion.tigris.org/xmlns/svn/}"
CUSTOM = "{http://subversion.tigris.org/xmlns/custom/}"
PROP = "{http://subversion.tigris.org/xmlns/dav/}"
BODY = b'<?xml version="1.0"?><D:propfind xmlns:D="DAV:"><D:allprop/></D:propfind>'
MAX_FILE = 64 * 1024 * 1024


def validate_mapping(record):
    url = urllib.parse.urlsplit(record["url"])
    if (url.scheme != "https" or not url.netloc or url.username is not None
            or url.password is not None or url.query or url.fragment
            or not re.fullmatch(r"[1-9][0-9]*", record["revision"])
            or ("/!svn/bc/" + record["revision"] + "/") not in url.path
            or not url.path.endswith("/") or ".." in PurePosixPath(urllib.parse.unquote(url.path)).parts
            or "\\" in urllib.parse.unquote(url.path) or "\0" in urllib.parse.unquote(url.path)
            or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", record["svn_archive_prefix"])
            or not re.fullmatch(r"[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}", record["svn_repository_uuid"])
            or not re.fullmatch(r"[a-f0-9]{64}", record["sha256"])):
        raise ValueError("Invalid SVN source mapping")
    return url


def create_svn_archive(record, destination):
    base = validate_mapping(record)
    root_properties = {}

    def request(relative, method="GET"):
        url = record["url"] + urllib.parse.quote(relative, safe="/")
        headers = {"User-Agent": "Transcribe-package/1"}
        if method == "PROPFIND":
            headers.update(Depth="1", **{"Content-Type": "text/xml"})
        for attempt in range(3):
            try:
                req = urllib.request.Request(url, method=method, headers=headers,
                                             data=BODY if method == "PROPFIND" else None)
                with urllib.request.urlopen(req, timeout=120) as response:
                    if response.geturl() != url:
                        raise ValueError("SVN snapshot redirect is not allowed")
                    data = response.read(MAX_FILE + 1)
                    if len(data) > MAX_FILE:
                        raise ValueError("SVN response exceeds size limit")
                    return data
            except urllib.error.HTTPError as error:
                status = error.code; error.close()
                if status not in {502, 503, 504} or attempt == 2:
                    raise RuntimeError(f"SVN source download failed: HTTP {status} for {relative}") from error
                time.sleep(attempt + 1)
            except (urllib.error.URLError, http.client.IncompleteRead, ConnectionResetError, TimeoutError) as error:
                reason = error.reason if isinstance(error, urllib.error.URLError) else error
                if (not isinstance(reason, (ssl.SSLEOFError, http.client.IncompleteRead, ConnectionResetError, TimeoutError))
                        or attempt == 2):
                    raise RuntimeError("SVN source transport failed for " + relative) from error
                time.sleep(attempt + 1)

    def list_directory(relative):
        xml = request(relative, "PROPFIND")
        if b"<!DOCTYPE" in xml.upper() or b"<!ENTITY" in xml.upper():
            raise ValueError("Unsupported SVN XML declarations")
        tree = ET.fromstring(xml)
        if tree.tag != DAV + "multistatus":
            raise ValueError("Invalid SVN directory response")
        rows = {}; seen_self = False; names = set()
        for item in tree.findall(DAV + "response"):
            href = urllib.parse.urlsplit(item.findtext(DAV + "href", ""))
            if ((href.netloc and href.netloc != base.netloc) or (href.scheme and href.scheme != "https")
                    or href.query or href.fragment or not href.path.startswith(base.path)):
                raise ValueError("Unsafe SVN response path")
            name = urllib.parse.unquote(href.path[len(base.path):], errors="strict")
            path = PurePosixPath(name)
            if ((name and not path.parts) or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name or "\0" in name
                    or (name and path.as_posix() != name.rstrip("/"))):
                raise ValueError("Unsafe SVN source path")
            good = [p for p in item.findall(DAV + "propstat") if p.findtext(DAV + "status") == "HTTP/1.1 200 OK"]
            if len(good) != 1:
                raise ValueError("Invalid SVN property status")
            props = good[0].find(DAV + "prop")
            version = props.findtext(DAV + "version-name", "") if props is not None else ""
            if (props is None or props.findtext(PROP + "repository-uuid") != record["svn_repository_uuid"]
                    or not re.fullmatch(r"[1-9][0-9]*", version) or int(version) > int(record["revision"])):
                raise ValueError("SVN repository/revision mismatch")
            if props.find(SVN + "special") is not None or props.find(SVN + "externals") is not None:
                raise ValueError("Unsupported SVN special file or externals")
            directory = props.find(DAV + "resourcetype/" + DAV + "collection") is not None
            if name == relative:
                if seen_self or not directory:
                    raise ValueError("Invalid SVN directory identity")
                seen_self = True
                if not relative:
                    root_properties.update({p.tag: p.text or "" for p in props if p.tag.startswith((SVN, CUSTOM))})
                continue
            if not name or path.parent != PurePosixPath(relative) or path.as_posix() in names:
                raise ValueError("Unsafe/duplicate SVN directory child")
            names.add(path.as_posix())
            row = dict(directory=directory, mode=0o755 if directory or props.find(SVN + "executable") is not None else 0o644,
                       properties={p.tag: p.text or "" for p in props if p.tag.startswith((SVN, CUSTOM))})
            if not directory:
                size = props.findtext(DAV + "getcontentlength", "")
                sha1 = props.findtext(PROP + "sha1-checksum", "")
                if not size.isdecimal() or int(size) > MAX_FILE or not re.fullmatch(r"[a-f0-9]{40}", sha1):
                    raise ValueError("Invalid SVN file metadata")
                row.update(size=int(size), sha1=sha1)
            rows[path.as_posix() + ("/" if directory else "")] = row
        if not seen_self:
            raise ValueError("Missing SVN directory identity")
        return rows

    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="svn-files-") as temporary:
        root = Path(temporary); entries = {}; pending = [""]
        with ThreadPoolExecutor(max_workers=4) as pool:
            def results(function, items):
                try:
                    yield from pool.map(function, items)
                except BaseException:
                    pool.shutdown(wait=True, cancel_futures=True)
                    raise

            while pending:
                following = []
                for rows in results(list_directory, pending):
                    if entries.keys() & rows.keys():
                        raise ValueError("Duplicate SVN source entry")
                    entries.update(rows)
                    following.extend(name for name, row in rows.items() if row["directory"])
                if len(entries) > 100000:
                    raise ValueError("SVN snapshot exceeds entry limit")
                pending = following
            if sum(row.get("size", 0) for row in entries.values()) > 512 * 1024 * 1024:
                raise ValueError("SVN snapshot exceeds total size limit")

            def fetch_file(item):
                name, row = item; data = request(name)
                if len(data) != row["size"] or hashlib.sha1(data).hexdigest() != row["sha1"]:
                    raise ValueError("SVN file checksum mismatch: " + name)
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)

            list(results(fetch_file, [(name, row) for name, row in entries.items() if not row["directory"]]))
        metadata_name = ".transcribe-svn-properties.json"
        if metadata_name in entries or metadata_name + "/" in entries:
            raise ValueError("SVN snapshot metadata path collision")
        metadata = json.dumps(dict(schema=1, repository_uuid=record["svn_repository_uuid"], revision=record["revision"],
                                   snapshot_url=record["url"], raw_repository_bytes=True, root_properties=root_properties, entries=entries),
                              sort_keys=True, indent=2).encode() + b"\n"
        entries[metadata_name] = dict(directory=False, size=len(metadata), mode=0o644)
        with destination.open("xb") as output, tarfile.open(fileobj=output, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name, row in sorted(entries.items()):
                member = tarfile.TarInfo(record["svn_archive_prefix"] + "/" + name.rstrip("/"))
                member.mode = row["mode"]
                member.type = tarfile.DIRTYPE if row["directory"] else tarfile.REGTYPE
                member.size = row.get("size", 0)
                if row["directory"]:
                    archive.addfile(member)
                elif name == metadata_name:
                    archive.addfile(member, io.BytesIO(metadata))
                else:
                    with (root / name).open("rb") as stream:
                        archive.addfile(member, stream)


def fetch_svn_source(record, destination):
    validate_mapping(record)
    if destination.exists() or destination.is_symlink():
        if destination.is_symlink() or not destination.is_file() or digest(destination) != record["sha256"]:
            raise ValueError("SVN source cache checksum mismatch")
        return destination
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix="svn-source-") as temporary:
        archive = Path(temporary) / "input.tar"
        create_svn_archive(record, archive)
        if digest(archive) != record["sha256"]:
            raise ValueError("SVN source snapshot checksum mismatch")
        try:
            os.link(archive, destination)
        except FileExistsError:
            if destination.is_symlink() or not destination.is_file() or digest(destination) != record["sha256"]:
                raise ValueError("SVN source cache checksum mismatch")
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("revision")
    parser.add_argument("uuid")
    parser.add_argument("prefix")
    parser.add_argument("destination", type=Path)
    parser.add_argument("sha256")
    args = parser.parse_args()
    fetch_svn_source(dict(url=args.url, revision=args.revision, svn_repository_uuid=args.uuid,
                          svn_archive_prefix=args.prefix, sha256=args.sha256), args.destination)


if __name__ == "__main__":
    main()
