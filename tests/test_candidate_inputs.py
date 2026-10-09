"""Ensure clean-container tests consume the builder's exact image and fixtures."""
import json
from pathlib import Path
import tempfile
import unittest
from check_linux_candidate import prepare_inputs
from linux_package import digest


class CandidateInputTests(unittest.TestCase):
    def test_exact_selection_and_tampering(self):
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary)
            dist = project / "dist"; dist.mkdir()
            image = dist / "candidate.AppImage"; image.write_bytes(b"exact-image")
            (dist / "stale.AppImage").write_bytes(b"must-not-select")
            prerequisites = project / ".cache/linux-package-container/run-exact/real-prerequisites"
            (prerequisites / "data/models").mkdir(parents=True)
            audio = prerequisites / "Русская речь 😀.wav"; audio.write_bytes(b"audio")
            model = prerequisites / "data/models/ggml-small-q5_1.bin"; model.write_bytes(b"model")
            metadata = dict(environment={"TRANSCRIBE_REAL_AUDIO": "/work/run-exact/real-prerequisites/" + audio.name},
                            audio_sha256=digest(audio), model_sha256=digest(model))
            (prerequisites / "prerequisites.json").write_text(json.dumps(metadata))
            record = dict(filename=image.name, sha256=digest(image), stage="run-exact", real_smoke=True)
            record_path = dist / "candidate.json"; record_path.write_text(json.dumps(record))
            inputs = project / "inputs"; inputs.mkdir()
            prepare_inputs(record_path, inputs, project)
            self.assertEqual((inputs / "Transcribe.AppImage").read_bytes(), b"exact-image")
            self.assertEqual((inputs / "audio.wav").read_bytes(), b"audio")
            self.assertEqual((inputs / "model.bin").read_bytes(), b"model")
            self.assertEqual(len((inputs / "SHA256SUMS").read_text().splitlines()), 3)
            model.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "fixture checksum"):
                prepare_inputs(record_path, inputs, project)
            image.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "AppImage checksum"):
                prepare_inputs(record_path, inputs, project)
            record["filename"] = "../elsewhere.AppImage"
            record_path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, "basenames"):
                prepare_inputs(record_path, inputs, project)


if __name__ == "__main__":
    unittest.main()
