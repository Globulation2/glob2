#!/bin/bash
# Start a new, auditable synchronous training run. Never starts implicitly.
set -euo pipefail
ROOT=${NEUROTICA_ROOT:-"$HOME/neurotica"}
exec "$ROOT/.venv/bin/python" "$(dirname "$0")/neurotica_selfplay.py" \
  --root "$ROOT/glob2" --binary "$ROOT/glob2/build/src/glob2" \
  --generators "$ROOT/generators.txt" --init "${1:?BC checkpoint required}" \
  --out "${2:?fresh output directory required}" --device cuda --generations 1000 "${@:3}"
