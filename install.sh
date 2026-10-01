#!/usr/bin/env bash
set -euo pipefail
trap 'printf "Установка не завершена. Устрани ошибку выше и запусти install.sh снова.\n" >&2' ERR
if (( EUID == 0 )); then
  printf 'Запускай install.sh обычным пользователем, без sudo.\n' >&2
  exit 1
fi
selection="${1:-medium}"
if (( $# > 1 )); then
  printf 'Использование: bash install.sh small|medium|turbo\n' >&2
  exit 2
fi
case "$selection" in small|medium|turbo) ;; *) printf 'Использование: bash install.sh small|medium|turbo\n' >&2; exit 2 ;; esac
jobs="${TRANSCRIBE_BUILD_JOBS:-2}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf 'TRANSCRIBE_BUILD_JOBS должен быть положительным целым\n' >&2; exit 2; }
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/models.sh
source "$project_dir/scripts/models.sh"
root="${TRANSCRIBE_HOME:-${HOME:?Не задан HOME}/.local/share/transcribe}"
bin_dir="${HOME:?Не задан HOME}/.local/bin"
require_tools g++ cmake git curl ffmpeg yt-dlp sha256sum flock mktemp install mkdir mv rm
model_info "$selection"
model_info vad
revision=927cfce34f31707e17f2bff35c349632fb9e2c3a
source_dir="$root/whisper.cpp"
# Check existing sources before creating installation directories.
if [[ -e "$source_dir" ]]; then
  actual_revision="$(git -C "$source_dir" rev-parse HEAD)"
  if [[ "$actual_revision" != "$revision" ]]; then
    printf 'В %s находится другая версия whisper.cpp. Укажи отдельный TRANSCRIBE_HOME.\n' "$source_dir" >&2
    exit 1
  fi
  if [[ -n "$(git -C "$source_dir" status --porcelain --untracked-files=no)" ]]; then
    printf 'Исходники whisper.cpp изменены: %s. Укажи отдельный TRANSCRIBE_HOME.\n' "$source_dir" >&2
    exit 1
  fi
fi
lock_models "$root"
staging="$(mktemp -d "$root/.install.XXXXXX")"
binary_stage=''
engine_stage=''
cleanup() {
  rm -rf -- "$staging"
  if [[ -n "$binary_stage" ]]; then rm -f -- "$binary_stage"; fi
  if [[ -n "$engine_stage" ]]; then rm -f -- "$engine_stage"; fi
}
trap cleanup EXIT
if [[ ! -e "$source_dir" ]]; then
  git clone --depth 1 --branch v1.9.4 https://github.com/ggml-org/whisper.cpp.git "$staging/source"
  [[ "$(git -C "$staging/source" rev-parse HEAD)" == "$revision" ]] || {
    printf 'Ревизия загруженного whisper.cpp не совпадает с закреплённой.\n' >&2
    exit 1
  }
  mv -- "$staging/source" "$source_dir"
fi
# Build in staging: a failed build cannot overwrite installed executables.
cmake -S "$source_dir" -B "$staging/whisper-build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_BACKEND_DL=OFF \
  -DGGML_CUDA=OFF -DGGML_VULKAN=OFF -DGGML_BLAS=OFF \
  -DWHISPER_BUILD_TESTS=OFF
cmake --build "$staging/whisper-build" --config Release --target whisper-cli --parallel "$jobs"
cmake -S "$project_dir" -B "$staging/project-build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$staging/project-build" --parallel "$jobs"
download_model "$root" "$selection"
download_model "$root" vad
"$staging/project-build/transcribe" --version
"$staging/whisper-build/bin/whisper-cli" --help >/dev/null
printf '%s\n' "$selection" > "$staging/default-model"
mkdir -p -- "$bin_dir" "$source_dir/build/bin"
binary_stage="$(mktemp "$bin_dir/.transcribe.XXXXXX")"
engine_stage="$(mktemp "$source_dir/build/bin/.whisper-cli.XXXXXX")"
install -m 755 "$staging/project-build/transcribe" "$binary_stage"
install -m 755 "$staging/whisper-build/bin/whisper-cli" "$engine_stage"
# Each rename is atomic. These three separate paths are not one transaction.
mv -f -- "$engine_stage" "$source_dir/build/bin/whisper-cli"
engine_stage=''
mv -f -- "$binary_stage" "$bin_dir/transcribe"
binary_stage=''
mv -f -- "$staging/default-model" "$root/default-model"
printf '\nУстановлено: %s/transcribe\nМодель: %s\n' "$bin_dir" "$selection"
# Print a command to copy, preserving the user's future HOME and PATH.
# shellcheck disable=SC2016
printf 'Добавь ~/.local/bin в PATH, если каталог ещё не включён:\nexport PATH="$HOME/.local/bin:$PATH"\n'
if [[ -n "${TRANSCRIBE_HOME:-}" ]]; then
  printf 'Для запуска сохраняй TRANSCRIBE_HOME=%s в окружении.\n' "$root"
fi
"$bin_dir/transcribe" --help
