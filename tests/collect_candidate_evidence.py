"""Write candidate evidence; never infer a release pass from PR merge-tree CI."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from package_archive import extract_verified


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("--source", required=True)
parser.add_argument("--run", required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--linux-result", type=Path)
parser.add_argument("--windows-result", type=Path)
parser.add_argument("--linux-prerequisites", type=Path)
parser.add_argument("--windows-prerequisites", type=Path)
parser.add_argument("--zip", type=Path)
args = parser.parse_args()
ci = json.loads(subprocess.check_output(["gh", "run", "view", args.run, "--repo", "khdobromir/lecture-transcriber",
    "--json", "event,url,headSha,status,conclusion,jobs,createdAt,updatedAt"], text=True))
jobs = [{"name": job["name"], "status": job["status"], "conclusion": job["conclusion"],
         "url": job.get("url", ci["url"] + "/job/" + str(job["databaseId"]))} for job in ci["jobs"]]
exact_ci = (ci["event"] == "workflow_dispatch" and ci["headSha"] == args.source
            and ci["status"] == "completed" and ci["conclusion"] == "success"
            and bool(jobs) and all(job["conclusion"] == "success" for job in jobs))
evidence = {"generated_utc": datetime.now(timezone.utc).isoformat(), "source": args.source,
            "ci": {key: ci[key] for key in ["event", "url", "headSha", "status", "conclusion"]},
            "jobs": jobs, "exact_source_ci_passed": exact_ci, "runtime": {}}
for platform in ["linux", "windows"]:
    path, prerequisite = getattr(args, platform + "_result"), getattr(args, platform + "_prerequisites")
    if path and path.is_file():
        result = json.loads(path.read_text(encoding="utf-8"))
        verified = (result.get("source") == args.source and result.get("completed")
                    and result.get("live_preview") and result.get("first_model_import"))
        if prerequisite and prerequisite.is_file():
            pinned = json.loads(prerequisite.read_text(encoding="utf-8"))
            verified = verified and all(result.get(key) == pinned.get(key) for key in ["audio_sha256", "model_sha256"])
        else:
            pinned, verified = {}, False
        evidence["runtime"][platform] = {"verified_for_source": bool(verified), "result": result, "prerequisites": pinned}
    else:
        evidence["runtime"][platform] = {"verified_for_source": False, "status": "unverified"}
if args.zip:
    with tempfile.TemporaryDirectory(prefix="transcribe-candidate-report-") as temporary:
        _, manifest = extract_verified(args.zip, Path(temporary), args.source)
    evidence["zip"] = {"sha256": digest(args.zip), "manifest": manifest, "source_verified": not manifest.get("dirty", True)}
else:
    evidence["zip"] = {"status": "unverified"}
evidence["manual"] = {"clean_windows_11": "unverified: no machine available", "native_dialogs_clipboard_open_exports": "unverified",
                      "full_manual_gui_smoke": "unverified", "vk_cookies_network": "unverified"}
evidence["extensions"] = {"large_processor_groups_cgroup": "unverified; nonblocking extension"}
evidence["release_ready"] = False
args.output.mkdir(parents=True, exist_ok=True)
(args.output / "validation.json").write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
lines = ["# Candidate validation", "", "Source: `" + args.source + "`", "", "Exact source CI: " + ("passed" if exact_ci else "unverified"),
         "", "| Job | Result |", "|---|---|"]
lines += [f"| [{job['name']}]({job['url']}) | {job['conclusion'] or job['status']} |" for job in jobs]
lines += ["", "| Runtime | Source verification |", "|---|---|"]
lines += [f"| {name} | {'passed' if record['verified_for_source'] else 'unverified'} |" for name, record in evidence["runtime"].items()]
if args.zip:
    lines += ["", "ZIP SHA-256: `" + evidence["zip"]["sha256"] + "`"]
lines += ["", "Release readiness: blocked by outstanding manual checks.", ""]
lines += ["- " + name + ": " + result for name, result in evidence["manual"].items()]
(args.output / "validation.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
print("Candidate evidence written; manual release gates remain unverified")
