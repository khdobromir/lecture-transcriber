"""Prepare pinned public speech and one model in an isolated candidate directory."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import urllib.request


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def fetch(url, target, expected, source=None):
    if target.is_file() and digest(target) == expected:
        return
    partial = target.with_suffix(target.suffix + ".part")
    if source:
        shutil.copyfile(source, partial)
    else:
        request = urllib.request.Request(url, headers={"User-Agent": "lecture-transcriber-candidate-validation/1"})
        with urllib.request.urlopen(request, timeout=120) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out, 1024 * 1024)
    if digest(partial) != expected:
        raise RuntimeError("Candidate dependency SHA-256 mismatch: " + target.name)
    partial.replace(target)


parser = argparse.ArgumentParser()
parser.add_argument("workspace", type=Path)
parser.add_argument("--ffmpeg", default="ffmpeg")
parser.add_argument("--audio-source", type=Path)
parser.add_argument("--model-source", type=Path)
parser.add_argument("--engine", type=Path)
args = parser.parse_args()
root = args.workspace.resolve()
root.mkdir(parents=True, exist_ok=True)
home = root / "data"
(home / "models").mkdir(parents=True, exist_ok=True)
if args.engine:
    engine_directory = home / "whisper.cpp/build/bin"
    engine_directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.engine, engine_directory / args.engine.name)
    for runtime in args.engine.parent.glob("*.dll"):
        shutil.copy2(runtime, engine_directory / runtime.name)
project = Path(__file__).resolve().parents[1]
fixture = json.loads((project / "tests/fixtures/russian-speech.json").read_text(encoding="utf-8"))
source = root / "russian-source.ogg"
fetch(fixture["source_url"], source, fixture["source_sha256"], args.audio_source)
audio = root / "Русская речь 😀.wav"
subprocess.run([args.ffmpeg, "-nostdin", "-loglevel", "error", "-y", "-ss", str(fixture["clip_start_seconds"]),
                "-i", str(source), "-t", str(fixture["clip_duration_seconds"]), "-ac", "1", "-ar", "16000",
                "-c:a", "pcm_s16le", "-map_metadata", "-1", "-bitexact", str(audio)], check=True)
selection, filename, repository, revision, expected = next(line.split() for line in
    (project / "scripts/models.tsv").read_text(encoding="utf-8").splitlines() if line.startswith("small "))
model = home / "models" / filename
fetch(f"https://huggingface.co/{repository}/resolve/{revision}/{filename}", model, expected, args.model_source)
environment = {"TRANSCRIBE_REAL_HOME": str(home), "TRANSCRIBE_REAL_AUDIO": str(audio), "TRANSCRIBE_REAL_MODEL": selection}
evidence = {"environment": environment, "source_audio_sha256": fixture["source_sha256"],
            "audio_sha256": digest(audio), "model_sha256": expected,
            "engine_sha256": digest(args.engine) if args.engine else None,
            "expected_whisper_revision": "927cfce34f31707e17f2bff35c349632fb9e2c3a", "model_repository": repository,
            "model_revision": revision, "license": fixture["license"], "attribution": fixture["attribution"],
            "ffmpeg": subprocess.check_output([args.ffmpeg, "-version"], text=True).splitlines()[0]}
(root / "prerequisites.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
print("Pinned public speech/model prepared; SHA-256 recorded in prerequisites.json")
