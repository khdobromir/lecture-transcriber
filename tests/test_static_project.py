"""Verify source selection and the narrow Qt analyzer include workaround."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class StaticProjectTests(unittest.TestCase):
    def test_owned_sources_and_only_qt_system_includes(self):
        with tempfile.TemporaryDirectory(prefix="transcribe-static-test-") as temporary:
            root = Path(temporary)
            build, output = root / "build", root / "analysis"
            build.mkdir()
            output.mkdir()
            qt = root / "Qt SDK"
            (qt / "QtCore").mkdir(parents=True)
            (qt / "QtNetwork").mkdir()
            (qt / "QtCore/qtmetamacros.h").write_text("#define Q_MOC_OUTPUT_REVISION 68\n")
            entries = [{"directory": str(build), "file": str(root / source),
                        "arguments": ["clang++", "-isystem", str(qt), "-isystem", str(qt / "QtCore"),
                                      "-isystem", str(qt / "QtNetwork"), "-isystem", "/other-sdk", "-c", str(root / source)]}
                       for source in ["src/main.cpp", "gui/models.cpp", "tests/test.cpp", "gui/vendor/test.cpp", "build/generated.cpp"]]
            (build / "compile_commands.json").write_text(json.dumps(entries))
            script = Path(__file__).resolve().parents[1] / "scripts/static-project.py"
            subprocess.run([sys.executable, str(script), str(build), str(root), str(output)], check=True)
            self.assertEqual(json.loads((output / "compile_commands.json").read_text()), entries[:3])
            selected = json.loads((output / "tidy/compile_commands.json").read_text())
            self.assertEqual(len(selected), 3)
            for entry in selected:
                self.assertEqual(entry["arguments"][1:9], ["-I", str(qt), "-I", str(qt / "QtCore"),
                                                          "-I", str(qt / "QtNetwork"), "-isystem", "/other-sdk"])
            self.assertEqual((output / "moc-revision.txt").read_text(), "68")
            self.assertEqual(json.loads((build / "compile_commands.json").read_text()), entries)


if __name__ == "__main__":
    unittest.main()
