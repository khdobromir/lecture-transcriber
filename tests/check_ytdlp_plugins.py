"""Explicit, network-free plugin isolation smoke for an installed/bundled yt-dlp.

Usage: python tests/check_ytdlp_plugins.py /absolute/path/to/yt-dlp[.exe]
Not an ordinary CTest dependency: no downloader installation is needed for units.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def check(binary, environment=None):
    binary = Path(binary).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="transcribe-plugin-smoke-") as temporary:
        root = Path(temporary)
        plugin = root / "plugins/owned/yt_dlp_plugins/extractor/transcribe_marker.py"
        plugin.parent.mkdir(parents=True)
        plugin.write_text(
            "import os\nfrom pathlib import Path\n"
            "from yt_dlp.extractor.common import InfoExtractor\n"
            "Path(os.environ['TRANSCRIBE_PLUGIN_MARKER']).write_text('loaded')\n"
            "class TranscribeMarkerIE(InfoExtractor):\n"
            "    _VALID_URL = r'transcribe-marker:(?P<id>.+)'\n",
            encoding="utf-8",
        )
        marker = root / "marker"
        environment = dict(os.environ if environment is None else environment,
                           TRANSCRIBE_PLUGIN_MARKER=str(marker))
        environment.pop("YTDLP_NO_PLUGINS", None)
        environment.pop("PYTHONPATH", None)
        common = [str(binary), "--ignore-config", "--no-cache-dir"]
        owned = ["--plugin-dirs", str(root / "plugins")]
        info = root / "info.json"
        info.write_text(json.dumps({"id": "fixture", "title": "fixture", "ext": "wav",
                                    "url": "https://example.invalid/fixture.wav"}), encoding="utf-8")
        operation = ["--simulate", "--load-info-json", str(info)]
        # Clear defaults first: the positive control executes only our own plugin.
        # --list-extractors returns before configured plugin loading in yt-dlp.
        subprocess.run(common + ["--no-plugin-dirs"] + owned + operation,
                       cwd=root, env=environment, check=True, capture_output=True, timeout=30)
        if not marker.is_file():
            raise RuntimeError("Positive control did not reach plugin loading")
        marker.unlink()
        subprocess.run(common + owned + ["--no-plugin-dirs"] + operation,
                       cwd=root, env=environment, check=True, capture_output=True, timeout=30)
        if marker.exists():
            raise RuntimeError("--no-plugin-dirs executed the marker plugin")
        version = subprocess.check_output([str(binary), "--ignore-config", "--no-plugin-dirs", "--version"],
                                          cwd=root, env=environment, text=True, timeout=30).strip()
        return {"version": version, "positive_control": True, "plugins_disabled": True, "network": False}


if __name__ == "__main__":
    print(json.dumps(check(sys.argv[1])))
