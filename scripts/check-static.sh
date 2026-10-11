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
checker="${CPPCHECK:-cppcheck}"
"$checker" --version
shellcheck --version
bash -n install.sh scripts/*.sh
shellcheck -x install.sh scripts/*.sh
# Both analyzers use only owned TUs; generated/vendor TUs are excluded.
static_dir="$(mktemp -d "${TMPDIR:-/tmp}/transcribe-static.XXXXXX")"
trap 'rm -rf -- "$static_dir"' EXIT
python3 scripts/static-project.py "$build_dir" "$project_dir" "$static_dir"
mapfile -t sources < "$static_dir/sources.txt"
"$tidy" -p "$static_dir/tidy" "${sources[@]}"
checker_options=()
moc_revision="$(<"$static_dir/moc-revision.txt")"
if [[ -n "$moc_revision" ]]; then
  # Model Qt macros and match the SDK's MOC header guards; do not suppress them.
  # QML's static-plugin registration macro expands to SDK/generated glue.
  checker_options+=(--library=qt "-DQ_MOC_OUTPUT_REVISION=$moc_revision" '-DQ_IMPORT_QML_PLUGIN(x)=')
fi
# Qt 6.8 MOC also checks a marker declared by qtmochelpers.h. Match the actual
# SDK header, rather than suppressing generated-header configuration errors.
mapfile -t qt_defines < "$static_dir/cppcheck-qt-defines.txt"
for definition in "${qt_defines[@]}"; do checker_options+=("-D$definition"); done
"$checker" --project="$static_dir/compile_commands.json" --std=c++23 \
  --enable=warning,performance,portability --error-exitcode=1 --inline-suppr "${checker_options[@]}"
