"""Regressions for hostile downloads/archives and complete Linux manifests."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from linux_package import verify

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
spec = importlib.util.spec_from_file_location("linux_builder", Path(__file__).resolve().parents[1] / "scripts/package-linux.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class LinuxPackageTests(unittest.TestCase):
    def test_gateway_download_retries_are_bounded_and_keep_strict_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = b"Verified source input"
            record = dict(filename="retry-source.tar", url="https://example.invalid/source.tar",
                          sha256=builder.hashlib.sha256(data).hexdigest())
            def response(content):
                stream = io.BytesIO(content); stream.geturl = lambda: record["url"]
                return stream
            def failure(status):
                return builder.urllib.error.HTTPError(record["url"], status, "Upstream failure", {}, io.BytesIO())
            with patch.object(builder.urllib.request, "urlopen", side_effect=[failure(502), failure(503), response(data)]) as request, patch("time.sleep"):
                target = builder.fetch(record, root)
            self.assertEqual(request.call_count, 3)
            self.assertEqual(target.read_bytes(), data)
            self.assertEqual(list(root.iterdir()), [target])
            target.unlink()
            for statuses in [[502, 502, 502], [404]]:
                with patch.object(builder.urllib.request, "urlopen", side_effect=[failure(s) for s in statuses]) as request, patch("time.sleep"):
                    with self.assertRaisesRegex(RuntimeError, r"retry-source\.tar.*HTTP " + str(statuses[-1])):
                        builder.fetch(record, root)
                self.assertEqual(request.call_count, len(statuses))
                self.assertEqual(list(root.iterdir()), [])
            with patch.object(builder.urllib.request, "urlopen", return_value=response(b"Changed bytes")) as request, patch("time.sleep") as sleep:
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    builder.fetch(record, root)
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()
            self.assertEqual(list(root.iterdir()), [])
            redirect = response(data); redirect.geturl = lambda: "http://example.invalid/source.tar"
            with patch.object(builder.urllib.request, "urlopen", return_value=redirect) as request, patch("time.sleep") as sleep:
                with self.assertRaisesRegex(ValueError, "HTTPS"):
                    builder.fetch(record, root)
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()
            self.assertEqual(list(root.iterdir()), [])

    def test_download_verifies_canonical_content_and_discards_failed_partials(self):
        from package_canonical_sources import canonicalize_tar
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); cache = root / "cache"; cache.mkdir()
            def archive(name, timestamp, data):
                target = root / name
                with tarfile.open(target, "w:gz", format=tarfile.PAX_FORMAT) as package:
                    entry = tarfile.TarInfo("LICENSE"); entry.size = len(data); entry.mtime = timestamp
                    package.addfile(entry, io.BytesIO(data))
                return target
            first = archive("first.tar.gz", 1000.125, b"Original terms")
            later = archive("later.tar.gz", 2000.875, b"Original terms")
            canonical = root / "canonical.tar"; canonicalize_tar(first, canonical)
            record = dict(filename="codec.tar", url="https://example.invalid/source.tar.gz",
                          sha256=builder.digest(canonical), canonical_tar=True)
            def response(data):
                stream = io.BytesIO(data); stream.geturl = lambda: record["url"]
                return stream
            with patch.object(builder.urllib.request, "urlopen", return_value=response(later.read_bytes())):
                result = builder.fetch(record, cache)
            self.assertEqual(result.read_bytes(), canonical.read_bytes())
            self.assertEqual(list(cache.iterdir()), [result])
            result.unlink()
            changed = archive("changed.tar.gz", 3000.125, b"Changed terms")
            with patch.object(builder.urllib.request, "urlopen", return_value=response(changed.read_bytes())):
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    builder.fetch(record, cache)
            self.assertEqual(list(cache.iterdir()), [])

    def test_runtime_sources_bind_original_notices_and_recipes_to_exact_binary(self):
        from package_runtime_sources import runtime_source_records, collect_runtime_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "runtime"; binary.write_bytes(b"pinned runtime bytes")
            runtime = dict(sha256=builder.digest(binary))
            source = root / "runtime-source.tar"
            with tarfile.open(source, "w") as archive:
                for name, data in [("src/LICENSE", b"License"), ("src/README", b"Additional terms")]:
                    member = tarfile.TarInfo(name); member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))
            record = dict(name="runtime-source", filename=source.name, sha256=builder.digest(source),
                          kind="runtime-source", required_notices=["LICENSE", "README"],
                          for_binary_sha256=[runtime["sha256"]])
            recipes = dict(name="recipes", kind="runtime-build-recipes", for_binary_sha256=[runtime["sha256"]])
            lock = dict(linux_appimage_runtime=dict(for_binary_sha256=runtime["sha256"], downloads=[record, recipes]))
            self.assertEqual(runtime_source_records(lock, runtime), [record, recipes])
            collect_runtime_notices(lock, runtime, binary, root, root / "licenses")
            self.assertEqual((root / "licenses/AppImage-runtime-source-notices/runtime-source/README").read_bytes(),
                             b"Additional terms")
            evidence = json.loads((root / "licenses/appimage-runtime-provenance.json").read_text())
            self.assertEqual(evidence["binary_sha256"], runtime["sha256"])
            self.assertEqual(evidence["source_inputs"]["downloads"], [record, recipes])
            self.assertFalse(evidence["corresponding_sources_complete"])
            with self.assertRaisesRegex(ValueError, "do not match pinned binary"):
                runtime_source_records(lock, dict(sha256="0" * 64))
            recipes["for_binary_sha256"] = ["0" * 64]
            with self.assertRaisesRegex(ValueError, "inconsistent binary mapping"):
                runtime_source_records(lock, runtime)
            recipes["for_binary_sha256"] = [runtime["sha256"]]
            binary.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "binary checksum mismatch"):
                collect_runtime_notices(lock, runtime, binary, root, root / "bad-binary")
            self.assertFalse((root / "bad-binary").exists())
            binary.write_bytes(b"pinned runtime bytes"); record["required_notices"].append("missing.txt")
            with self.assertRaisesRegex(ValueError, "required source notice"):
                collect_runtime_notices(lock, runtime, binary, root, root / "missing-notice")
            self.assertFalse((root / "missing-notice").exists())
            record["required_notices"] = ["../outside"]
            with self.assertRaisesRegex(ValueError, "Unsafe source notice path"):
                collect_runtime_notices(lock, runtime, binary, root, root / "unsafe-notice")

    def test_runtime_lock_retains_separate_platform_inputs_and_exact_alpine_recipes(self):
        from package_runtime_sources import runtime_source_records
        lock = json.loads((builder.PROJECT / "packaging/source-inputs.json").read_text())
        dependencies = json.loads((builder.PROJECT / "packaging/linux/dependencies.json").read_text())
        records = runtime_source_records(lock, dependencies["downloads"]["runtime"])
        self.assertEqual(len([r for r in records if r["kind"] == "runtime-source"]), 7)
        recipe_names = {r["filename"] for r in records if r["kind"] == "runtime-build-recipes"}
        self.assertEqual(len(recipe_names), 2)
        group = lock["linux_appimage_runtime"]
        self.assertFalse(group["corresponding_sources_complete"])
        for package in group["build_evidence"]["alpine_packages"]:
            self.assertIn(package["recipe_input"], recipe_names)
        common = builder.source_records(lock, dependencies["downloads"].values(), dependencies["qt"])
        self.assertFalse({r["name"] for r in common}.intersection(r["name"] for r in records))

    def test_sdk_sources_require_exact_qt_and_library_hashes_before_copying_notices(self):
        from package_sdk_sources import sdk_source_records, collect_sdk_notices
        import copy
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "icu.tar"
            with tarfile.open(source, "w") as archive:
                entry = tarfile.TarInfo("icu/LICENSE"); entry.size = 11
                archive.addfile(entry, io.BytesIO(b"ICU license"))
            record = dict(name="icu", revision="73.2", filename=source.name,
                          sha256=builder.digest(source), libraries={"libicuuc.so.73": "a" * 64},
                          required_notices=["LICENSE"])
            lock = dict(linux_sdk_dependencies=dict(qt="6.8.3", downloads=[record]))
            self.assertEqual(sdk_source_records(lock, "6.8.3"), [record])
            with self.assertRaisesRegex(ValueError, "SDK source inputs do not match Qt"):
                sdk_source_records(lock, "6.11.2")
            provenance = dict(corresponding_sources_complete=False,
                              libraries=[dict(payload="usr/lib/libicuuc.so.73", provider="sdk-third-party",
                                              payload_sha256="b" * 64, original_sha256="a" * 64)],
                              unresolved=["usr/lib/libicuuc.so.73"])
            before = copy.deepcopy(provenance)
            result = collect_sdk_notices(provenance, [record], root, root / "licenses")
            self.assertEqual(provenance, before, "Do not mutate provenance before all checks succeed")
            self.assertEqual(result["unresolved"], [])
            self.assertFalse(result["corresponding_sources_complete"])
            library = result["libraries"][0]
            self.assertEqual(library["source_input"]["sha256"], record["sha256"])
            self.assertEqual((root / "licenses" / library["copyright"]).read_bytes(), b"ICU license")
            for field, value in [("original_sha256", "c" * 64), ("provider", "deb")]:
                changed = copy.deepcopy(provenance); changed["libraries"][0][field] = value
                destination = root / ("wrong-" + field)
                with self.assertRaisesRegex(ValueError, "SDK library does not match"):
                    collect_sdk_notices(changed, [record], root, destination)
                self.assertFalse(destination.exists())
            with self.assertRaisesRegex(ValueError, "SDK library does not match"):
                collect_sdk_notices(dict(libraries=[], unresolved=[]), [record], root, root / "missing-library")
            ambiguous = copy.deepcopy(provenance)
            duplicate = dict(ambiguous["libraries"][0], payload="usr/plugins/libicuuc.so.73")
            ambiguous["libraries"].append(duplicate)
            with self.assertRaisesRegex(ValueError, "SDK library does not match"):
                collect_sdk_notices(ambiguous, [record], root, root / "ambiguous")
            source.write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                collect_sdk_notices(provenance, [record], root, root / "corrupt")

    def test_sdk_mapping_rejects_missing_required_notice_and_duplicate_claims(self):
        from package_sdk_sources import collect_sdk_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "icu.tar"
            with tarfile.open(source, "w") as archive:
                entry = tarfile.TarInfo("icu/COPYING"); entry.size = 11
                archive.addfile(entry, io.BytesIO(b"ICU license"))
            record = dict(name="icu", revision="73.2", filename=source.name,
                          sha256=builder.digest(source), libraries={"libicuuc.so.73": "a" * 64},
                          required_notices=["LICENSE"])
            provenance = dict(libraries=[dict(payload="usr/lib/libicuuc.so.73", provider="sdk-third-party",
                                             original_sha256="a" * 64)], unresolved=["usr/lib/libicuuc.so.73"])
            with self.assertRaisesRegex(ValueError, "required source notice"):
                collect_sdk_notices(provenance, [record], root, root / "missing-notice")
            with self.assertRaisesRegex(ValueError, "Duplicate SDK library source mapping"):
                collect_sdk_notices(provenance, [record, record], root, root / "duplicates")

    def test_system_library_owner_handles_usrmerge_and_rejects_ambiguous_ownership(self):
        from package_linux_notices import package_owner
        def output(command, **kwargs):
            if command[-1] == "/usr/lib/transcribe-fixture.so.1":
                raise subprocess.CalledProcessError(1, command)
            self.assertEqual(command[-1], "/lib/transcribe-fixture.so.1")
            return "libfixture1:amd64: /lib/transcribe-fixture.so.1\n"
        with patch("package_linux_notices.subprocess.check_output", side_effect=output):
            self.assertEqual(package_owner(Path("/usr/lib/transcribe-fixture.so.1")), "libfixture1:amd64")
        with patch("package_linux_notices.subprocess.check_output",
                   return_value="one:amd64, two:amd64: /lib/transcribe-fixture.so.1\n"):
            with self.assertRaisesRegex(ValueError, "uniquely attribute"):
                package_owner(Path("/lib/transcribe-fixture.so.1"))

    def test_data_payloads_retain_exact_sources_and_verify_the_owner_or_generator(self):
        from package_linux_notices import collect_data_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); app = root / "app"; app.mkdir()
            source = root / "font"; source.write_bytes(b"font bytes")
            payload = app / "font.ttf"; payload.write_bytes(source.read_bytes())
            generator = root / "update-ca-certificates"; generator.write_bytes(b"generator recipe")
            bundle = app / "ca-bundle.crt"; bundle.write_bytes(b"CA bundle")
            original_bundle = root / "original.crt"; original_bundle.write_bytes(bundle.read_bytes())
            system = root / "system"
            for package in ["fonts-demo", "ca-certificates"]:
                notice = system / "usr/share/doc" / package / "copyright"
                notice.parent.mkdir(parents=True); notice.write_bytes(b"Copyright " + package.encode())
            entries = [dict(payload="font.ttf", original=source, package="fonts-demo"),
                       dict(payload="ca-bundle.crt", original=original_bundle,
                            package="ca-certificates", generator=generator)]
            def query(command):
                if command[:2] == ["dpkg-query", "-S"]:
                    owner = "fonts-demo" if command[-1] == str(source) else "ca-certificates"
                    return owner + ": " + command[-1] + "\n"
                owner = command[-1]
                return f"{owner}\t1.0-2\t{owner}-source\t1.0-2\n"
            with patch("package_linux_notices.query", side_effect=query):
                result = collect_data_notices(dict(libraries=[]), app, root / "licenses", entries, system)
                self.assertEqual([r["source_package"] for r in result["data_files"]],
                                 ["fonts-demo-source", "ca-certificates-source"])
                self.assertEqual(result["data_files"][1]["generator_sha256"], builder.digest(generator))
                payload.write_bytes(b"altered data")
                with self.assertRaisesRegex(ValueError, "Data payload differs from builder input"):
                    collect_data_notices(dict(libraries=[]), app, root / "changed", entries, system)
            payload.write_bytes(source.read_bytes())
            with patch("package_linux_notices.query", side_effect=query), \
                    patch("package_linux_notices.package_owner", return_value="unrelated-package"), \
                    self.assertRaisesRegex(ValueError, "Data input owner does not match"):
                collect_data_notices(dict(libraries=[]), app, root / "wrong-owner", entries, system)

    def test_system_library_provenance_copies_package_and_common_notices(self):
        from package_linux_notices import collect_system_notices
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"; libraries = app / "usr/lib"; libraries.mkdir(parents=True)
            (libraries / "libdemo.so.1").write_bytes(b"\x7fELFpatched library")
            (libraries / "libunresolved.so.1").write_bytes(b"\x7fELFunknown")
            (libraries / "libQt6Core.so.6").write_bytes(b"\x7fELFQt")
            sdk = root / "sdk"; sdk.mkdir(); (sdk / "libQt6Core.so.6").write_bytes(b"\x7fELFQt original")
            system = root / "system"; original = system / "lib/libdemo.so.1"
            original.parent.mkdir(parents=True); original.write_bytes(b"\x7fELForiginal library")
            notice = system / "usr/share/doc/libdemo1/copyright"; notice.parent.mkdir(parents=True)
            notice.write_bytes(b"Demo copyright; see /usr/share/common-licenses/LGPL-2.1")
            common = system / "usr/share/common-licenses/LGPL-2.1"; common.parent.mkdir(parents=True)
            common.write_bytes(b"LGPL license text")
            def output(command, **kwargs):
                if command == ["ldconfig", "-p"]:
                    return f"libdemo.so.1 (libc6,x86-64) => {original}\n"
                if command[:2] == ["dpkg-query", "-S"]:
                    return "libdemo1:amd64: " + str(original) + "\n"
                if command[:2] == ["dpkg-query", "-W"]:
                    return "libdemo1:amd64\t1.2-3\tdemo\t1.2-3\n"
                self.fail("Unexpected subprocess")
            licenses = app / "licenses"; licenses.mkdir()
            with patch("package_linux_notices.subprocess.check_output", side_effect=output):
                result = collect_system_notices(app, licenses, sdk, system_root=system)
            deb = next(entry for entry in result["libraries"] if entry["provider"] == "deb")
            self.assertEqual(deb["source_package"], "demo")
            self.assertEqual(deb["source_version"], "1.2-3")
            self.assertNotEqual(deb["payload_sha256"], deb["original_sha256"])
            self.assertEqual((licenses / deb["copyright"]).read_bytes(), notice.read_bytes())
            self.assertEqual((licenses / "Linux-system/common-licenses/LGPL-2.1").read_bytes(), common.read_bytes())
            self.assertEqual(result["unresolved"], ["usr/lib/libunresolved.so.1"])
            self.assertFalse(result["corresponding_sources_complete"])
            # Missing copyright cannot be reported as covered just because dpkg
            # identified a package. Retain an explicit unresolved payload entry.
            notice.unlink()
            with patch("package_linux_notices.subprocess.check_output", side_effect=output):
                missing = collect_system_notices(app, app / "missing-notices", sdk, system_root=system)
            self.assertIn("usr/lib/libdemo.so.1", missing["unresolved"])
            record = next(entry for entry in missing["libraries"] if entry["payload"].endswith("libdemo.so.1"))
            self.assertEqual(record["provider"], "unresolved")
            self.assertIn("Missing system package copyright", record["reason"])

    def test_publication_rejects_changed_bytes_and_preserves_previous_candidate(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "staging"; source.write_bytes(b"checked bytes")
            target = root / "published.AppImage"
            expected = builder.digest(source)
            source.write_bytes(b"changed bytes")
            with self.assertRaisesRegex(ValueError, "bytes changed"):
                builder.publish_file(source, target, expected)
            self.assertFalse(target.exists())
            self.assertFalse(list(root.glob(".candidate-*")))
            source.write_bytes(b"checked bytes")
            builder.publish_file(source, target, expected)
            self.assertEqual(target.read_bytes(), b"checked bytes")
            with self.assertRaises(FileExistsError):
                builder.publish_file(source, target, expected)
            self.assertEqual(target.read_bytes(), b"checked bytes")

    def test_launchers_isolate_qt_and_cpu_backends_from_the_callers_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "Приложение 😀"; tools = root / "usr/bin/tools"
            tools.mkdir(parents=True); (tools / ".transcribe-bundle").write_text("1\n")
            launcher = builder.PROJECT / "packaging/linux/AppRun"
            shutil.copy2(launcher, root / "AppRun")
            gui = root / "usr/bin/transcribe-gui"
            gui.write_text('#!/bin/sh\nprintf "%s\\n" "$QT_PLUGIN_PATH" "$QML_IMPORT_PATH" "$LD_LIBRARY_PATH" "$QT_QPA_PLATFORM_PLUGIN_PATH" "$QT_QPA_PLATFORMTHEME" "$1"\n')
            gui.chmod(0o755)
            env = dict(os.environ, QT_PLUGIN_PATH="/outside", QML_IMPORT_PATH="/outside", LD_LIBRARY_PATH="/outside",
                       QT_QPA_PLATFORM_PLUGIN_PATH="/outside", QT_QPA_PLATFORMTHEME="outside")
            argument = 'Файл с пробелами 😀 $(literal)'
            output = subprocess.check_output(["/bin/sh", str(root / "AppRun"), argument], cwd=temporary, env=env, text=True)
            self.assertEqual(output.splitlines(), [str(root / "usr/plugins"), str(root / "usr/qml"),
                                                   str(root / "usr/lib"), "", "generic", argument])
            (tools / ".transcribe-bundle").unlink()
            self.assertNotEqual(subprocess.run(["/bin/sh", str(root / "AppRun")],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode, 0)
            wrapper = tools / "whisper-cli"
            shutil.copy2(builder.PROJECT / "packaging/linux/whisper-cli", wrapper)
            backend = tools / "whisper-cli.bin"
            backend.write_text('#!/bin/sh\nprintf "%s\\n" "$PWD" "$GGML_BACKEND_PATH" "$1"\n'); backend.chmod(0o755)
            output = subprocess.check_output(["/bin/sh", str(wrapper), argument], cwd=temporary,
                                            env=dict(os.environ, GGML_BACKEND_PATH="/outside.so"), text=True)
            self.assertEqual(output.splitlines(), [str(tools), "", argument])

    def test_archives_reject_traversal_links_devices_and_duplicates(self):
        for names, link in [(["../outside"], None), (["/absolute"], None), (["same", "same"], None),
                            (["symlink"], tarfile.SYMTYPE), (["hardlink"], tarfile.LNKTYPE), (["device"], tarfile.CHRTYPE)]:
            with self.subTest(names=names, link=link), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary); archive = root / "input.tar"
                with tarfile.open(archive, "w") as out:
                    for name in names:
                        entry = tarfile.TarInfo(name); entry.size = 1
                        if link:
                            entry.type = link; entry.linkname = "../outside"
                        out.addfile(entry, io.BytesIO(b"x"))
                with self.assertRaises(ValueError):
                    builder.extract_sources(archive, root / "output")
                self.assertFalse((root / "output").exists())

    def test_corrupt_cache_is_rejected_without_network_or_execution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); (root / "tool").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "checksum"):
                builder.fetch({"filename": "tool", "sha256": "0" * 64}, root)
            (root / "tool").unlink(); (root / "tool").symlink_to("outside")
            with self.assertRaisesRegex(ValueError, "symlinks"):
                builder.fetch({"filename": "tool", "sha256": "0" * 64}, root)

    def test_complete_manifest_covers_modes_links_added_and_corrupt_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ["AppRun", "usr/bin/transcribe", "usr/bin/transcribe-gui", "usr/bin/tools/.transcribe-bundle",
                         "usr/bin/tools/whisper-cli", "usr/bin/tools/whisper-cli.bin",
                         "usr/bin/tools/libggml-cpu-x64.so", "usr/bin/tools/libggml-cpu-sandybridge.so",
                         "usr/bin/tools/libggml-cpu-haswell.so", "usr/bin/tools/ffmpeg", "usr/bin/tools/ffprobe", "usr/bin/tools/yt-dlp"]:
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b"payload")
            (root / "relative").symlink_to("AppRun")
            manifest = {"schema": 1, "source": "abc", "files": builder.inventory(root)}
            (root / "package-manifest.json").write_text(json.dumps(manifest))
            verify(root, "abc")
            with self.assertRaisesRegex(ValueError, "provenance"):
                verify(root, "wrong")
            (root / "extra").write_bytes(b"unexpected")
            with self.assertRaisesRegex(ValueError, "whole package"):
                verify(root, "abc")
            (root / "extra").unlink(); (root / "AppRun").write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify(root, "abc")
            (root / "AppRun").write_bytes(b"payload"); (root / "AppRun").chmod(0o755)
            with self.assertRaisesRegex(ValueError, "mode"):
                verify(root, "abc")
            (root / "escape").symlink_to("/etc/passwd")
            with self.assertRaisesRegex(ValueError, "symlink"):
                builder.inventory(root)


if __name__ == "__main__":
    unittest.main()
