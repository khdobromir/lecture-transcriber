"""Check changed owned C++ lines against a base commit; never format vendor trees."""
import argparse
import difflib
import os
from pathlib import Path
import re
import subprocess


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments])


def changed_ranges(diff):
    ranges = []
    for match in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", diff, re.MULTILINE):
        start = int(match[1])
        count = int(match[2]) if match[2] is not None else 1
        if count:
            ranges.append((start, start + count - 1))
    return ranges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default=os.environ.get("FORMAT_BASE", ""), help="Base revision; defaults to merge-base with origin/master")
    parser.add_argument("--clang-format", default=os.environ.get("CLANG_FORMAT", "clang-format"))
    parser.add_argument("--fix", action="store_true", help="Apply formatting only to changed ranges")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = subprocess.check_output([args.clang_format, "--version"], text=True).strip()
    if not re.search(r"clang-format version 18\.1\.8(?:\s|$)", version):
        parser.error(f"Expected clang-format 18.1.8, got {version}; see CONTRIBUTING.md")
    reference = args.base if args.base and set(args.base) != {"0"} else git(root, "merge-base", "origin/master", "HEAD").decode().strip()
    base = git(root, "rev-parse", "--verify", "--end-of-options", reference + "^{commit}").decode().strip()
    paths = git(root, "diff", "--no-ext-diff", "--no-textconv", "--no-renames", "--name-only", "--diff-filter=ACM", "-z", base, "--", "src", "gui", "tests").split(b"\0")
    failed = checked = 0
    for raw in paths:
        if not raw:
            continue
        path = Path(os.fsdecode(raw))
        if path.parent not in {Path("src"), Path("gui"), Path("tests")} or path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        source = root / path
        if source.is_symlink():
            parser.error(f"Owned source must not be a symlink: {path}")
        diff = git(root, "diff", "--no-ext-diff", "--no-textconv", "--no-renames", "--unified=0", base, "--", str(path)).decode("utf-8")
        ranges = changed_ranges(diff)
        if not ranges:
            continue
        command = [args.clang_format, "--style=file", "--assume-filename=" + str(source)]
        command += [f"--lines={start}:{end}" for start, end in ranges]
        original = source.read_bytes()
        formatted = subprocess.check_output(command, input=original, cwd=root)
        checked += 1
        if original == formatted:
            continue
        if args.fix:
            source.write_bytes(formatted)
            print(f"Formatted changed ranges: {path}")
        else:
            failed += 1
            print("".join(difflib.unified_diff(original.decode("utf-8").splitlines(keepends=True),
                formatted.decode("utf-8").splitlines(keepends=True), fromfile=str(path), tofile=str(path) + " (formatted)")), end="")
    print(f"Checked {checked} changed owned C++ files against {base[:12]} using {version}; {failed} need formatting.")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
