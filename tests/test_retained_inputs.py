"""Recover pinned transport inputs without trusting archive paths or stale pins."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_retained_inputs import digest, restore_archive


class RetainedInputTests(unittest.TestCase):
    def test_restore_checks_archive_member_and_current_pin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive, cache = root / "retained.tar.gz", root / "cache"
            data = b"original pinned binary"
            expected = hashlib.sha256(data).hexdigest()
            records = [dict(filename="tool.tar", sha256=expected), dict(filename="new.tar", sha256=expected)]
            def write(member_data):
                with tarfile.open(archive, "w:gz") as package:
                    for name, content in [("build-inputs.json", json.dumps(dict(files={"inputs/tool.tar": expected})).encode()),
                                          ("inputs/tool.tar", member_data), ("inputs/../../escape", b"unselected")]:
                        entry = tarfile.TarInfo(name); entry.size = len(content)
                        package.addfile(entry, io.BytesIO(content))
            write(data)
            self.assertEqual(restore_archive(archive, cache, records, digest(archive)), 1)
            self.assertEqual((cache / "tool.tar").read_bytes(), data)
            self.assertFalse((cache / "new.tar").exists())
            self.assertFalse((root / "escape").exists())
            self.assertEqual(restore_archive(archive, cache, records, digest(archive)), 0)
            with self.assertRaisesRegex(ValueError, "archive checksum"):
                restore_archive(archive, cache, records, "0" * 64)
            (cache / "tool.tar").unlink()
            changed_pin = [dict(filename="tool.tar", sha256="1" * 64)]
            self.assertEqual(restore_archive(archive, cache, changed_pin, digest(archive)), 0)
            write(b"changed binary with stale inventory")
            with self.assertRaisesRegex(ValueError, "member checksum"):
                restore_archive(archive, cache, records, digest(archive))
            self.assertEqual(list(cache.iterdir()), [])
            with self.assertRaisesRegex(ValueError, "Invalid retained input filename"):
                restore_archive(archive, cache, [dict(filename="../escape", sha256=expected)], digest(archive))


if __name__ == "__main__":
    unittest.main()
