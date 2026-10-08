#!/usr/bin/env bash
# Compila Meshtastic upstream con la variante RA-02 de esta carpeta.
# Contrato con scripts/build-app.sh: deja el binario de app en "$1/firmware.bin".
set -euo pipefail

OUT_DIR="$(realpath -m "${1:?uso: build.sh <out_dir>}")"
APP_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK_DIR:-$APP_DIR/.work}"

REPO=$(jq -r '.upstream.repo' "$APP_DIR/app.json")
REF=$(jq -r '.upstream.ref' "$APP_DIR/app.json")
ENV=$(jq -r '.upstream.env' "$APP_DIR/app.json")
FW="$WORK/firmware-$REF"

if [ ! -d "$FW/.git" ]; then
  echo ">> Clonando $REPO @ $REF"
  rm -rf "$FW"
  git clone --quiet --depth 1 --branch "$REF" "$REPO" "$FW"
fi

# Árbol limpio en cada build (conserva .pio para cachear)
git -C "$FW" checkout --quiet -- .
git -C "$FW" clean -fdq -e .pio

VARIANT_DST="$FW/variants/esp32s3/m5stack_cardputer_adv_ra02"
rm -rf "$VARIANT_DST"
mkdir -p "$VARIANT_DST"
cp "$APP_DIR"/variant/* "$VARIANT_DST/"

# Parches opcionales sobre upstream (apps/meshtastic-ra02/patches/*.patch)
if compgen -G "$APP_DIR/patches/*.patch" > /dev/null; then
  for p in "$APP_DIR"/patches/*.patch; do
    echo ">> Aplicando $(basename "$p")"
    git -C "$FW" apply --3way "$p"
  done
fi

# pioarduino corre un "pio run" anidado desde ~/.platformio/penv y lo instala
# con "pioarduino>=6.1.19", que hoy resuelve a 6.2.0 y rompe tool-scons.
# Se crea el penv con pkg install y se fija la versión antes de compilar.
PIO_VERSION="${PIO_VERSION:-6.1.19}"
echo ">> pio pkg install -e $ENV"
pio pkg install -d "$FW" -e "$ENV"
PENV_PY="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/penv/bin/python"
if [ -x "$PENV_PY" ]; then
  echo ">> Fijando pioarduino==$PIO_VERSION en el penv"
  if command -v uv >/dev/null; then
    uv pip install --python "$PENV_PY" "pioarduino==$PIO_VERSION"
  else
    "$PENV_PY" -m pip install "pioarduino==$PIO_VERSION"
  fi
fi

echo ">> pio run -e $ENV"
pio run -d "$FW" -e "$ENV"

mkdir -p "$OUT_DIR"
cp "$FW/.pio/build/$ENV/firmware.bin" "$OUT_DIR/firmware.bin"
echo ">> OK: $OUT_DIR/firmware.bin"
