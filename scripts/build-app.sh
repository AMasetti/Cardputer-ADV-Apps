#!/usr/bin/env bash
# Compila una app y deja el .bin listo para la SD en dist/<app>/.
#
#   scripts/build-app.sh <app> [sufijo]
#
#   sufijo vacío  -> dist/<app>/<app>-v<version>.bin          (release)
#   sufijo "dev.abc123" -> dist/<app>/<app>-v<version>-dev.abc123.bin
#
# Builders (campo "builder" en apps/<app>/app.json):
#   platformio -> pio run -d apps/<app> -e <env>
#   custom     -> apps/<app>/build.sh <out_dir>  (debe dejar firmware.bin)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="${1:?uso: build-app.sh <app> [sufijo]}"
SUFFIX="${2:-}"
APP_DIR="$ROOT/apps/$APP"
MANIFEST="$APP_DIR/app.json"

[ -f "$MANIFEST" ] || { echo "No existe $MANIFEST" >&2; exit 1; }

VERSION=$(jq -r '.version' "$MANIFEST")
BUILDER=$(jq -r '.builder // "platformio"' "$MANIFEST")
FULL_VERSION="$VERSION${SUFFIX:+-$SUFFIX}"
export APP_VERSION="$FULL_VERSION"

TMP_OUT="$ROOT/build/$APP"
rm -rf "$TMP_OUT" && mkdir -p "$TMP_OUT"

case "$BUILDER" in
  platformio)
    ENV=$(jq -r '.env // "cardputer"' "$MANIFEST")
    pio run -d "$APP_DIR" -e "$ENV"
    cp "$APP_DIR/.pio/build/$ENV/firmware.bin" "$TMP_OUT/firmware.bin"
    ;;
  custom)
    bash "$APP_DIR/build.sh" "$TMP_OUT"
    ;;
  *)
    echo "builder desconocido: $BUILDER" >&2; exit 1 ;;
esac

DIST="$ROOT/dist/$APP"
mkdir -p "$DIST"
BIN="$DIST/$APP-v$FULL_VERSION.bin"
cp "$TMP_OUT/firmware.bin" "$BIN"
( cd "$DIST" && sha256sum "$(basename "$BIN")" > "$(basename "$BIN").sha256" )

SIZE=$(stat -c%s "$BIN")
jq -n --arg app "$APP" --arg version "$FULL_VERSION" --arg file "$(basename "$BIN")" \
      --argjson size "$SIZE" --arg sha "$(cut -d' ' -f1 "$BIN.sha256")" \
      --arg commit "${GITHUB_SHA:-$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local)}" \
      '{app:$app, version:$version, file:$file, size:$size, sha256:$sha, commit:$commit}' \
      > "$DIST/build-info.json"

printf '>> %s  (%s KB)\n' "$BIN" "$((SIZE / 1024))"

# El Launcher instala la app en su partición OTA: avisar si se acerca al límite.
LIMIT_KB=${APP_SIZE_LIMIT_KB:-3264}
if [ $((SIZE / 1024)) -gt "$LIMIT_KB" ]; then
  echo "::warning::$APP ocupa $((SIZE / 1024)) KB (> $LIMIT_KB KB). Puede no entrar en la partición del Launcher; cambiá el esquema de particiones en el Launcher."
fi
