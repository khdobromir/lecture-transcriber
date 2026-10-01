#!/usr/bin/env bash
# Shared installer/download functions. The manifest is data, never shell code.
models_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

require_tools() {
  local tool
  for tool in "$@"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      printf 'Нет необходимого инструмента: %s\n' "$tool" >&2
      return 1
    fi
  done
}

model_info() {
  local selection="$1" key extra
  while read -r key model_filename model_repository model_revision model_sha256 extra; do
    [[ "$key" == "$selection" ]] || continue
    if [[ -n "$extra" || ! "$model_filename" =~ ^ggml-[a-zA-Z0-9_.-]+\.bin$ ||
          ! "$model_repository" =~ ^[a-zA-Z0-9_-]+/[a-zA-Z0-9_.-]+$ ||
          ! "$model_revision" =~ ^[a-f0-9]{40}$ || ! "$model_sha256" =~ ^[a-f0-9]{64}$ ]]; then
      printf 'Некорректная запись модели: %s\n' "$selection" >&2
      return 1
    fi
    return 0
  done < "$models_script_dir/models.tsv"
  printf 'Модель отсутствует в манифесте: %s\n' "$selection" >&2
  return 1
}

lock_models() {
  local root="$1"
  mkdir -p -- "$root/models"
  exec {models_lock_fd}> "$root/.install.lock"
  if ! flock --nonblock "$models_lock_fd"; then
    printf 'Другая установка или загрузка уже использует %s. Повтори позже.\n' "$root" >&2
    return 1
  fi
}

verify_model() {
  local path="$1" expected="$2" actual
  [[ -f "$path" ]] || return 1
  actual="$(sha256sum < "$path")" || return 1
  [[ "${actual%% *}" == "$expected" ]]
}

download_model() {
  local root="$1" selection="$2" target
  model_info "$selection"
  target="$root/models/$model_filename"
  if [[ -e "$target" || -L "$target" ]]; then
    if verify_model "$target" "$model_sha256"; then
      printf 'Модель проверена: %s\n' "$target"
      return 0
    fi
    printf 'SHA-256 модели не совпадает: %s\nСуществующий файл сохранён. Перемести его в резервное место и повтори загрузку.\n' "$target" >&2
    return 1
  fi
  if [[ -L "$target.part" ]]; then
    printf 'Временный файл является символьной ссылкой: %s.part\n' "$target" >&2
    return 1
  fi
  # A previous run can be interrupted after the transfer but before rename.
  if verify_model "$target.part" "$model_sha256"; then
    mv -- "$target.part" "$target"
    printf 'Проверена завершённая загрузка: %s\n' "$target"
    return 0
  fi
  # Network failure preserves .part for resuming; a hash failure rejects it.
  curl --fail --location --retry 3 --continue-at - --connect-timeout 30 \
    --proto '=https' --proto-redir '=https' --output "$target.part" \
    "https://huggingface.co/$model_repository/resolve/$model_revision/$model_filename"
  if ! verify_model "$target.part" "$model_sha256"; then
    printf 'SHA-256 загрузки не совпадает: %s.part. Повтори загрузку.\n' "$target" >&2
    rm -f -- "$target.part"
    return 1
  fi
  mv -- "$target.part" "$target"
  printf 'Скачано и проверено: %s\n' "$target"
}
