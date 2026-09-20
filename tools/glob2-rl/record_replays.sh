#!/bin/bash
# Exact same policy path as paired evaluation; retain maps, flags and results.
set -euo pipefail
ROOT=${NEUROTICA_ROOT:-"$HOME/neurotica"}
exec "$ROOT/.venv/bin/python" "$(dirname "$0")/paired_eval.py" run \
  --root "$ROOT/glob2" --binary "$ROOT/glob2/build/src/glob2" \
  --checkpoint "${1:?checkpoint required}" --manifest "${2:?manifest required}" \
  --out "${3:?fresh output directory required}" --arm replay --device cuda --replay "${@:4}"
