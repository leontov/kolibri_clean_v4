#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_WASM="$ROOT_DIR/ui/kolibri.wasm"

mapfile -t CORE_FILES < <(find "$ROOT_DIR/core" -name '*.c' ! -name 'main.c')

if [[ ${#CORE_FILES[@]} -eq 0 ]]; then
  echo "no core sources found" >&2
  exit 1
fi

if ! command -v clang >/dev/null 2>&1; then
  echo "clang (with wasm32-wasi target) is required" >&2
  exit 1
fi

clang --target=wasm32-wasi \
  -O2 \
  -nostartfiles \
  -Wl,--no-entry \
  -Wl,--export=memory \
  -Wl,--strip-debug \
  -o "$OUT_WASM" \
  wasm/export.c "${CORE_FILES[@]}"

echo "built $OUT_WASM"

