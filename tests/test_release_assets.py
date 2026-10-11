"""Reject checksum-only assets and require GitHub's digest for every upload."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_assets import asset_names, inventory, verify_github


class ReleaseAssetTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        for name in asset_names("0.4.1"):
            (self.directory / name).write_bytes(b"fixture: " + name.encode())
        self.local = inventory(self.directory, "0.4.1")
        self.remote = {"assets": [dict(name=item["name"], size=item["size"], digest="sha256:" + item["sha256"])
                                  for item in self.local], "zipball_url": "not-an-uploaded-asset"}

    def test_complete_set_and_digests(self):
        self.assertEqual(len(self.local), 12)
        verify_github(self.local, self.remote)

    def test_checksum_assets_are_rejected(self):
        for name in ["SHA256SUMS", "SHA256SUMS.txt", "app.sha256", "sources.sha256", "checksums.txt"]:
            with self.subTest(name=name):
                path = self.directory / name; path.write_text("duplicate checksum")
                with self.assertRaisesRegex(ValueError, "unexpected"):
                    inventory(self.directory, "0.4.1")
                path.unlink()

    def test_missing_and_invalid_digest_fail(self):
        for value in [None, "", "sha256:" + "0" * 64, "sha512:abc"]:
            remote = copy.deepcopy(self.remote); remote["assets"][0]["digest"] = value
            with self.assertRaisesRegex(ValueError, "digest"):
                verify_github(self.local, remote)
        remote = copy.deepcopy(self.remote); del remote["assets"][0]["digest"]
        with self.assertRaisesRegex(ValueError, "digest"):
            verify_github(self.local, remote)

    def test_size_and_asset_set_fail(self):
        remote = copy.deepcopy(self.remote); remote["assets"][0]["size"] += 1
        with self.assertRaises(ValueError):
            verify_github(self.local, remote)
        for assets in [self.remote["assets"][:-1], self.remote["assets"] + self.remote["assets"][:1]]:
            with self.assertRaisesRegex(ValueError, "set differs"):
                verify_github(self.local, {"assets": assets})

    def test_recomputed_local_hash_detects_corruption(self):
        (self.directory / self.local[0]["name"]).write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "digest"):
            verify_github(inventory(self.directory, "0.4.1"), self.remote)

    def test_missing_asset_fails(self):
        (self.directory / self.local[0]["name"]).unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            inventory(self.directory, "0.4.1")


if __name__ == "__main__":
    unittest.main()
