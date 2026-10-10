"""Detect changed/committed source during packaging and reject unsafe release modes."""
from pathlib import Path
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
