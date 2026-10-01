#!/usr/bin/env python3
"""Installer tests use real file operations/hashes and controlled external tools."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
REVISION = "927cfce34f31707e17f2bff35c349632fb9e2c3a"
PAYLOAD = b"verified fixture model\n"
MOCK = r'''
import json, os, sys
from pathlib import Path
kind = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ["INSTALL_LOG"], "a") as log:
    log.write(json.dumps([kind, args]) + "\n")
def value(flag):
    return args[args.index(flag) + 1]
if kind == "git":
    if args[0] == "clone":
        destination = Path(args[-1])
        destination.mkdir(parents=True)
        (destination / "partial-clone").write_text("fixture")
        if os.environ.get("FAIL_CLONE"):
            sys.exit(41)
    elif "rev-parse" in args:
        print(os.environ.get("ENGINE_REVISION", "927cfce34f31707e17f2bff35c349632fb9e2c3a"))
    elif "status" in args:
        print(os.environ.get("DIRTY_ENGINE", ""), end="")
elif kind == "cmake":
    if os.environ.get("FAIL_BUILD"):
        sys.exit(42)
    if "--build" in args:
        build = Path(value("--build"))
        binary = build / ("bin/whisper-cli" if "--target" in args else "transcribe")
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_text("#!/bin/bash\nprintf 'fixture executable\\n'\n")
        binary.chmod(0o755)
elif kind == "curl":
    destination = Path(value("--output"))
    assert "--continue-at" in args
    assert "/resolve/main/" not in args[-1]
    data = b"verified fixture model\n"
    if os.environ.get("FAIL_NETWORK") or (os.environ.get("FAIL_VAD") and "silero" in args[-1]):
        destination.write_bytes(data[:7])
        sys.exit(28)
    if os.environ.get("BAD_HASH"):
        data = b"corrupted download"
    if destination.exists():
        previous = destination.read_bytes()
        assert data.startswith(previous), "resume must preserve prefix"
        with destination.open("ab") as stream:
            stream.write(data[len(previous):])
    else:
        destination.write_bytes(data)
'''


class Installation(unittest.TestCase):
    def setUp(self):
        if os.geteuid() == 0:
            raise RuntimeError("Запускайте тесты установщика обычным пользователем")
        self.temp = tempfile.TemporaryDirectory(prefix="transcribe-install-tests-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.project = self.directory / "Проект с пробелами"
        self.project.mkdir()
        shutil.copy2(PROJECT / "install.sh", self.project)
        shutil.copytree(PROJECT / "scripts", self.project / "scripts")
        digest = hashlib.sha256(PAYLOAD).hexdigest()
        manifest = self.project / "scripts/models.tsv"
        rows = []
        for line in manifest.read_text().splitlines():
            if line.startswith("#") or not line:
                rows.append(line)
            else:
                fields = line.split()
                fields[-1] = digest
                rows.append(" ".join(fields))
        manifest.write_text("\n".join(rows) + "\n")
        self.home = self.directory / "Пользователь"
        self.root = self.home / "app data"
        self.bin = self.directory / "tools"
        self.bin.mkdir()
        # No system git/curl/build tools can accidentally be used by these tests.
        for tool in ("dirname", "mkdir", "mv", "rm", "mktemp", "install", "sha256sum", "flock"):
            executable = shutil.which(tool)
            self.assertIsNotNone(executable, tool)
            (self.bin / tool).symlink_to(executable)
        for tool in ("git", "cmake", "curl", "g++", "ffmpeg", "yt-dlp"):
            script = self.bin / tool
            script.write_text(f"#!{sys.executable}\n" + MOCK)
            script.chmod(0o755)
        self.log = self.directory / "calls.jsonl"
        self.env = dict(os.environ, HOME=str(self.home), TRANSCRIBE_HOME=str(self.root),
                        PATH=str(self.bin), INSTALL_LOG=str(self.log))
        self.env.pop("TRANSCRIBE_BUILD_JOBS", None)

    def invoke(self, script="install.sh", *args, env=None):
        return subprocess.run(["/bin/bash", str(self.project / script), *args],
                              env=env or self.env, cwd=self.directory,
                              capture_output=True, text=True)

    def calls(self, kind):
        if not self.log.exists():
            return []
        return [args for name, args in map(json.loads, self.log.read_text().splitlines())
                if name == kind]

    def model(self):
        return self.root / "models/ggml-small-q5_1.bin"

    def previous_installation(self):
        self.command = self.home / ".local/bin/transcribe"
        self.command.parent.mkdir(parents=True)
        self.command.write_bytes(b"old command")
        self.default = self.root / "default-model"
        self.default.parent.mkdir(parents=True)
        self.default.write_bytes(b"medium\n")
        self.engine = self.root / "whisper.cpp/build/bin/whisper-cli"
        self.engine.parent.mkdir(parents=True)
        self.engine.write_bytes(b"old engine")

    def assert_previous_preserved(self):
        self.assertEqual(self.command.read_bytes(), b"old command")
        self.assertEqual(self.default.read_bytes(), b"medium\n")
        self.assertEqual(self.engine.read_bytes(), b"old engine")
        self.assertEqual(list(self.root.glob(".install.*")), [self.root / ".install.lock"])

    def test_success_and_repeat_skip_download_and_clone(self):
        for _ in range(2):
            result = self.invoke("install.sh", "small")
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / "default-model").read_text(), "small\n")
        self.assertTrue(os.access(self.home / ".local/bin/transcribe", os.X_OK))
        self.assertTrue(os.access(self.root / "whisper.cpp/build/bin/whisper-cli", os.X_OK))
        self.assertEqual(len(self.calls("curl")), 2)
        self.assertEqual(len([args for args in self.calls("git") if args[0] == "clone"]), 1)
        self.assertEqual(self.model().read_bytes(), PAYLOAD)
        self.assertFalse(list(self.root.glob(".install.??????")))

    def test_invalid_arguments_create_no_directories(self):
        for args in (("bad",), ("small", "extra")):
            result = self.invoke("install.sh", *args)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(self.home.exists())
        result = self.invoke("install.sh", "small", env=dict(self.env, TRANSCRIBE_BUILD_JOBS="0"))
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.home.exists())

    def test_default_data_directory_without_override(self):
        env = dict(self.env)
        env.pop("TRANSCRIBE_HOME", None)
        result = self.invoke("install.sh", "small", env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        default_data = self.home / ".local/share/transcribe"
        self.assertEqual((default_data / "default-model").read_text(), "small\n")
        self.assertTrue((default_data / "whisper.cpp/build/bin/whisper-cli").is_file())
        self.assertTrue(os.access(self.home / ".local/bin/transcribe", os.X_OK))
        self.assertFalse(self.root.exists())

    def test_missing_tool_creates_no_directories(self):
        (self.bin / "curl").unlink()
        result = self.invoke("install.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("curl", result.stderr)
        self.assertFalse(self.home.exists())

    def test_wrong_revision_preserves_previous_installation(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, ENGINE_REVISION="0" * 40))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.command.read_bytes(), b"old command")
        self.assertEqual(self.default.read_bytes(), b"medium\n")
        self.assertFalse(self.calls("cmake"))
        self.assertFalse(self.calls("curl"))

    def test_dirty_sources_are_rejected(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, DIRTY_ENGINE=" M example.cpp\n"))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.calls("cmake"))

    def test_clone_failure_does_not_publish_sources(self):
        result = self.invoke("install.sh", "small", env=dict(self.env, FAIL_CLONE="1"))
        self.assertEqual(result.returncode, 41)
        self.assertFalse((self.root / "whisper.cpp").exists())
        self.assertFalse(list(self.root.glob(".install.??????")))

    def test_wrong_cloned_revision_does_not_publish_sources(self):
        result = self.invoke("install.sh", "small", env=dict(self.env, ENGINE_REVISION="0" * 40))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "whisper.cpp").exists())

    def test_build_failure_preserves_previous_installation(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, FAIL_BUILD="1"))
        self.assertEqual(result.returncode, 42)
        self.assert_previous_preserved()
        self.assertFalse(self.calls("curl"))

    def test_network_failure_preserves_previous_installation(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, FAIL_NETWORK="1"))
        self.assertEqual(result.returncode, 28)
        self.assert_previous_preserved()
        self.assertTrue(Path(str(self.model()) + ".part").is_file())
        self.assertFalse(self.model().exists())

    def test_vad_failure_preserves_previous_installation(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, FAIL_VAD="1"))
        self.assertEqual(result.returncode, 28)
        self.assert_previous_preserved()
        self.assertEqual(self.model().read_bytes(), PAYLOAD)

    def test_bad_download_hash_removes_only_partial_file(self):
        self.previous_installation()
        result = self.invoke("install.sh", "small", env=dict(self.env, BAD_HASH="1"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256", result.stderr)
        self.assert_previous_preserved()
        self.assertFalse(self.model().exists())
        self.assertFalse(Path(str(self.model()) + ".part").exists())

    def test_corrupt_existing_model_is_preserved(self):
        self.model().parent.mkdir(parents=True)
        self.model().write_bytes(b"existing corrupt model")
        result = self.invoke("scripts/download-model.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256", result.stderr)
        self.assertEqual(self.model().read_bytes(), b"existing corrupt model")
        self.assertFalse(self.calls("curl"))

    def test_network_failure_then_resume(self):
        first = self.invoke("scripts/download-model.sh", "small", env=dict(self.env, FAIL_NETWORK="1"))
        self.assertEqual(first.returncode, 28)
        self.assertFalse(self.model().exists())
        second = self.invoke("scripts/download-model.sh", "small")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(self.model().read_bytes(), PAYLOAD)
        self.assertFalse(Path(str(self.model()) + ".part").exists())

    def test_complete_partial_is_promoted_without_network(self):
        self.model().parent.mkdir(parents=True)
        Path(str(self.model()) + ".part").write_bytes(PAYLOAD)
        result = self.invoke("scripts/download-model.sh", "small")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.model().read_bytes(), PAYLOAD)
        self.assertFalse(self.calls("curl"))

    def test_existing_corrupt_model_preserves_previous_installation(self):
        self.previous_installation()
        self.model().parent.mkdir(parents=True)
        self.model().write_bytes(b"keep corrupt existing model")
        result = self.invoke("install.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assert_previous_preserved()
        self.assertEqual(self.model().read_bytes(), b"keep corrupt existing model")
        self.assertFalse(self.calls("curl"))

    def test_download_selection_and_tools_validated_before_mkdir(self):
        for args in (("bad",), ("small", "extra")):
            result = self.invoke("scripts/download-model.sh", *args)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(self.home.exists())
        (self.bin / "sha256sum").unlink()
        result = self.invoke("scripts/download-model.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.home.exists())

    def test_partial_symlink_does_not_modify_external_file(self):
        self.model().parent.mkdir(parents=True)
        external = self.directory / "keep"
        external.write_bytes(b"original")
        Path(str(self.model()) + ".part").symlink_to(external)
        result = self.invoke("scripts/download-model.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(external.read_bytes(), b"original")
        self.assertFalse(self.calls("curl"))

    def test_lock_rejects_concurrent_download(self):
        import fcntl
        self.root.mkdir(parents=True)
        with (self.root / ".install.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.invoke("scripts/download-model.sh", "small")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.calls("curl"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
