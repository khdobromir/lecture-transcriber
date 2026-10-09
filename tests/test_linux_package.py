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
