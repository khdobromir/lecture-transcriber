"""Detect changed/committed source during packaging and reject unsafe release modes."""
from pathlib import Path
import base64
import copy
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
import tarfile
import struct
import zlib
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_source import validate_identity
from package_inputs import archive_inputs, source_records
from package_notices import collect_qt_notices


class SourceIdentityTests(unittest.TestCase):
    def test_ffmpeg_deps_sources_are_bound_without_executing_upstream_python(self):
        from package_ffmpeg_sources import verify_deps_input
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); revision = "c" * 40
            original = ("use_relative_paths = True\nvars = {'repo': 'https://example.invalid', 'revision': '"
                        + revision + "'}\ndeps = {'third_party/library': Var('repo') + '/library.git@' + Var('revision')}\n").encode()
            def parent_archive(body):
                path = root / "parent.tar"
                with tarfile.open(path, "w") as archive:
                    member = tarfile.TarInfo("parent/DEPS"); member.size = len(body)
                    archive.addfile(member, io.BytesIO(body))
                return dict(name="parent", filename=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            parent = parent_archive(original)
            record = dict(name="library", revision=revision, repository="https://example.invalid/library.git",
                          deps_input=dict(parent="parent", path="third_party/library"))
            result = verify_deps_input(record, parent, root)
            self.assertEqual(result, dict(source="library", parent="parent", parent_sha256=parent["sha256"],
                             path="third_party/library", revision=revision, deps_sha256=hashlib.sha256(original).hexdigest()))
            for changes in [dict(revision="d" * 40), dict(repository="https://example.invalid/other.git"),
                            dict(deps_input=dict(parent="parent", path="third_party/other"))]:
                previous = record.copy(); record.update(changes)
                with self.assertRaises(ValueError): verify_deps_input(record, parent, root)
                record.clear(); record.update(previous)
            marker = root / "upstream-code-executed"
            malicious = ("__import__('pathlib').Path(" + repr(str(marker)) + ").write_text('executed')").encode()
            for body in [original + malicious + b"\n", original.replace(b"Var('revision')", malicious),
                         original.replace(b"use_relative_paths = True", b"use_relative_paths = False"),
                         original + b"vars = {}\n", original.replace(b"Var('revision')", b"Var('unknown')"),
                         original.replace(b"Var('revision')", b"str(123)"),
                         original.replace(b"deps =", b"deps = None\nunused ="),
                         original + b"vars['revision'] = 'changed'\n"]:
                parent = parent_archive(body)
                with self.assertRaises(ValueError): verify_deps_input(record, parent, root)
                self.assertFalse(marker.exists())
            parent = parent_archive(original)
            (root / parent["filename"]).write_bytes(b"Corrupt source")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                verify_deps_input(record, parent, root)

    def test_ffmpeg_submodule_sources_require_verified_parent_git_objects_and_declaration(self):
        from package_ffmpeg_sources import collect_ffmpeg_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); binary = root / "ffmpeg.zip"; binary.write_bytes(b"Pinned binary")
            checksum = hashlib.sha256(binary.read_bytes()).hexdigest()
            def archive(filename, files):
                path = root / filename
                with tarfile.open(path, "w") as output:
                    for name, content in files.items():
                        member = tarfile.TarInfo(name); member.size = len(content)
                        output.addfile(member, io.BytesIO(content))
                return dict(filename=filename, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            def object_hash(kind, body):
                return hashlib.sha1(kind.encode() + b" " + str(len(body)).encode() + b"\0" + body).hexdigest()
            def fixture(mode=b"160000"):
                declaration = b'[submodule "deps/library"]\npath = deps/library\nurl = https://example.invalid/library.git\n'
                revision = "c" * 40
                subtree = mode + b" library\0" + bytes.fromhex(revision)
                subtree_sha = object_hash("tree", subtree)
                tree = (b"100644 .gitmodules\0" + bytes.fromhex(object_hash("blob", declaration))
                        + b"40000 deps\0" + bytes.fromhex(subtree_sha))
                tree_sha = object_hash("tree", tree)
                commit = ("tree " + tree_sha + "\nauthor Example <example@example.invalid> 0 +0000\n"
                          "committer Example <example@example.invalid> 0 +0000\n\nPinned parent\n").encode()
                parent = archive("parent.tar", {"parent/LICENSE": b"Parent license", "parent/.gitmodules": declaration})
                parent.update(name="parent", kind="ffmpeg-dependency-source", revision=object_hash("commit", commit),
                              repository="https://example.invalid/parent.git", recipe="scripts.d/parent.sh", recipe_slot="",
                              configure_flags=["--enable-parent"], required_notices=["LICENSE"], for_binary_sha256=[checksum])
                recipe = archive("recipes.tar", {"build/scripts.d/parent.sh":
                    ('SCRIPT_REPO="' + parent["repository"] + '"\nSCRIPT_COMMIT="' + parent["revision"] + '"\n').encode()})
                recipe.update(name="ffmpeg-build-recipes", for_binary_sha256=[checksum])
                child = parent | archive("library.tar", {"library/LICENSE": b"Original nested library license"})
                child.update(name="library", revision=revision, repository="https://example.invalid/library.git",
                             git_submodule=dict(parent="parent", path="deps/library",
                                                commit_base64=base64.b64encode(commit).decode(),
                                                trees_base64={tree_sha: base64.b64encode(tree).decode(),
                                                              subtree_sha: base64.b64encode(subtree).decode()}))
                lock = dict(downloads=[dict(name="ffmpeg-source", for_binary_sha256=[checksum]), recipe],
                            ffmpeg_dependencies=dict(for_binary_sha256=[checksum], downloads=[parent, child]))
                return lock, parent, child
            pinned = dict(filename=binary.name, sha256=checksum)
            lock, parent, child = fixture()
            result = collect_ffmpeg_notices(lock, pinned, root, root / "licenses", "--enable-parent")
            self.assertEqual(result["git_submodules"], [dict(source="library", parent="parent",
                             parent_sha256=parent["sha256"], path="deps/library", revision=child["revision"],
                             declaration_sha256=hashlib.sha256(
                                 b'[submodule "deps/library"]\npath = deps/library\nurl = https://example.invalid/library.git\n').hexdigest())])
            self.assertEqual((root / "licenses/FFmpeg-dependency-source-notices/library/LICENSE").read_bytes(),
                             b"Original nested library license")
            module = child["git_submodule"]
            mutations = [dict(revision="d" * 40), dict(repository="https://example.invalid/other.git"),
                         dict(configure_flags=["--enable-other"]),
                         dict(git_submodule=module | dict(parent="missing")),
                         dict(git_submodule=module | dict(path="../escape")),
                         dict(git_submodule=module | dict(path="deps/other")),
                         dict(git_submodule=module | dict(commit_base64=base64.b64encode(b"Changed commit").decode())),
                         dict(git_submodule=module | dict(commit_base64="invalid base64!")),
                         dict(git_submodule=module | dict(trees_base64={})),
                         dict(git_submodule=module | dict(trees_base64={key: base64.b64encode(b"Changed tree").decode()
                                                                      for key in module["trees_base64"]}))]
            for changes in mutations:
                original = copy.deepcopy(child); child.update(changes)
                with self.assertRaises(ValueError):
                    collect_ffmpeg_notices(lock, pinned, root, root / "rejected", "--enable-parent")
                self.assertFalse((root / "rejected").exists())
                child.clear(); child.update(original)
            parent.update(archive("parent.tar", {"parent/LICENSE": b"Parent license", "parent/.gitmodules": b"Changed declaration"}))
            with self.assertRaisesRegex(ValueError, "declaration does not match"):
                collect_ffmpeg_notices(lock, pinned, root, root / "stale", "--enable-parent")
            self.assertFalse((root / "stale").exists())
            lock, parent, child = fixture(mode=b"100644")
            with self.assertRaisesRegex(ValueError, "not a Git link"):
                collect_ffmpeg_notices(lock, pinned, root, root / "ordinary-file", "--enable-parent")
            self.assertFalse((root / "ordinary-file").exists())

    def test_generated_source_selection_retains_code_without_model_checkpoints(self):
        from package_canonical_sources import canonicalize_tar, canonicalize_verified_tar
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / "upstream.tar.gz"
            files = {"dnn/generated.c": b"Original generated source", "dnn/generated.h": b"Original header",
                     "dnn/models/checkpoint.pth": b"Excluded model checkpoint"}
            with tarfile.open(source, "w:gz") as archive:
                for name, content in files.items():
                    member = tarfile.TarInfo(name); member.size = len(content)
                    archive.addfile(member, io.BytesIO(content))
            selected = ["dnn/generated.c", "dnn/generated.h"]
            expected = root / "expected.tar"
            canonicalize_tar(source, expected, selected)
            checksum = hashlib.sha256(expected.read_bytes()).hexdigest()
            target = root / "retained.tar"
            canonicalize_verified_tar(source, target, checksum, selected)
            with self.assertRaisesRegex(ValueError, "Upstream source checksum"):
                canonicalize_verified_tar(source, root / "wrong-upstream.tar", checksum, selected, "b" * 64)
            self.assertFalse((root / "wrong-upstream.tar").exists())
            with tarfile.open(target) as archive:
                self.assertEqual(archive.getnames(), selected)
                for name in selected: self.assertEqual(archive.extractfile(name).read(), files[name])
            for paths in [[], ["../escape"], ["dnn/*.c"], ["--option"], ["dnn/generated.c"] * 2,
                          ["missing.c"], ["dnn"], "dnn/generated.c"]:
                with self.assertRaises(ValueError):
                    canonicalize_verified_tar(source, root / "rejected.tar", checksum, paths)
                self.assertFalse((root / "rejected.tar").exists())
            cli = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/package_canonical_sources.py"),
                                  str(source), str(root / "cli.tar"), checksum,
                                  "--path", selected[0], "--path", selected[1]], capture_output=True, text=True)
            self.assertEqual(cli.returncode, 0, cli.stderr)
            self.assertEqual((root / "cli.tar").read_bytes(), target.read_bytes())

    def test_opus_generated_input_requires_pinned_parent_and_upstream_reference(self):
        from package_ffmpeg_sources import collect_ffmpeg_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); binary = root / "ffmpeg.zip"; binary.write_bytes(b"Pinned binary")
            checksum = hashlib.sha256(binary.read_bytes()).hexdigest(); data_sha = "d" * 64
            def archive(filename, files):
                path = root / filename
                with tarfile.open(path, "w") as output:
                    for name, content in files.items():
                        member = tarfile.TarInfo(name); member.size = len(content)
                        output.addfile(member, io.BytesIO(content))
                return dict(filename=filename, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            recipe = archive("recipes.tar", {"build/scripts.d/opus.sh":
                ('SCRIPT_REPO="https://github.com/xiph/opus.git"\nSCRIPT_COMMIT="' + "a" * 40 + '"\n').encode()})
            recipe.update(name="ffmpeg-build-recipes", for_binary_sha256=[checksum])
            source_files = {"opus/LICENSE": b"Original license", "opus/autogen.sh":
                ('dnn/download_model.sh "' + data_sha + '"\n').encode(), "opus/dnn/download_model.sh":
                b'model=opus_data-$1.tar.gz\ncurl https://media.xiph.org/opus/models/$model\n'}
            parent = archive("opus.tar", source_files)
            parent.update(name="opus", kind="ffmpeg-dependency-source", revision="a" * 40,
                          repository="https://github.com/xiph/opus.git", recipe="scripts.d/opus.sh", recipe_slot="",
                          configure_flags=["--enable-libopus"], required_notices=["LICENSE"], for_binary_sha256=[checksum])
            child = parent | archive("generated.tar", {"dnn/generated.h": b"Original generated header"})
            child.update(name="opus-generated", canonical_tar=True, canonical_tar_paths=["dnn/generated.h"],
                         upstream_sha256=data_sha, strip_components=0, required_notices=["dnn/generated.h"],
                         generated_input=dict(kind="opus-dnn", parent="opus"),
                         url="https://media.xiph.org/opus/models/opus_data-" + data_sha + ".tar.gz")
            lock = dict(downloads=[dict(name="ffmpeg-source", for_binary_sha256=[checksum]), recipe],
                        ffmpeg_dependencies=dict(for_binary_sha256=[checksum], downloads=[parent, child]))
            pinned = dict(filename=binary.name, sha256=checksum)
            result = collect_ffmpeg_notices(lock, pinned, root, root / "licenses", "--enable-libopus")
            self.assertEqual(result["generated_inputs"][0]["parent_sha256"], parent["sha256"])
            self.assertEqual((root / "licenses/FFmpeg-dependency-source-notices/opus-generated/dnn/generated.h").read_bytes(),
                             b"Original generated header")
            for changes in [dict(revision="b" * 40), dict(generated_input=dict(kind="opus-dnn", parent="missing")),
                            dict(generated_input=dict(kind="unknown", parent="opus")), dict(canonical_tar=False),
                            dict(canonical_tar_paths=["dnn/models/checkpoint.pth"]), dict(upstream_sha256="e" * 64),
                            dict(url="https://example.invalid/changed.tar.gz")]:
                previous = child.copy(); child.update(changes)
                with self.assertRaises(ValueError):
                    collect_ffmpeg_notices(lock, pinned, root, root / "rejected", "--enable-libopus")
                self.assertFalse((root / "rejected").exists())
                child.clear(); child.update(previous)
            source_files["opus/autogen.sh"] = b'dnn/download_model.sh "' + b"f" * 64 + b'"\n'
            parent.update(archive("opus.tar", source_files))
            with self.assertRaisesRegex(ValueError, "does not match pinned source"):
                collect_ffmpeg_notices(lock, pinned, root, root / "stale", "--enable-libopus")
            self.assertFalse((root / "stale").exists())

    def test_svn_sources_pin_raw_bytes_modes_properties_and_cache(self):
        from package_svn_sources import fetch_svn_source, create_svn_archive
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            record = dict(url="https://example.invalid/svn/!svn/bc/12/trunk/tool/", revision="12",
                          svn_snapshot=True, svn_archive_prefix="tool-12",
                          svn_repository_uuid="981c7233-bbb0-e711-95a9-001517a2e1a4", sha256="a" * 64)
            data = b"Original source\n"; checksum = hashlib.sha1(data).hexdigest()
            base = "/svn/!svn/bc/12/trunk/tool/"
            def listing(name="LICENSE", uuid=None, special="", version="12"):
                uuid = uuid or record["svn_repository_uuid"]
                return (f'<D:multistatus xmlns:D="DAV:" xmlns:V="http://subversion.tigris.org/xmlns/dav/" '
                    f'xmlns:S="http://subversion.tigris.org/xmlns/svn/"><D:response><D:href>{base}</D:href>'
                    f'<D:propstat><D:prop><D:resourcetype><D:collection/></D:resourcetype><V:repository-uuid>{uuid}</V:repository-uuid>'
                    f'<D:version-name>{version}</D:version-name></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>'
                    f'<D:response><D:href>{base}{name}</D:href><D:propstat><D:prop><D:resourcetype/>'
                    f'<D:version-name>8</D:version-name><V:repository-uuid>{uuid}</V:repository-uuid>'
                    f'<D:getcontentlength>{len(data)}</D:getcontentlength><V:sha1-checksum>{checksum}</V:sha1-checksum>'
                    f'<S:executable>*</S:executable><S:eol-style>native</S:eol-style>{special}'
                    '</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>').encode()
            xml = listing(); body = data; redirected = False
            def request(req, **kwargs):
                response = io.BytesIO(xml if req.get_method() == "PROPFIND" else body)
                response.geturl = lambda: "http://example.invalid/changed" if redirected else req.full_url
                return response
            source = root / "expected.tar"
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                create_svn_archive(record, source)
            with tarfile.open(source) as archive:
                member = archive.getmember("tool-12/LICENSE")
                self.assertEqual(member.mode, 0o755)
                self.assertEqual(archive.extractfile(member).read(), data)
                metadata = json.load(archive.extractfile("tool-12/.transcribe-svn-properties.json"))
                self.assertEqual(metadata["entries"]["LICENSE"]["properties"]["{http://subversion.tigris.org/xmlns/svn/}eol-style"], "native")
            record["sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
            target = root / "verified.tar"
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                fetch_svn_source(record, target)
            self.assertEqual(target.read_bytes(), source.read_bytes())
            target.unlink()
            import ssl
            import urllib.error
            calls = []
            def transient(req, **kwargs):
                calls.append(req.full_url)
                if len(calls) == 1:
                    raise urllib.error.URLError(ssl.SSLEOFError("Unexpected TLS EOF"))
                return request(req, **kwargs)
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=transient), patch("package_svn_sources.time.sleep") as sleep:
                fetch_svn_source(record, target)
            self.assertEqual(len(calls), 3)
            sleep.assert_called_once_with(1)
            target.unlink()
            for reason, expected_calls in [(ssl.SSLEOFError("Unexpected TLS EOF"), 3),
                                            (ssl.SSLCertVerificationError("Certificate rejected"), 1)]:
                with patch("package_svn_sources.urllib.request.urlopen", side_effect=urllib.error.URLError(reason)) as network, patch("package_svn_sources.time.sleep"):
                    with self.assertRaisesRegex(RuntimeError, "transport failed"):
                        fetch_svn_source(record, target)
                self.assertEqual(network.call_count, expected_calls)
                self.assertFalse(target.exists())
                self.assertFalse(any(p.name.startswith("svn-source-") for p in root.iterdir()))
            target.write_bytes(source.read_bytes())
            with patch("package_svn_sources.urllib.request.urlopen") as network:
                fetch_svn_source(record, target)
            network.assert_not_called()
            target.write_bytes(b"Previous cache")
            with self.assertRaisesRegex(ValueError, "cache checksum"):
                fetch_svn_source(record, target)
            self.assertEqual(target.read_bytes(), b"Previous cache"); target.unlink()
            for changed in [listing(name="../escape"), listing(name="."), listing(name="LICENSE", uuid="wrong"),
                            listing(special="<S:special>*</S:special>"), listing(version="13"),
                            b'<!DOCTYPE x [<!ENTITY y "z">]><x/>']:
                xml = changed
                with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                    with self.assertRaises(ValueError): fetch_svn_source(record, target)
                self.assertFalse(target.exists())
                self.assertFalse(any(p.name.startswith("svn-source-") for p in root.iterdir()))
            xml = listing(); body = b"Changed source"
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                with self.assertRaisesRegex(ValueError, "file checksum"): fetch_svn_source(record, target)
            body = data; redirected = True
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                with self.assertRaisesRegex(ValueError, "redirect"): fetch_svn_source(record, target)
            redirected = False
            with patch("package_svn_sources.urllib.request.urlopen", side_effect=request):
                with self.assertRaisesRegex(ValueError, "snapshot checksum"):
                    fetch_svn_source(record | dict(sha256="b" * 64), target)
            self.assertFalse(target.exists())
            for changes in [dict(url="http://example.invalid/"), dict(revision="HEAD"),
                            dict(url=record["url"].replace("/12/", "/13/")), dict(svn_archive_prefix="../escape")]:
                with patch("package_svn_sources.urllib.request.urlopen") as network:
                    with self.assertRaises(ValueError): fetch_svn_source(record | changes, target)
                network.assert_not_called()

    def test_git_sources_verify_revision_and_archive_before_publication(self):
        from package_git_sources import fetch_git_source
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); cache = root / "cache"; cache.mkdir()
            archive = root / "source.tar"
            with tarfile.open(archive, "w") as package:
                member = tarfile.TarInfo("codec/LICENSE"); member.size = 14
                package.addfile(member, io.BytesIO(b"Original terms"))
            original = archive.read_bytes()
            record = dict(url="https://example.invalid/codec.git", revision="a" * 40,
                          git_archive_prefix="codec", sha256=hashlib.sha256(original).hexdigest())
            returned_revision = record["revision"]; archive_bytes = original
            def git(command, **kwargs):
                environment = kwargs["env"]
                self.assertNotIn("GIT_DIR", environment)
                self.assertNotIn("GIT_TRACE", environment)
                self.assertNotIn("GIT_CONFIG_COUNT", environment)
                self.assertEqual(environment["GIT_CONFIG_NOSYSTEM"], "1")
                self.assertEqual(environment["GIT_TERMINAL_PROMPT"], "0")
                self.assertEqual(Path(environment["GIT_CONFIG_GLOBAL"]).read_bytes(), b"")
                if "archive" in command:
                    if "git_archive_paths" in record:
                        self.assertEqual(command[command.index("--") + 1:], record["git_archive_paths"])
                    target = next(a[len("--output="):] for a in command if a.startswith("--output="))
                    Path(target).write_bytes(archive_bytes)
                output = (returned_revision + "\n").encode() if "rev-parse" in command else b""
                return subprocess.CompletedProcess(command, 0, stdout=output, stderr=b"")
            target = cache / "source.tar"
            with patch.dict(os.environ, {"GIT_DIR": "/user/repository", "GIT_TRACE": "user-trace", "GIT_CONFIG_COUNT": "1"}), patch("package_git_sources.subprocess.run", side_effect=git):
                fetch_git_source(record, target)
            self.assertEqual(target.read_bytes(), original)
            self.assertEqual(list(cache.iterdir()), [target])
            target.unlink(); record["git_archive_paths"] = ["LICENSE.txt", "amf/public/include"]
            with patch("package_git_sources.subprocess.run", side_effect=git):
                fetch_git_source(record, target)
            record.pop("git_archive_paths")
            with patch("package_git_sources.subprocess.run") as run:
                fetch_git_source(record, target)
            run.assert_not_called()
            target.write_bytes(b"Previous input")
            with self.assertRaisesRegex(ValueError, "cache checksum mismatch"):
                fetch_git_source(record, target)
            self.assertEqual(target.read_bytes(), b"Previous input")
            target.unlink()
            returned_revision = "b" * 40
            with patch("package_git_sources.subprocess.run", side_effect=git):
                with self.assertRaisesRegex(ValueError, "commit mismatch"):
                    fetch_git_source(record, target)
            self.assertEqual(list(cache.iterdir()), [])
            returned_revision = record["revision"]; archive_bytes = b"Changed archive"
            with patch("package_git_sources.subprocess.run", side_effect=git):
                with self.assertRaisesRegex(ValueError, "source checksum mismatch"):
                    fetch_git_source(record, target)
            self.assertEqual(list(cache.iterdir()), [])
            archive_bytes = original
            def concurrent_git(command, **kwargs):
                result = git(command, **kwargs)
                if "archive" in command:
                    target.write_bytes(b"Concurrent previous input")
                return result
            with patch("package_git_sources.subprocess.run", side_effect=concurrent_git):
                with self.assertRaisesRegex(ValueError, "cache checksum mismatch"):
                    fetch_git_source(record, target)
            self.assertEqual(target.read_bytes(), b"Concurrent previous input")
            target.unlink()
            with patch("package_git_sources.subprocess.run", side_effect=[
                    subprocess.CompletedProcess([], 0, stdout=b"", stderr=b""),
                    subprocess.CompletedProcess([], 128, stdout=b"", stderr=b"Network failure")]):
                with self.assertRaisesRegex(RuntimeError, "Network failure"):
                    fetch_git_source(record, target)
            self.assertEqual(list(cache.iterdir()), [])
            for changes in [dict(url="http://example.invalid/source.git"), dict(url="https://user:secret@example.invalid/source.git"),
                            dict(revision="--bad-option"), dict(git_archive_prefix="../escape"),
                            dict(git_archive_paths=["../escape"]), dict(git_archive_paths=["--option"]),
                            dict(git_archive_paths=["*.h"]), dict(git_archive_paths=[]),
                            dict(git_archive_paths=["LICENSE", "LICENSE"]), dict(git_archive_paths="LICENSE")]:
                with patch("package_git_sources.subprocess.run") as run:
                    with self.assertRaises(ValueError):
                        fetch_git_source(record | changes, target)
                run.assert_not_called()

    def test_canonical_source_archives_pin_content_modes_and_paths(self):
        from package_canonical_sources import canonicalize_tar, canonicalize_verified_tar
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def fixture(name, timestamp, content=b"Original terms", mode=0o644, member_name="LICENSE", kind=tarfile.REGTYPE):
                path = root / name
                with tarfile.open(path, "w:gz", format=tarfile.PAX_FORMAT) as archive:
                    member = tarfile.TarInfo(member_name); member.size = len(content) if kind == tarfile.REGTYPE else 0
                    member.mtime = timestamp; member.uid = 123; member.gid = 456
                    member.uname = "builder"; member.gname = "group"; member.mode = mode; member.type = kind
                    archive.addfile(member, io.BytesIO(content) if member.isfile() else None)
                return path
            first = fixture("first.tar.gz", 1234.125)
            later = fixture("later.tar.gz", 9876.875)
            canonical = root / "expected.tar"
            canonicalize_tar(first, canonical)
            expected = hashlib.sha256(canonical.read_bytes()).hexdigest()
            target = root / "verified.tar"
            canonicalize_verified_tar(later, target, expected)
            self.assertEqual(target.read_bytes(), canonical.read_bytes())
            previous = root / "previous.tar"; previous.write_bytes(b"Previous input")
            with self.assertRaisesRegex(ValueError, "cache checksum mismatch"):
                canonicalize_verified_tar(later, previous, expected)
            self.assertEqual(previous.read_bytes(), b"Previous input")
            with tarfile.open(target) as archive:
                member = archive.getmember("LICENSE")
                self.assertEqual(archive.extractfile(member).read(), b"Original terms")
                self.assertEqual((member.mtime, member.uid, member.gid, member.uname, member.gname), (0, 0, 0, "", ""))
                self.assertEqual(member.mode, 0o644)
            for content, mode in [(b"Changed terms", 0o644), (b"Original terms", 0o755)]:
                changed = fixture("changed.tar.gz", 1234.125, content=content, mode=mode)
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    canonicalize_verified_tar(changed, root / "rejected.tar", expected)
                self.assertFalse((root / "rejected.tar").exists())
            for name, kind in [("../LICENSE", tarfile.REGTYPE), ("/LICENSE", tarfile.REGTYPE),
                               ("C:/LICENSE", tarfile.REGTYPE), ("dir\\LICENSE", tarfile.REGTYPE),
                               ("link", tarfile.SYMTYPE), ("device", tarfile.CHRTYPE)]:
                unsafe = fixture("unsafe.tar.gz", 1, member_name=name, kind=kind)
                with self.assertRaises(ValueError):
                    canonicalize_verified_tar(unsafe, root / "unsafe.tar", expected)
                self.assertFalse((root / "unsafe.tar").exists())
            duplicate = root / "duplicate.tar.gz"
            with tarfile.open(duplicate, "w:gz") as archive:
                for _ in range(2):
                    member = tarfile.TarInfo("LICENSE"); member.size = 1
                    archive.addfile(member, io.BytesIO(b"x"))
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                canonicalize_verified_tar(duplicate, root / "duplicate.tar", expected)
            cli = subprocess.run([sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/package_canonical_sources.py"),
                                  str(later), str(root / "cli.tar"), expected], capture_output=True, text=True)
            self.assertEqual(cli.returncode, 0, cli.stderr)
            self.assertEqual((root / "cli.tar").read_bytes(), canonical.read_bytes())

    def test_ffmpeg_dependency_notices_require_exact_archive_recipe_and_configuration(self):
        from package_ffmpeg_sources import collect_ffmpeg_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "ffmpeg.zip"; binary.write_bytes(b"pinned FFmpeg input")
            checksum = hashlib.sha256(binary.read_bytes()).hexdigest()
            def archive(filename, files):
                path = root / filename
                with tarfile.open(path, "w") as output:
                    for name, content in files.items():
                        member = tarfile.TarInfo(name); member.size = len(content)
                        output.addfile(member, io.BytesIO(content))
                return dict(filename=filename, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            recipe = archive("recipes.tar", {"build/scripts.d/tool.sh":
                ('SCRIPT_REPO2="https://github.com/example/tool.git"\nSCRIPT_COMMIT2="' + "a" * 40 + '"\n').encode()})
            recipe.update(name="ffmpeg-build-recipes", for_binary_sha256=[checksum])
            source = archive("tool.tar", {"tool/LICENSE": b"Original terms", "tool/README": b"Further terms"})
            source.update(name="tool", kind="ffmpeg-dependency-source", revision="a" * 40,
                          repository="https://github.com/example/tool.git", recipe="scripts.d/tool.sh", recipe_slot="2",
                          configure_flags=["--enable-tool"], required_notices=["LICENSE", "README"],
                          for_binary_sha256=[checksum])
            lock = dict(downloads=[dict(name="ffmpeg-source", for_binary_sha256=[checksum]), recipe],
                        ffmpeg_dependencies=dict(for_binary_sha256=[checksum], downloads=[source]))
            pinned = dict(filename=binary.name, sha256=checksum)
            result = collect_ffmpeg_notices(lock, pinned, root, root / "licenses", "configuration: --enable-tool")
            self.assertFalse(result["corresponding_sources_complete"])
            self.assertEqual(result["ffmpeg_input_sha256"], checksum)
            self.assertEqual((root / "licenses/FFmpeg-dependency-source-notices/tool/README").read_bytes(), b"Further terms")
            original_recipe = (root / recipe["filename"]).read_bytes()
            original_checksum = recipe["sha256"]
            tagged = archive("recipes.tar", {"build/scripts.d/tool.sh":
                b'SCRIPT_REPO2="https://github.com/example/tool.git"\nSCRIPT_COMMIT2="release-1.0"\n'})
            recipe["sha256"] = tagged["sha256"]
            source["recipe_revision"] = "release-1.0"
            tagged_result = collect_ffmpeg_notices(lock, pinned, root, root / "tagged", "--enable-tool")
            self.assertEqual(tagged_result["sources"][0]["revision"], "a" * 40)
            source["recipe_revision"] = "release-2.0"
            with self.assertRaisesRegex(ValueError, "recipe does not match"):
                collect_ffmpeg_notices(lock, pinned, root, root / "wrong-tag", "--enable-tool")
            source.pop("recipe_revision")
            (root / recipe["filename"]).write_bytes(original_recipe)
            recipe["sha256"] = original_checksum
            svn_recipe = archive("recipes.tar", {"build/scripts.d/tool.sh":
                b'SCRIPT_REPO2="https://github.com/example/tool.git"\nSCRIPT_REV2="12"\n'})
            recipe["sha256"] = svn_recipe["sha256"]
            source.update(svn_snapshot=True, revision="12", svn_archive_prefix="tool-12",
                          svn_repository_uuid="981c7233-bbb0-e711-95a9-001517a2e1a4",
                          url="https://example.invalid/svn/!svn/bc/12/trunk/tool/")
            svn_result = collect_ffmpeg_notices(lock, pinned, root, root / "svn", "--enable-tool")
            self.assertEqual(svn_result["sources"][0]["revision"], "12")
            source["revision"] = "11"
            source["url"] = source["url"].replace("/12/", "/11/")
            with self.assertRaisesRegex(ValueError, "recipe does not match"):
                collect_ffmpeg_notices(lock, pinned, root, root / "wrong-svn", "--enable-tool")
            for key in ["svn_snapshot", "svn_archive_prefix", "svn_repository_uuid", "url"]: source.pop(key)
            source["revision"] = "a" * 40
            (root / recipe["filename"]).write_bytes(original_recipe)
            recipe["sha256"] = original_checksum
            with self.assertRaisesRegex(ValueError, "configuration"):
                collect_ffmpeg_notices(lock, pinned, root, root / "disabled", "--enable-tool --disable-tool")
            self.assertFalse((root / "disabled").exists())
            source["revision"] = "b" * 40
            with self.assertRaisesRegex(ValueError, "recipe does not match"):
                collect_ffmpeg_notices(lock, pinned, root, root / "wrong-commit", "--enable-tool")
            self.assertFalse((root / "wrong-commit").exists())
            source["revision"] = "a" * 40; source["recipe"] = "../escape"
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                collect_ffmpeg_notices(lock, pinned, root, root / "unsafe", "--enable-tool")
            source["recipe"] = "scripts.d/tool.sh"; source["recipe_slot"] = "2.*"
            with self.assertRaisesRegex(ValueError, "Unsafe FFmpeg recipe slot"):
                collect_ffmpeg_notices(lock, pinned, root, root / "unsafe-slot", "--enable-tool")
            source["recipe_slot"] = "2"; source["required_notices"].append("missing.txt")
            with self.assertRaisesRegex(ValueError, "required source notice"):
                collect_ffmpeg_notices(lock, pinned, root, root / "missing-notice", "--enable-tool")
            source["required_notices"].pop()
            original = (root / recipe["filename"]).read_bytes()
            (root / recipe["filename"]).write_bytes(b"changed recipes")
            with self.assertRaisesRegex(ValueError, "checksum"):
                collect_ffmpeg_notices(lock, pinned, root, root / "corrupt-recipe", "--enable-tool")
            (root / recipe["filename"]).write_bytes(original); binary.write_bytes(b"tampered input")
            with self.assertRaisesRegex(ValueError, "checksum"):
                collect_ffmpeg_notices(lock, pinned, root, root / "bad-binary", "--enable-tool")

    def test_ffmpeg_sources_reject_stale_or_unknown_platform_mappings(self):
        from package_inputs import ffmpeg_source_records
        record = dict(name="library", kind="ffmpeg-dependency-source", for_binary_sha256=["a" * 64],
                      revision="c" * 40, configure_flags=["--enable-library"], required_notices=["LICENSE"])
        lock = dict(downloads=[dict(name="ffmpeg-source", for_binary_sha256=["a" * 64, "b" * 64])],
                    ffmpeg_dependencies=dict(for_binary_sha256=["a" * 64, "b" * 64], downloads=[record]))
        self.assertEqual(ffmpeg_source_records(lock, [{"sha256": "a" * 64}]), [record])
        record["canonical_tar"] = "true"
        with self.assertRaisesRegex(ValueError, "Incomplete FFmpeg"):
            ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
        record.pop("canonical_tar")
        record.update(git_snapshot=True, git_archive_prefix="codec")
        self.assertEqual(ffmpeg_source_records(lock, [{"sha256": "a" * 64}]), [record])
        for changes in [dict(git_snapshot="true"), dict(git_archive_prefix="../escape"), dict(canonical_tar=True)]:
            previous = record.copy(); record.update(changes)
            with self.assertRaisesRegex(ValueError, "Incomplete FFmpeg"):
                ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
            record.clear(); record.update(previous)
        record["git_archive_paths"] = ["LICENSE.txt", "amf/public/include"]
        self.assertEqual(ffmpeg_source_records(lock, [{"sha256": "a" * 64}]), [record])
        record["git_archive_paths"] = ["../escape"]
        with self.assertRaises(ValueError): ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
        record.pop("git_snapshot"); record.pop("git_archive_prefix")
        record["git_archive_paths"] = ["LICENSE"]
        with self.assertRaisesRegex(ValueError, "requires a Git snapshot"):
            ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
        record.pop("git_archive_paths")
        original = record.copy()
        record.update(svn_snapshot=True, revision="12", svn_archive_prefix="tool-12",
                      svn_repository_uuid="981c7233-bbb0-e711-95a9-001517a2e1a4", sha256="e" * 64,
                      url="https://example.invalid/svn/!svn/bc/12/trunk/tool/")
        self.assertEqual(ffmpeg_source_records(lock, [{"sha256": "a" * 64}]), [record])
        for changes in [dict(svn_snapshot="true"), dict(git_snapshot=True), dict(canonical_tar=True),
                        dict(revision="HEAD"), dict(svn_archive_prefix="../escape")]:
            previous = record.copy(); record.update(changes)
            with self.assertRaises(ValueError): ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
            record.clear(); record.update(previous)
        record.clear(); record.update(original)
        with self.assertRaisesRegex(ValueError, "FFmpeg.*pinned binary"):
            ffmpeg_source_records(lock, [{"sha256": "d" * 64}])
        record["for_binary_sha256"] = ["d" * 64]
        with self.assertRaisesRegex(ValueError, "unknown binary mapping"):
            ffmpeg_source_records(lock, [{"sha256": "a" * 64}])
        record["for_binary_sha256"] = ["a" * 64]; record["required_notices"] = []
        with self.assertRaisesRegex(ValueError, "Incomplete FFmpeg"):
            ffmpeg_source_records(lock, [{"sha256": "a" * 64}])

    def test_standalone_notices_are_read_without_running_the_executable(self):
        from package_standalone_notices import collect_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def fixture(names, kind=b"x"):
                data = bytearray(); toc = bytearray()
                for name, content in names:
                    compressed = zlib.compress(content); encoded = name.encode() + b"\0"
                    toc += struct.pack("!IIIIBc", 18 + len(encoded), len(data), len(compressed), len(content), 1, kind) + encoded
                    data += compressed
                offset = len(data); data += toc
                data += struct.pack("!8sIIII64s", b"MEI\x0c\x0b\x0a\x0b\x0e", len(data) + 88, offset, len(toc), 314, b"libpython3.14.so")
                binary = root / "standalone"; binary.write_bytes(b"not executable" + data)
                return binary, hashlib.sha256(binary.read_bytes()).hexdigest()
            binary, checksum = fixture([("THIRD_PARTY_LICENSES.txt", b"Original notices"),
                                        ("demo.dist-info\\licenses\\LICENSE", b"Dependency terms"),
                                        ("demo.dist-info/METADATA", b"Name: demo\nVersion: 1.0\n"),
                                        ("executable.pyc", b"never execute")])
            result = collect_notices(binary, checksum, root / "notices")
            self.assertEqual((root / "notices/THIRD_PARTY_LICENSES.txt").read_bytes(), b"Original notices")
            self.assertEqual((root / "notices/demo.dist-info/licenses/LICENSE").read_bytes(), b"Dependency terms")
            self.assertFalse((root / "notices/executable.pyc").exists())
            self.assertEqual(result["binary_sha256"], checksum)
            self.assertEqual(result["distributions"], [{"name": "demo", "version": "1.0"}])
            # The Windows release labels these plain data files as BINARY.
            binary, checksum = fixture([("THIRD_PARTY_LICENSES.txt", b"Windows terms")], kind=b"b")
            collect_notices(binary, checksum, root / "windows-notices")
            self.assertEqual((root / "windows-notices/THIRD_PARTY_LICENSES.txt").read_bytes(), b"Windows terms")
            with self.assertRaisesRegex(ValueError, "checksum"):
                collect_notices(binary, "0" * 64, root / "corrupt")
            for names in [[("../LICENSE", b"escape")], [("THIRD_PARTY_LICENSES.txt", b"one"), ("THIRD_PARTY_LICENSES.txt", b"two")]]:
                binary, checksum = fixture(names)
                with self.assertRaises(ValueError):
                    collect_notices(binary, checksum, root / "unsafe")
                self.assertFalse((root / "unsafe").exists())

    def test_flat_source_notices_preserve_paths_and_reject_unsafe_inputs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def fixture(files):
                source = root / "flat-source.tar"
                with tarfile.open(source, "w") as archive:
                    for name, content in files.items():
                        entry = tarfile.TarInfo(name); entry.size = len(content)
                        archive.addfile(entry, io.BytesIO(content))
                return [dict(name="codec", filename=source.name, sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                             strip_components=0, required_notices=["LICENSE", "README"])]
            records = fixture({"LICENSE": b"Original terms", "README": b"Additional terms",
                               "src/licenses/TERMS": b"Nested terms", "src/code.c": b"Never execute"})
            collect_qt_notices(records, root, root / "notices")
            self.assertEqual((root / "notices/codec/LICENSE").read_bytes(), b"Original terms")
            self.assertEqual((root / "notices/codec/README").read_bytes(), b"Additional terms")
            self.assertEqual((root / "notices/codec/src/licenses/TERMS").read_bytes(), b"Nested terms")
            self.assertFalse((root / "notices/codec/src/code.c").exists())
            for prefix in [-1, 2, True, "0"]:
                records[0]["strip_components"] = prefix
                with self.assertRaisesRegex(ValueError, "Invalid source archive prefix"):
                    collect_qt_notices(records, root, root / "invalid-prefix")
            records[0]["strip_components"] = 0
            records[0]["required_notices"].append("../outside")
            with self.assertRaisesRegex(ValueError, "Unsafe source notice path"):
                collect_qt_notices(records, root, root / "unsafe-required")
            for name in ["../LICENSE", "/LICENSE", "C:/LICENSE", "dir\\LICENSE"]:
                records = fixture({name: b"Escape"})
                with self.assertRaises(ValueError):
                    collect_qt_notices(records, root, root / "unsafe-member")
                self.assertFalse((root / "unsafe-member").exists())

    def test_qt_notices_include_referenced_files_and_reject_incomplete_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            def fixture(files):
                source = root / "qt-source.tar"
                with tarfile.open(source, "w") as archive:
                    for name, content in files.items():
                        entry = tarfile.TarInfo(name); entry.size = len(content)
                        archive.addfile(entry, io.BytesIO(content))
                return [dict(name="qtbase", filename=source.name, sha256=hashlib.sha256(source.read_bytes()).hexdigest())]
            files = {"qtbase/LICENSE.txt": b"Qt license", "qtbase/src/TERMS": b"Third party terms",
                     "qtbase/LICENSES/GPL-3.0-only.txt": b"Full GPL license",
                     "qtbase/src/qt_attribution.json": b'{"LicenseFile":"TERMS","Copyright":"literal\nnewline"}',
                     "qtbase/src/not-a-notice.cpp": b"source code"}
            records = fixture(files)
            collect_qt_notices(records, root, root / "notices")
            self.assertEqual((root / "notices/qtbase/src/TERMS").read_bytes(), b"Third party terms")
            self.assertEqual((root / "notices/qtbase/LICENSES/GPL-3.0-only.txt").read_bytes(), b"Full GPL license")
            self.assertFalse((root / "notices/qtbase/src/not-a-notice.cpp").exists())
            metadata = json.loads((root / "notices/notices.json").read_text(encoding="utf-8"))
            self.assertEqual(metadata["components"][0]["source"]["sha256"], records[0]["sha256"])
            files.pop("qtbase/src/TERMS")
            records = fixture(files)
            with self.assertRaisesRegex(ValueError, "Missing source notice reference"):
                collect_qt_notices(records, root, root / "missing")
            (root / records[0]["filename"]).write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                collect_qt_notices(records, root, root / "corrupt")
            self.assertFalse((root / "corrupt").exists())
            records = fixture({"qtbase/../LICENSE": b"escape"})
            with self.assertRaisesRegex(ValueError, "Unsafe source notice path"):
                collect_qt_notices(records, root, root / "unsafe")

    def test_source_materials_must_match_the_binary_pin(self):
        project = Path(__file__).resolve().parents[1]
        source_lock = json.loads((project / "packaging/source-inputs.json").read_text(encoding="utf-8"))
        self.assertFalse(source_lock["corresponding_sources_complete"])
        for platform in ["linux", "windows"]:
            lock = json.loads((project / f"packaging/{platform}/dependencies.json").read_text(encoding="utf-8"))
            binaries = lock["downloads"]
            binaries = list(binaries.values()) if isinstance(binaries, dict) else binaries
            records = source_records(source_lock, binaries, lock["qt"])
            self.assertEqual({record["name"] for record in records if "for_binary_sha256" in record and record.get("kind") not in {"standalone-runtime-source", "ffmpeg-dependency-source"}},
                             {"ffmpeg-source", "ffmpeg-build-recipes", "yt-dlp-source"})
            self.assertEqual({record["name"] for record in records if "for_binary_sha256" not in record},
                             {"qtbase", "qtdeclarative", "qtwayland", "qtsvg", "qtimageformats",
                              "qtshadertools", "qttranslations", "qttools"})
            standalone = [record for record in records if record.get("kind") == "standalone-runtime-source"]
            python = next(record for record in standalone if record["name"].startswith("yt-dlp-cpython-source"))
            websockets = next(record for record in standalone if record["name"].startswith("yt-dlp-websockets-source"))
            self.assertEqual(python["revision"], "3.14.7" if platform == "linux" else "3.10.11")
            self.assertEqual(websockets["revision"], "17.0.1" if platform == "linux" else "16.1.1")
            self.assertEqual(any(record["name"].startswith("yt-dlp-cryptography-source") for record in standalone), platform == "linux")
            cli = subprocess.run([sys.executable, str(project / "scripts/package_inputs.py"), "select-sources",
                                  str(project / "packaging/source-inputs.json"), str(project / f"packaging/{platform}/dependencies.json")],
                                 check=True, text=True, stdout=subprocess.PIPE)
            self.assertEqual(json.loads(cli.stdout)["downloads"], records)
            stale = json.loads(json.dumps(source_lock))
            stale["standalone_dependencies"]["for_binary_sha256"] = ["0" * 64]
            with self.assertRaisesRegex(ValueError, "Standalone dependency sources do not match"):
                source_records(stale, binaries, lock["qt"])
            with self.assertRaisesRegex(ValueError, "does not match pinned SDK"):
                source_records(source_lock, binaries, "0.0.0")
            for name in ["ffmpeg", "yt-dlp"]:
                selected = lock["downloads"][name] if isinstance(lock["downloads"], dict) else next(
                    record for record in binaries if record["name"] == name)
                updated = [dict(record, sha256="0" * 64) if record == selected else record for record in binaries]
                with self.assertRaisesRegex(ValueError, "does not match pinned binary"):
                    source_records(source_lock, updated, lock["qt"])

    def test_application_version_is_independent_of_windows_ansi_locale(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text('project(transcribe VERSION 0.4.0 DESCRIPTION "Транскрипция русской речи")\n', encoding="utf-8")
            script = "import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); from package_source import application_version; print(application_version(Path(sys.argv[2])))"
            environment = dict(os.environ, PYTHONUTF8="0", PYTHONCOERCECLOCALE="0", LC_ALL="C")
            result = subprocess.run([sys.executable, "-c", script, str(Path(__file__).resolve().parents[1] / "scripts"), str(root)],
                                    env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(result.returncode, 0, result.stderr.decode("ascii", errors="replace"))
            self.assertEqual(result.stdout.strip(), b"0.4.0")

    def test_build_inputs_preserve_exact_bytes_and_reject_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "continuous-tool"; source.write_bytes(b"pinned upstream bytes")
            material = root / "patch.py"; material.write_bytes(b"# source patch")
            records = [dict(filename=source.name, sha256=hashlib.sha256(source.read_bytes()).hexdigest())]
            archive = root / "inputs.tar.gz"
            archive_inputs(records, root, archive, [material])
            with tarfile.open(archive) as package:
                self.assertEqual(set(package.getnames()), {"build-inputs.json", "inputs/continuous-tool", "materials/patch.py"})
                self.assertEqual(package.extractfile("inputs/continuous-tool").read(), source.read_bytes())
                self.assertFalse(json.loads(package.extractfile("build-inputs.json").read())["corresponding_sources_complete"])
                self.assertTrue(all(not entry.uname and not entry.gname for entry in package.getmembers()))
            source.write_bytes(b"different upstream bytes")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                archive_inputs(records, root, root / "bad.tar.gz")
            self.assertFalse((root / "bad.tar.gz").exists())
            source.write_bytes(b"pinned upstream bytes")
            original_add = tarfile.TarFile.add
            def mutate_during_archive(archive, path, *args, **kwargs):
                if path == source:
                    source.write_bytes(b"changed after precheck")
                return original_add(archive, path, *args, **kwargs)
            with patch.object(tarfile.TarFile, "add", mutate_during_archive):
                with self.assertRaisesRegex(ValueError, "Archived build input changed"):
                    archive_inputs(records, root, root / "mutated.tar.gz")

    def test_mutation_commit_and_release_guards(self):
        with tempfile.TemporaryDirectory(prefix="transcribe-source-identity-") as temporary:
            project = Path(temporary)
            def git(*args):
                return subprocess.check_output(["git", "-c", "core.hooksPath=/dev/null", "-C", str(project), *args], stderr=subprocess.PIPE)
            git("init")
            git("config", "user.name", "Package test")
            git("config", "user.email", "package-test@example.invalid")
            source = project / "CMakeLists.txt"
            source.write_text("project(transcribe VERSION 0.3.0)\n")
            git("add", ".")
            git("commit", "-m", "test: initial source")
            original = validate_identity(project, release=True)
            with self.assertRaisesRegex(ValueError, "all tests"):
                validate_identity(project, release=True, skip_tests=True)
            source.write_text("project(transcribe VERSION 0.3.0)\n# mutation\n")
            diagnostic = validate_identity(project)
            self.assertTrue(diagnostic["dirty"])
            with self.assertRaisesRegex(ValueError, "clean sources"):
                validate_identity(project, release=True)
            with self.assertRaisesRegex(ValueError, "Source changed"):
                validate_identity(project, original)
            # A concurrent commit makes the tree clean again; it still must fail.
            git("add", ".")
            git("commit", "-m", "test: concurrent mutation")
            self.assertFalse(validate_identity(project)["dirty"])
            with self.assertRaisesRegex(ValueError, "Source changed"):
                validate_identity(project, original)
            (project / "new-input").write_bytes(b"untracked source")
            with self.assertRaisesRegex(ValueError, "Source changed"):
                validate_identity(project, diagnostic)


if __name__ == "__main__":
    unittest.main()
