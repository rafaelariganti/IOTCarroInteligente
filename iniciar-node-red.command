#!/bin/bash
# Dois cliques neste arquivo para iniciar o Node-RED do Carro Inteligente.
# Editor: http://localhost:1880/   Dashboard: http://localhost:1880/ui/
# Para parar: feche esta janela do Terminal (ou Ctrl+C).

PASTA="$HOME/.node-red-carro"
PROJETO="$(cd "$(dirname "$0")" && pwd)"

if [ ! -d "$PASTA/node_modules/node-red-dashboard" ]; then
  mkdir -p "$PASTA"
  (cd "$PASTA" && npm init -y >/dev/null && npm install node-red-dashboard)
fi

if [ ! -f "$PASTA/settings-carro.js" ]; then
  cat > "$PASTA/settings-carro.js" <<'EOF'
module.exports = {
  flowFile: 'flows.json',
  uiPort: 1880,
  telemetry: { enabled: false },
  editorTheme: { tours: false }
};
EOF
fi

cp "$PROJETO/flow.json" "$PASTA/flows.json"
(sleep 6 && [ -z "$SEM_NAVEGADOR" ] && open http://localhost:1880/ui/) &
node-red -u "$PASTA" -s "$PASTA/settings-carro.js"
