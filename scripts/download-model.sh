#!/usr/bin/env bash
set -euo pipefail
selection="${1:-medium}"
if (( $# > 1 )); then
  printf 'Использование: bash scripts/download-model.sh small|medium|turbo\n' >&2
  exit 2
fi
case "$selection" in
  small|medium|turbo) ;;
  *) printf 'Использование: bash scripts/download-model.sh small|medium|turbo\n' >&2; exit 2 ;;
esac
# shellcheck source=scripts/models.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/models.sh"
root="${TRANSCRIBE_HOME:-${HOME:?Не задан HOME}/.local/share/transcribe}"
require_tools curl sha256sum flock mkdir mv rm
model_info "$selection"
lock_models "$root"
download_model "$root" "$selection"
