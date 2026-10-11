"""Run the candidate harness using isolated pinned prerequisites."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--harness", type=Path, required=True)
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--engine", type=Path, required=True)
parser.add_argument("--workspace", type=Path, required=True)
parser.add_argument("--audio-source", type=Path)
parser.add_argument("--model-source", type=Path)
args = parser.parse_args()
command = [sys.executable, str(Path(__file__).with_name("prepare_real_smoke.py")), str(args.workspace), "--engine", str(args.engine)]
for name in ["audio_source", "model_source"]:
    value = getattr(args, name)
    if value:
        command.extend(["--" + name.replace("_", "-"), str(value)])
subprocess.run(command, check=True, timeout=600)
prerequisites = json.loads((args.workspace / "prerequisites.json").read_text(encoding="utf-8"))
environment = dict(os.environ, **prerequisites["environment"], TRANSCRIBE_REAL_BINARY=str(args.binary.resolve()),
                   TRANSCRIBE_REAL_ARTIFACTS=str(args.workspace / "result"), QT_QPA_PLATFORM="offscreen",
                   QT_QPA_PLATFORMTHEME="generic", QT_QUICK_BACKEND="software")
subprocess.run([str(args.harness.resolve()), "-o", str(args.workspace.resolve() / "gui-real.log") + ",txt"],
               env=environment, check=True, timeout=360)
