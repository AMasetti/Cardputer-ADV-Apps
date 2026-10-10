#!/usr/bin/env bash
# Compila Doom (romalik/m5cardputer_doom) con los parches de esta carpeta.
# Contrato con scripts/build-app.sh: deja el binario de app en "$1/firmware.bin".
#
# Upstream se compila con ESP-IDF v4.4.6 (no PlatformIO). Si hay un idf.py en
# el PATH se usa ese; si no, se corre dentro de la imagen espressif/idf:v4.4.6.
set -euo pipefail

OUT_DIR="$(realpath -m "${1:?uso: build.sh <out_dir>}")"
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK_DIR:-$APP_DIR/.work}"

REPO=$(jq -r '.upstream.repo' "$APP_DIR/app.json")
REF=$(jq -r '.upstream.ref' "$APP_DIR/app.json")
IDF_IMAGE=$(jq -r '.upstream.idfImage' "$APP_DIR/app.json")
SRC="$WORK/m5cardputer_doom-${REF:0:12}"

if [ ! -d "$SRC/.git" ]; then
  echo ">> Clonando $REPO @ $REF"
  rm -rf "$SRC"
  git init --quiet "$SRC"
  git -C "$SRC" fetch --quiet --depth 1 "$REPO" "$REF"
  git -C "$SRC" checkout --quiet FETCH_HEAD
fi

# Árbol limpio en cada build (conserva build/ y managed_components/ para cachear)
git -C "$SRC" checkout --quiet -- .
git -C "$SRC" clean -fdq -e build -e managed_components

# El IWAD shareware embebido (16 MB de C) no se compila ni se redistribuye:
# el WAD se carga desde la microSD. CMake lo levantaría con GLOB_RECURSE.
rm -f "$SRC/main/doom/source/iwad/doom1.c"

for p in "$APP_DIR"/patches/*.patch; do
  echo ">> Aplicando $(basename "$p")"
  git -C "$SRC" apply "$p"
done

if command -v idf.py >/dev/null; then
  echo ">> idf.py build (local)"
  (cd "$SRC" && idf.py build)
else
  echo ">> idf.py build en $IDF_IMAGE"
  docker run --rm \
    -u "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$SRC":/project -w /project \
    "$IDF_IMAGE" idf.py build
fi

mkdir -p "$OUT_DIR"
cp "$SRC/build/cardputer_doom.bin" "$OUT_DIR/firmware.bin"
echo ">> OK: $OUT_DIR/firmware.bin"
