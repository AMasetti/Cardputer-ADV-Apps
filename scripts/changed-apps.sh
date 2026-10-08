#!/usr/bin/env bash
# Imprime un array JSON con las apps a compilar entre <base> y HEAD.
#   scripts/changed-apps.sh <base-sha|all>
# Si cambian scripts/ o el workflow, recompila todas.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="${1:-all}"

all_apps() {
  for d in "$ROOT"/apps/*/; do
    name=$(basename "$d")
    [[ "$name" == _* ]] && continue
    [ -f "$d/app.json" ] && echo "$name"
  done
}

if [ "$BASE" = "all" ] || ! git -C "$ROOT" cat-file -e "$BASE^{commit}" 2>/dev/null; then
  all_apps | jq -R . | jq -sc .
  exit 0
fi

CHANGED=$(git -C "$ROOT" diff --name-only "$BASE" HEAD)

if echo "$CHANGED" | grep -qE '^(scripts/|\.github/workflows/build\.yml)'; then
  all_apps | jq -R . | jq -sc .
  exit 0
fi

echo "$CHANGED" \
  | { grep -E '^apps/[^/_][^/]*/' || true; } \
  | cut -d/ -f2 | sort -u \
  | while read -r app; do [ -f "$ROOT/apps/$app/app.json" ] && echo "$app"; done \
  | jq -R . | jq -sc .
