"""Check complete GUI translations against fresh lupdate extraction, in a temporary directory."""
import collections
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
catalog = root / "gui/i18n/transcribe_en.ts"


def messages(path):
    tree = ET.parse(path)
    return {(context.findtext("name"), message.findtext("source")): message
            for context in tree.findall("context") for message in context.findall("message")
            if message.find("translation").get("type") not in {"obsolete", "vanished"}}


current = messages(catalog)
for key, message in current.items():
    translation = message.find("translation")
    text = translation.text or ""
    assert text and translation.get("type") != "unfinished", f"Missing translation: {key}"
    placeholders = lambda value: collections.Counter(re.findall(r"%L?\d+|%n", value))
    assert placeholders(key[1]) == placeholders(text), f"Placeholder mismatch: {key}"
with tempfile.TemporaryDirectory(prefix="transcribe-translations-") as directory:
    extracted = Path(directory) / "transcribe_en.ts"
    subprocess.run([sys.argv[1], str(root / "gui"), "-no-obsolete", "-ts", str(extracted)], check=True)
    expected = messages(extracted)
    assert current.keys() == expected.keys(), (f"Missing: {expected.keys() - current.keys()}; "
                                              f"stale: {current.keys() - expected.keys()}")
print(f"PASS {len(current)} complete English GUI translations with matching placeholders and sources")
