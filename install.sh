#!/usr/bin/env bash
set -euo pipefail
trap 'printf "Установка не завершена. Устрани ошибку выше и запусти install.sh снова.\n" >&2' ERR
if (( EUID == 0 )); then
  printf 'Запускай install.sh обычным пользователем, без sudo.\n' >&2
  exit 1
fi
selection="${1:-medium}"
case "$selection" in small|medium|turbo) ;; *) printf 'Использование: bash install.sh small|medium|turbo\n' >&2; exit 2 ;; esac
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
root="${VKLECTURE_HOME:-$HOME/.local/share/vklecture}"
bin_dir="$HOME/.local/bin"
for tool in g++ cmake git curl ffmpeg yt-dlp; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Нет %s. Сначала выполни:\nsudo pacman -S --needed base-devel cmake git curl ffmpeg yt-dlp unzip\n' "$tool" >&2
    exit 1
  fi
done
mkdir -p "$root/models" "$bin_dir"
# Ограничиваем параллельную сборку, чтобы не перегрузить слабый ноутбук.
jobs="${VKLECTURE_BUILD_JOBS:-2}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { printf 'VKLECTURE_BUILD_JOBS должен быть положительным целым\n' >&2; exit 2; }
revision=927cfce34f31707e17f2bff35c349632fb9e2c3a
source_dir="$root/whisper.cpp"
if [[ ! -d "$source_dir" ]]; then
  git clone --depth 1 --branch v1.9.4 https://github.com/ggml-org/whisper.cpp.git "$source_dir"
fi
actual_revision="$(git -C "$source_dir" rev-parse HEAD)"
if [[ "$actual_revision" != "$revision" ]]; then
  printf 'В %s находится другая версия whisper.cpp. Укажи отдельный VKLECTURE_HOME.\n' "$source_dir" >&2
  exit 1
fi
cmake -S "$source_dir" -B "$source_dir/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DGGML_CUDA=OFF -DGGML_VULKAN=OFF -DGGML_BLAS=OFF \
  -DWHISPER_BUILD_TESTS=OFF
cmake --build "$source_dir/build" --config Release --target whisper-cli --parallel "$jobs"
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build" --parallel "$jobs"
install -m 755 "$project_dir/build/vklecture" "$bin_dir/vklecture"
bash "$project_dir/scripts/download-model.sh" "$selection"
vad="$root/models/ggml-silero-v6.2.0.bin"
if [[ ! -s "$vad" ]]; then
  curl --fail --location --retry 3 --continue-at - --output "$vad.part" \
    'https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin'
  [[ -s "$vad.part" ]]
  mv -- "$vad.part" "$vad"
fi
printf '%s\n' "$selection" > "$root/default-model"
printf '\nУстановлено: %s/vklecture\nМодель: %s\n' "$bin_dir" "$selection"
printf 'Добавь ~/.local/bin в PATH, если каталог ещё не включён:\nexport PATH="$HOME/.local/bin:$PATH"\n'
if [[ -n "${VKLECTURE_HOME:-}" ]]; then
  printf 'Для запуска сохраняй VKLECTURE_HOME=%s в окружении.\n' "$root"
fi
"$bin_dir/vklecture" --help
