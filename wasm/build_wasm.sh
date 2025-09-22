#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_WASM="$ROOT_DIR/ui/kolibri.wasm"

CORE_FILES=$(find "$ROOT_DIR/core" -name '*.c' ! -name 'main.c')

mapfile -t CORE_FILES < <(find "$ROOT_DIR/core" -name '*.c' ! -name 'main.c')

clang --target=wasm32-wasi \
  -O2 \
  -nostartfiles \
  -Wl,--export-all \
  -Wl,--no-entry \
  -o "$OUT_WASM" \

  wasm/export.c $CORE_FILES

  wasm/export.c "${CORE_FILES[@]}"

