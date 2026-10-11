#!/bin/sh
# Lee un mensaje de commit por stdin y falla si contiene atribución a una AI/agente.
# Uso: check-ai-attribution.sh [etiqueta] < mensaje

label="${1:-commit}"

AI_NAMES='claude|anthropic|openai|chatgpt|gpt-?[0-9]|copilot|gemini|bard|codex|cursor|codeium|windsurf|devin|aider|tabnine|sourcegraph|cody|amazon ?q|codewhisperer|jules|lovable|replit|v0|bolt\.new|llm|ai assistant|ai agent|\[bot\]|bot@|noreply@anthropic\.com'

matches=$(grep -inE \
  -e "^[[:space:]]*co-authored-by:.*($AI_NAMES)" \
  -e "generated (with|by) .*($AI_NAMES)" \
  -e "🤖")

if [ -n "$matches" ]; then
  echo "✖ Atribución a AI/agente detectada en $label:" >&2
  echo "$matches" | sed 's/^/    /' >&2
  echo "  Quitá esas líneas del mensaje (git commit --amend) antes de continuar." >&2
  exit 1
fi
exit 0
