#!/bin/bash
set -euo pipefail
GLOB2_WRITE_ABLATION_OUTPUT="$PWD/artifacts/resource-growth/attribution-v2/writes-matched.json" GLOB2_RETENTION_OUTPUT="$PWD/artifacts/resource-growth/attribution-v2/retention.json" artifacts/resource-growth/attribution-v2/matched-controls-tests --test-case='outlined proposal write ablation*,snapshot lease retention capture*' --no-skip > artifacts/resource-growth/attribution-v2/matched-controls.log 2>&1
python3 docs/.work/run-growth-attribution-v2.py batched
python3 docs/.work/run-growth-attribution-v2.py stages
python3 docs/.work/run-growth-attribution-v2.py fair
