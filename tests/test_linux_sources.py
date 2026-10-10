"""Exact-version source retention must fail closed without executing sources."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from package_linux_sources import collect_sources, select_source
from package_inputs import archive_inputs


def stanza(version="1:2.0-3", filename="demo_2.0.orig.tar.xz", content=b"source archive"):
    return (f"Package: demo\nVersion: {version}\nDirectory: pool/main/d/demo\n"
            f"Checksums-Sha256:\n {hashlib.sha256(content).hexdigest()} {len(content)} {filename}\n"
            f" {'a' * 64} 5 demo_2.0-3.dsc\n\n")


class LinuxSourcesTests(unittest.TestCase):
    def test_exact_version_and_conflicting_indexes(self):
        text = stanza("2.0-2") + stanza() + stanza()
        self.assertEqual({record["filename"] for record in select_source(text, "demo", "1:2.0-3")},
                         {"demo_2.0.orig.tar.xz", "demo_2.0-3.dsc"})
        with self.assertRaisesRegex(ValueError, "Exact source version unavailable"):
            select_source(text, "demo", "2.0-4")
        with self.assertRaisesRegex(ValueError, "Conflicting source metadata"):
            select_source(stanza() + stanza(content=b"different"), "demo", "1:2.0-3")

    def test_rejects_unsafe_and_incomplete_source_metadata(self):
        for name in ["../escape.tar.xz", "sub/file.tar.xz", "a\\b", "a:b"]:
            with self.subTest(name=name), self.assertRaises(ValueError):
                select_source(stanza(filename=name), "demo", "1:2.0-3")
        for text in [stanza().replace("Checksums-Sha256", "Checksums-Sha1"),
                     stanza().replace("demo_2.0-3.dsc", "other.tar.xz"),
                     stanza().replace("Package: demo", "Package: other")]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                select_source(text, "demo", "1:2.0-3")

    def test_download_is_only_source_archives_and_shared_packages_are_deduplicated(self):
        content = b"source archive"
        metadata = stanza().replace('a' * 64, hashlib.sha256(b"dsc!!").hexdigest())
        libs = [dict(provider="deb", payload=f"usr/lib/libdemo{n}.so", payload_sha256=str(n) * 64,
                     source_package="demo", source_version="1:2.0-3") for n in [1, 2]]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"; cache.mkdir()
            def download(command, **kwargs):
                self.assertEqual(command, ["apt-get", "source", "--download-only", "--only-source", "demo=1:2.0-3"])
                self.assertTrue(kwargs["check"])
                destination = Path(kwargs["cwd"])
                (destination / "demo_2.0.orig.tar.xz").write_bytes(content)
                (destination / "demo_2.0-3.dsc").write_bytes(b"dsc!!")
            with patch("package_linux_sources.query", return_value=metadata) as query, \
                    patch("package_linux_sources.subprocess.run", side_effect=download) as run:
                # Non-ELF data payloads must join the same exact-source mapping.
                result = collect_sources(dict(libraries=libs[:1], data_files=libs[1:]), cache)
                self.assertEqual(query.call_args.args[0], ["apt-cache", "showsrc", "--only-source", "demo"])
                self.assertEqual(run.call_count, 1)
                self.assertEqual(len(result["packages"]), 1)
                self.assertEqual(result["packages"][0]["payloads"], libs)
                records = result["downloads"]
                self.assertEqual(len(records), 2)
                archive_inputs(records, cache, root / "retained.tar.gz")
                collect_sources(dict(libraries=libs), cache)
                self.assertEqual(run.call_count, 1, "Verified cached source files should be reused")
                (cache / records[0]["filename"]).write_bytes(b"corrupt")
                with self.assertRaisesRegex(ValueError, "Source checksum mismatch"):
                    collect_sources(dict(libraries=libs), cache)

    def test_missing_package_metadata_and_corrupt_download_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            with self.assertRaisesRegex(ValueError, "Incomplete system source metadata"):
                collect_sources(dict(libraries=[dict(provider="deb")]), cache)
            lib = dict(provider="deb", payload="usr/lib/demo.so", payload_sha256="1" * 64,
                       source_package="demo", source_version="1:2.0-3")
            def corrupt(command, **kwargs):
                (Path(kwargs["cwd"]) / "demo_2.0.orig.tar.xz").write_bytes(b"x" * len(b"source archive"))
                (Path(kwargs["cwd"]) / "demo_2.0-3.dsc").write_bytes(b"dsc!!")
            with patch("package_linux_sources.query", return_value=stanza()), \
                    patch("package_linux_sources.subprocess.run", side_effect=corrupt), \
                    self.assertRaisesRegex(ValueError, "Source checksum mismatch"):
                collect_sources(dict(libraries=[lib]), cache)
            self.assertEqual(list(cache.iterdir()), [], "No unverified files retained")


if __name__ == "__main__":
    unittest.main()
