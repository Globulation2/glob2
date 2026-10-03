#!/usr/bin/env bash
set -euo pipefail
cd /home/bradley/.codex/worktrees/colony-skins/glob2
export GLOB2_TEST_URL=http://127.0.0.1:4283
export GLOB2_TEST_RENDERER=webgl2
export GLOB2_CHROMIUM_ANGLE=swiftshader
validation=artifacts/skins/merge-validation/tiling-gate
mkdir -p "$validation"
for mode in serial threaded; do
 GLOB2_SKIN_TEST_THREADS="$mode" PLAYWRIGHT_HTML_OUTPUT_DIR="$PWD/$validation/local-gate-chromium-$mode-report" npx --prefix browser playwright test -c browser/playwright.config.js colony-skins.spec.js skin-assets.spec.js --project=chromium --output="$validation/local-gate-chromium-$mode" > "$validation/local-gate-chromium-$mode.log" 2>&1
done
GLOB2_FIREFOX_HEADED=1 PLAYWRIGHT_HTML_OUTPUT_DIR="$PWD/$validation/local-gate-firefox-report" xvfb-run -a npx --prefix browser playwright test -c browser/playwright.config.js colony-skins.spec.js skin-assets.spec.js --project=firefox --output="$validation/local-gate-firefox" > "$validation/local-gate-firefox.log" 2>&1
PLAYWRIGHT_HTML_OUTPUT_DIR="$PWD/$validation/local-gate-webkit-report" npx --prefix browser playwright test -c artifacts/skins/playwright-webkit.config.cjs colony-skins.spec.js skin-assets.spec.js --project=webkit --output="$validation/local-gate-webkit" > "$validation/local-gate-webkit.log" 2>&1
PLAYWRIGHT_HTML_OUTPUT_DIR="$PWD/$validation/pacing-report" npx --prefix browser playwright test -c browser/playwright.config.js pacing.spec.js --project=chromium --output="$validation/pacing" > "$validation/pacing.log" 2>&1
printf 'Browser local gate passed\n' 
