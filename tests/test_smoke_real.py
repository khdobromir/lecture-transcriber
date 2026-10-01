#!/usr/bin/env python3
"""CLI regression tests for missing smoke-test prerequisites; no real models."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
PAYLOAD = b"smoke model fixture\n"


class SmokePreflight(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="transcribe-smoke-tests-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "project"
        (self.project / "tests").mkdir(parents=True)
        (self.project / "scripts").mkdir()
        self.script = self.project / "tests/smoke_real.py"
        shutil.copy2(PROJECT / "tests/smoke_real.py", self.script)
        digest = hashlib.sha256(PAYLOAD).hexdigest()
        rows = []
        for line in (PROJECT / "scripts/models.tsv").read_text().splitlines():
            if line.startswith("#") or not line:
                rows.append(line)
            else:
                fields = line.split()
                fields[-1] = digest
                rows.append(" ".join(fields))
        (self.project / "scripts/models.tsv").write_text("\n".join(rows) + "\n")
        self.binary = self.root / "transcribe"
        self.binary.write_text("#!/bin/sh\nexit 0\n")
        self.binary.chmod(0o755)
        self.home = self.root / "app data"
        self.engine = self.home / "whisper.cpp/build/bin/whisper-cli"
        self.engine.parent.mkdir(parents=True)
        self.engine.write_text("#!/bin/sh\nexit 0\n")
        self.engine.chmod(0o755)
        self.sample = self.home / "whisper.cpp/samples/jfk.wav"
        self.sample.parent.mkdir()
        self.sample.write_bytes(b"invalid audio fixture")
        models = self.home / "models"
        models.mkdir()
        self.medium = models / "ggml-medium-q5_0.bin"
        self.vad = models / "ggml-silero-v6.2.0.bin"
        self.medium.write_bytes(PAYLOAD)
        self.vad.write_bytes(PAYLOAD)
        self.env = dict(os.environ, HOME=str(self.root / "user"))
        self.env.pop("TRANSCRIBE_HOME", None)

    def invoke(self, *args, env=None):
        return subprocess.run([sys.executable, str(self.script), "--binary", str(self.binary),
                               *args], env=env or self.env, capture_output=True, text=True)

    def assert_error(self, result, message):
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(message, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_missing_default_installation_explains_app_home(self):
        result = self.invoke()
        self.assert_error(result, "Каталог установки не найден")
        self.assertIn(".local/share/transcribe", result.stderr)
        self.assertIn("--app-home", result.stderr)
        self.assertIn("bash install.sh medium", result.stderr)
        self.assertFalse((self.root / "user").exists())

    def test_missing_medium_explains_installation(self):
        self.medium.unlink()
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "ggml-medium-q5_0.bin")
        self.assertIn("bash install.sh medium", result.stderr)

    def test_missing_vad_is_reported(self):
        self.vad.unlink()
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "ggml-silero-v6.2.0.bin")

    def test_missing_binary_explains_build(self):
        self.binary.unlink()
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "Нет исполняемой программы")
        self.assertIn("CMake", result.stderr)

    def test_missing_engine_is_reported(self):
        self.engine.unlink()
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "Нет исполняемого whisper-cli")

    def test_missing_sample_is_reported(self):
        self.sample.unlink()
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "samples/jfk.wav")

    def test_custom_app_home_overrides_environment(self):
        self.vad.unlink()
        result = self.invoke("--app-home", str(self.home),
                             env=dict(self.env, TRANSCRIBE_HOME=str(self.root / "wrong home")))
        self.assert_error(result, str(self.vad))
        self.assertNotIn("wrong home", result.stderr)

    def test_corrupt_model_is_preserved(self):
        self.medium.write_bytes(b"corrupt model")
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "SHA-256")
        self.assertEqual(self.medium.read_bytes(), b"corrupt model")

    def test_ffmpeg_failure_has_no_traceback(self):
        result = self.invoke("--app-home", str(self.home))
        self.assert_error(result, "ffmpeg")
        self.assertEqual(self.sample.read_bytes(), b"invalid audio fixture")


if __name__ == "__main__":
    unittest.main(verbosity=2)
