#!/bin/bash
set -euo pipefail
GLOB2_WRITE_ABLATION_OUTPUT="$PWD/artifacts/resource-growth/attribution-v2/writes-outlined.json" GLOB2_CAPTURE_ABLATION_OUTPUT="$PWD/artifacts/resource-growth/attribution-v2/capture.json" artifacts/resource-growth/attribution-v2/controls-tests --test-case='outlined proposal write ablation*,incremental growth snapshot capture*' --no-skip > artifacts/resource-growth/attribution-v2/controls.log 2>&1
python3 docs/.work/run-growth-attribution-v2.py batched
python3 docs/.work/run-growth-attribution-v2.py stages
