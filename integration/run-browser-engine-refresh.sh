#!/usr/bin/env bash
set -euo pipefail
export GLOB2_TEST_URL=http://127.0.0.1:8776
export GLOB2_TEST_BUILD_DIR=/home/bradley/.codex/worktrees/a8c9/glob2/build/browser-fix-merge-web
cd browser
./node_modules/.bin/playwright test image-assets.spec.js determinism.spec.js --grep 'WebAssembly decodes|per-tick simulation|verifies the committed' --project chromium --project firefox --project webkit --reporter list --output ../artifacts/browser-fixes/merge-validation/engine-refresh-results > ../artifacts/browser-fixes/merge-validation/engine-refresh.log 2>&1
./node_modules/.bin/playwright test threading.spec.js --grep 'runtime executes shared' --project chromium --reporter list --output ../artifacts/browser-fixes/merge-validation/threading-refresh-results > ../artifacts/browser-fixes/merge-validation/threading-refresh.log 2>&1
