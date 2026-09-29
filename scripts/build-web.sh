#!/usr/bin/env bash
# Build the citsy browser player and copy it into the Chili creator.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EMSDK="${EMSDK:-$ROOT/.cache/emsdk}"
if [[ ! -f "$EMSDK/emsdk_env.sh" ]]; then
  echo "emsdk not found at $EMSDK" >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$EMSDK/emsdk_env.sh"
cmake -B "$ROOT/build-web" -S "$ROOT" \
  -DCMAKE_TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" \
  -DCITSY_BUILD_WEB=ON \
  -DCITSY_BUILD_TESTS=OFF \
  -DCITSY_BUILD_EXAMPLES=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-web" --target citsy_web -j
OUT="$ROOT/../chili-platform-frontend/public/creator/citsy"
mkdir -p "$OUT"
js="$(find "$ROOT/build-web" -name 'citsy.js' -print -quit)"
wasm="$(find "$ROOT/build-web" -name 'citsy.wasm' -print -quit)"
cp "$js" "$wasm" "$OUT/"
echo "copied citsy.js and citsy.wasm to $OUT"
