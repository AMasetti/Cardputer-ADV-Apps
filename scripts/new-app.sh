#!/usr/bin/env bash
# Crea apps/<nombre> a partir de apps/_template.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:?uso: new-app.sh <nombre-en-kebab-case>}"
[[ "$NAME" =~ ^[a-z0-9][a-z0-9-]*$ ]] || { echo "Usá kebab-case: a-z, 0-9 y guiones" >&2; exit 1; }
DST="$ROOT/apps/$NAME"
[ -e "$DST" ] && { echo "Ya existe $DST" >&2; exit 1; }
cp -r "$ROOT/apps/_template" "$DST"
grep -rl "__APP__" "$DST" | xargs sed -i "s/__APP__/$NAME/g"
echo "Creada apps/$NAME. Compilá con: scripts/build-app.sh $NAME"
