"""Test one builder-selected AppImage offline; never build or select by glob."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from linux_package import digest


def prepare_inputs(record_path, inputs, project):
    record = json.loads(record_path.read_text())
    if Path(record["filename"]).name != record["filename"] or Path(record["stage"]).name != record["stage"]:
        raise ValueError("Candidate paths must be basenames")
    image = record_path.parent / record["filename"]
    if digest(image) != record["sha256"]:
        raise ValueError("Candidate AppImage checksum changed after builder verification")
    shutil.copy2(image, inputs / "Transcribe.AppImage")
    sums = [record["sha256"] + "  Transcribe.AppImage"]
    if record["real_smoke"]:
        prerequisites = project / ".cache/linux-package-container" / record["stage"] / "real-prerequisites"
        metadata = json.loads((prerequisites / "prerequisites.json").read_text())
        # Container paths in the metadata are resolved within this exact stage.
        for name, key, checksum in [("audio.wav", "TRANSCRIBE_REAL_AUDIO", "audio_sha256"),
                                    ("model.bin", None, "model_sha256")]:
            source = prerequisites / Path(metadata["environment"][key]).name if key else prerequisites / "data/models/ggml-small-q5_1.bin"
            if digest(source) != metadata[checksum]:
                raise ValueError("Public fixture checksum changed: " + name)
            shutil.copy2(source, inputs / name)
            sums.append(metadata[checksum] + "  " + name)
    (inputs / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("record", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    record_path = args.record.resolve()
    evidence = record_path.parent / "clean-evidence"
    evidence.mkdir(exist_ok=False)
    with tempfile.TemporaryDirectory(prefix="transcribe-clean-inputs-") as temporary:
        inputs = Path(temporary)
        record = prepare_inputs(record_path, inputs, project)
        for name, base in [("ubuntu-24.04", "ubuntu:24.04"), ("debian-13", "debian:13-slim")]:
            output = evidence / name
            output.mkdir()
            subprocess.run(["docker", "pull", base], check=True)
            image_id = subprocess.check_output(["docker", "image", "inspect", "--format", "{{.Id}}", base], text=True).strip()
            # Exercise ordinary-user launch and keep private result directories
            # readable by the runner that uploads the evidence.
            command = ["docker", "run", "--rm", "--network", "none", "--user", f"{os.getuid()}:{os.getgid()}",
                       "-e", "REQUIRE_ASR=" + str(int(record["real_smoke"])),
                       "-v", str(inputs) + ":/inputs:ro", "-v", str(output) + ":/evidence",
                       "-v", str(project / "tests/smoke_linux_clean.sh") + ":/smoke.sh:ro", base, "sh", "/smoke.sh"]
            with (output / "container.log").open("wb") as log:
                subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=900)
            result = dict(record, container=base, container_image=image_id, offline=True)
            (output / "validation.json").write_text(json.dumps(result, indent=2) + "\n")
        if digest(record_path.parent / record["filename"]) != record["sha256"]:
            raise ValueError("Candidate changed during clean verification")


if __name__ == "__main__":
    main()
