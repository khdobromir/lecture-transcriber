"""Cross-platform regressions for the exact ZIP and its file manifest."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
from package_archive import extract_verified


class ArchiveTests(unittest.TestCase):
    def fixture(self, mutation=None):
        temp = tempfile.TemporaryDirectory(prefix="transcribe-archive-test-")
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        files = {f"{directory}/{runtime}": b"fixture" for directory in ["bin", "bin/tools"]
                 for runtime in ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]}
        files["bin/Файл 😀.txt"] = b"fixture"
        files["bin/tools/.transcribe-bundle"] = b"1\n"
        manifest = {"version": 1, "source": "a" * 40,
                    "files": [{"path": name, "sha256": hashlib.sha256(content).hexdigest()} for name, content in files.items()]}
        if mutation:
            mutation(files, manifest)
        archive = root / "final.zip"
        with zipfile.ZipFile(archive, "w") as out:
            for name, content in files.items():
                out.writestr("Transcribe/" + name, content)
            out.writestr("Transcribe/package-manifest.json", json.dumps(manifest))
        return archive, root / "extract"

    def test_valid_exact_archive(self):
        archive, destination = self.fixture()
        root, manifest = extract_verified(archive, destination, "a" * 40)
        self.assertTrue((root / "bin/Файл 😀.txt").is_file())
        self.assertEqual(manifest["source"], "a" * 40)

    def test_corruption_unlisted_missing_crt_and_traversal(self):
        for mutation in [lambda files, manifest: files.update({"bin/msvcp140.dll": b"corrupt"}),
                         lambda files, manifest: files.update({"extra": b"unlisted"}),
                         lambda files, manifest: files.pop("bin/tools/vcruntime140_1.dll"),
                         lambda files, manifest: files.update({"../escaped": b"unsafe"})]:
            with self.subTest(mutation=mutation):
                archive, destination = self.fixture(mutation)
                with self.assertRaises((ValueError, FileNotFoundError)):
                    extract_verified(archive, destination, "a" * 40)
                self.assertFalse((destination.parent / "escaped").exists())

    def test_wrong_source(self):
        archive, destination = self.fixture()
        with self.assertRaises(ValueError):
            extract_verified(archive, destination, "b" * 40)

    def test_manifest_cannot_approve_a_zip_without_portable_marker(self):
        def mutation(files, manifest):
            files.pop("bin/tools/.transcribe-bundle")
            manifest["files"] = [entry for entry in manifest["files"] if entry["path"] != "bin/tools/.transcribe-bundle"]
        archive, destination = self.fixture(mutation)
        with self.assertRaisesRegex(ValueError, "portable bundle marker"):
            extract_verified(archive, destination, "a" * 40)


if __name__ == "__main__":
    unittest.main()
