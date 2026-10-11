"""Compare declared Whisper revisions offline, without executing build scripts."""
import json
from pathlib import Path
import re
import tempfile
import unittest


SHELL_DECLARATIONS = ("install.sh", "scripts/ci-real-smoke.sh")
MANIFESTS = ("packaging/linux/dependencies.json", "packaging/windows/dependencies.json")
WORKFLOW = ".github/workflows/ci.yml"


def declared_pins(root):
    pins = {}

    def declaration(path, name, pattern):
        matches = re.findall(pattern, (root / path).read_text(encoding="utf-8"), re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f"{path}: expected one {name} declaration, found {len(matches)}")
        pins[f"{path}:{name}"] = matches[0]

    for path in SHELL_DECLARATIONS:
        declaration(path, "revision", r"^revision=([0-9a-f]{40})$")
    declaration(WORKFLOW, "Whisper fetch", r'^\s*git -C "\$engine_source" fetch --depth 1 origin ([0-9a-f]{40})$')
    declaration(WORKFLOW, "Whisper checkout", r'^\s*git -C "\$engine_source" checkout --detach ([0-9a-f]{40})$')
    for path in MANIFESTS:
        try:
            manifest = json.loads((root / path).read_text(encoding="utf-8"))
            whisper = manifest["downloads"]["whisper"] if "linux" in path else manifest["whisper"]
            revision = whisper["revision"]
            if not isinstance(revision, str) or not re.fullmatch(r"[0-9a-f]{40}", revision):
                raise ValueError("Whisper revision must be a full lowercase commit SHA")
            pins[path + ":revision"] = revision
            if "linux" in path:
                prefix = "https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/"
                url = whisper["url"]
                if not isinstance(url, str) or not url.startswith(prefix) or not re.fullmatch(r"[0-9a-f]{40}", url[len(prefix):]):
                    raise ValueError("Whisper URL must pin a full commit SHA")
                pins[path + ":URL"] = url[len(prefix):]
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"{path}: {error}") from error
    expected = pins["install.sh:revision"]
    mismatches = [f"{name}={value}" for name, value in pins.items() if value != expected]
    if mismatches:
        raise ValueError(f"Whisper pins differ from install.sh revision {expected}: " + "; ".join(mismatches))
    return pins


class DependencyPinTests(unittest.TestCase):
    def fixture(self, root, revision="a" * 40):
        files = {path: f"revision={revision}\n" for path in SHELL_DECLARATIONS}
        files[WORKFLOW] = (f'git -C "$engine_source" fetch --depth 1 origin {revision}\n'
                           f'git -C "$engine_source" checkout --detach {revision}\n')
        files[MANIFESTS[0]] = json.dumps({"downloads": {"whisper": {
            "revision": revision, "url": "https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/" + revision}}})
        files[MANIFESTS[1]] = json.dumps({"whisper": {"revision": revision}})
        for name, content in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        return files

    def test_repository_pins_agree(self):
        self.assertEqual(len(declared_pins(Path(__file__).resolve().parents[1])), 7)

    def test_one_changed_declaration_is_rejected(self):
        declarations = [(name, 0) for name in (*SHELL_DECLARATIONS, WORKFLOW, *MANIFESTS)]
        declarations += [(WORKFLOW, 1), (MANIFESTS[0], 1)]
        for name, occurrence in declarations:
            with self.subTest(path=name, occurrence=occurrence), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                files = self.fixture(root)
                content = files[name]
                start = list(re.finditer("a" * 40, content))[occurrence].start()
                (root / name).write_text(content[:start] + "b" * 40 + content[start + 40:], encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Whisper pins differ"):
                    declared_pins(root)

    def test_consistent_update_is_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.fixture(root, "b" * 40)
            self.assertEqual(set(declared_pins(root).values()), {"b" * 40})

    def test_missing_or_ambiguous_shell_pin_is_rejected(self):
        for content in ("revision=short\n", "revision=" + "a" * 40 + "\nrevision=" + "a" * 40 + "\n"):
            with self.subTest(content=content), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.fixture(root)
                (root / "install.sh").write_text(content, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "install.sh: expected one revision declaration"):
                    declared_pins(root)


if __name__ == "__main__":
    unittest.main()
