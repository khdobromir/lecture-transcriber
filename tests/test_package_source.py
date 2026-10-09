"""Detect changed/committed source during packaging and reject unsafe release modes."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import tarfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_source import validate_identity
from package_inputs import archive_inputs


class SourceIdentityTests(unittest.TestCase):
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
