#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == --container ]]; then
    shift
    export TRANSCRIBE_PROJECT_DIR="$project_dir"
    TRANSCRIBE_BUILD_UID="$(id -u)"; TRANSCRIBE_BUILD_GID="$(id -g)"
    export TRANSCRIBE_BUILD_UID TRANSCRIBE_BUILD_GID
    mkdir -p -- "$project_dir/.cache/linux-package-container/home" "$project_dir/dist"
    docker compose -f "$project_dir/packaging/linux/compose.yml" run --build --rm builder \
        python3 /source/scripts/package-linux.py --qt-root /opt/Qt/6.8.3/gcc_64 \
        --work-directory /work --destination /output "$@"
else
    exec python3 "$project_dir/scripts/package-linux.py" "$@"
fi
