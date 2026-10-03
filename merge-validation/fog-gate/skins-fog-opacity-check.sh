#!/usr/bin/env bash
set -euo pipefail
mkdir -p artifacts/skins/merge-validation/fog-gate
GLOB2_USER_DATA_DIR="$PWD/artifacts/skins/merge-validation/fog-gate" LD_LIBRARY_PATH="$PWD/artifacts/skins/sdk/lib" LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/src/skin-preview "$PWD/artifacts/skins/units" opacity --validate-opacity
python3 docs/.work/verify-skins-opacity.py
