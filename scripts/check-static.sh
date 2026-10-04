#!/usr/bin/env bash
set -euo pipefail
if (( $# != 1 )); then
  printf 'Usage: bash scripts/check-static.sh BUILD-DIRECTORY\n' >&2
  exit 2
fi
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd -- "$project_dir"
build_dir="$1"
[[ -f "$build_dir/compile_commands.json" ]] || { printf 'Missing compile_commands.json\n' >&2; exit 1; }
tidy="${CLANG_TIDY:-clang-tidy}"
"$tidy" --version
cppcheck --version
shellcheck --version
bash -n install.sh scripts/*.sh
shellcheck -x install.sh scripts/*.sh
# Only project-owned translation units; no generated or third-party source trees.
mapfile -t sources < <(python3 - "$build_dir/compile_commands.json" "$project_dir" <<'PYTHON'
import json
from pathlib import Path
import sys
root = Path(sys.argv[2]).resolve()
files = set()
for command in json.loads(Path(sys.argv[1]).read_text()):
    path = Path(command['file']).resolve()
    if path.is_relative_to(root) and path.relative_to(root).parts[0] in {'src', 'tests', 'gui'}:
        files.add(str(path))
print('\n'.join(sorted(files)))
PYTHON
)
(( ${#sources[@]} > 0 )) || { printf 'No project sources in compilation database\n' >&2; exit 1; }
"$tidy" -p "$build_dir" "${sources[@]}"
cppcheck --project="$build_dir/compile_commands.json" --std=c++23 \
  --enable=warning,performance,portability --error-exitcode=1 --inline-suppr
