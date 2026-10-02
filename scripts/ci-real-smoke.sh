#!/usr/bin/env bash
# CI-only bootstrap: reviewed pinned sources, no installer, no global installation.
set -euo pipefail
if (( $# != 2 )); then
  printf 'Usage: bash scripts/ci-real-smoke.sh BUILD-DIRECTORY NEW-WORKSPACE\n' >&2
  exit 2
fi
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(cd -- "$1" && pwd)"
workspace="$2"
[[ "$workspace" == /* && ! -e "$workspace" && ! -L "$workspace" ]] || {
  printf 'Workspace must be a new absolute path\n' >&2; exit 1;
}
mkdir -- "$workspace"
app="$workspace/app"
revision=927cfce34f31707e17f2bff35c349632fb9e2c3a
mkdir -- "$app"
# No user Git configuration, credential helper, hooks, submodules or Git templates.
GIT_CONFIG_NOSYSTEM=1 GIT_CONFIG_GLOBAL=/dev/null git \
  -c core.hooksPath=/dev/null -c credential.helper= -c init.templateDir= \
  clone --depth 1 --branch v1.9.4 https://github.com/ggml-org/whisper.cpp.git "$app/whisper.cpp"
[[ "$(git -C "$app/whisper.cpp" rev-parse HEAD)" == "$revision" ]] || {
  printf 'Unexpected whisper.cpp revision\n' >&2; exit 1;
}
cmake -S "$app/whisper.cpp" -B "$app/whisper.cpp/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_BACKEND_DL=OFF \
  -DGGML_CUDA=OFF -DGGML_VULKAN=OFF -DGGML_BLAS=OFF -DGGML_CPU_KLEIDIAI=OFF \
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DWHISPER_BUILD_SERVER=OFF -DWHISPER_BUILD_TESTS=OFF
cmake --build "$app/whisper.cpp/build" --target whisper-cli --parallel 2
# shellcheck source=scripts/models.sh
source "$project_dir/scripts/models.sh"
require_tools curl sha256sum flock mkdir mv rm
lock_models "$app"
download_model "$app" medium
download_model "$app" vad
"$build_dir/smoke_real" --binary "$build_dir/transcribe" --app-home "$app" \
  --manifest "$project_dir/scripts/models.tsv" --artifacts "$workspace/results"
