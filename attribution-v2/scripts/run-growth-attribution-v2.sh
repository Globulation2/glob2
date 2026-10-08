#!/bin/bash
set -euo pipefail
python3 docs/.work/run-growth-attribution-v2.py writes
python3 docs/.work/run-growth-attribution-v2.py statistics
python3 docs/.work/run-growth-attribution-v2.py stages
