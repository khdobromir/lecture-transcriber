#!/usr/bin/env bash
set -euo pipefail
root="${VKLECTURE_HOME:-$HOME/.local/share/vklecture}"
selection="${1:-medium}"
case "$selection" in
  small) name=small-q5_1 ;;
  medium) name=medium-q5_0 ;;
  turbo) name=large-v3-turbo-q5_0 ;;
  *) printf 'Использование: bash scripts/download-model.sh small|medium|turbo\n' >&2; exit 2 ;;
esac
mkdir -p "$root/models"
target="$root/models/ggml-$name.bin"
if [[ -s "$target" ]]; then
  printf 'Модель уже есть: %s\n' "$target"
  exit 0
fi
# Не публикуем неполный файл как готовую модель. .part можно докачать.
curl --fail --location --retry 3 --continue-at - \
  --output "$target.part" \
  "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$name.bin"
[[ -s "$target.part" ]]
mv -- "$target.part" "$target"
printf 'Скачано: %s\n' "$target"
