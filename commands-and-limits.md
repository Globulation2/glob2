Run from patch source babfe552a6b03d7da5f5dae1341f2e005cca6647, base b9653484b55ca398382707d712c34e319c3c4fa5. Native paired GCC13 release and both Wasm variants come from run40556771805 at697ad9d29ed4c2c7d5c965c71dd4ad54395e8306. Native runtime assets and pinned shared libraries are extracted from that same web-native artifact; native command vectors, executable hashes and per-run results are attached. Current-native validation uses the up-to-date source-identical GCC15.2 development CLI with the existing pinned SDL3 prefix. No current-source Wasm or Windows/macOS native rebuild is claimed.

Commands:
- gh run download 40556771805 -n web-native -D artifacts/ci-watch/2205/hosted-native
- gh run download 40556771805 -n web-client -D artifacts/ci-watch/2008/web-client
- GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix python3 tools/dev_build.py build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2
- python3 artifacts/ci-watch/2205/native-validation.py
- python3 browser/serve.py 8779 --bind 127.0.0.1 --directory artifacts/ci-watch/2008/web-client
- GLOB2_TEST_BUILD_DIR="$PWD/artifacts/ci-watch/2008/web-client" GLOB2_TEST_URL=http://127.0.0.1:8779 browser/node_modules/.bin/playwright test studio.spec.js --config=browser/playwright.config.js --project=chromium --project=firefox --project=webkit --grep 'Studio source produces deterministic trace' --output=artifacts/ci-watch/2205/browser-after --reporter=list
- node --test browser/unit/*.test.js
- python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
- python3 tools/check_docs.py
- git diff --check

Why the native initial-save raw-byte attempt failed: the source map is format133 and serialization writes186. MapHeader::checkSum includes versionMinor; Game::checkSum rotates that contribution another 6+teams+players times. For this fixed ordinary map (2teams/2players, no experiments), the only aggregate delta is ror32(133 XOR186,11)=132120576, verified at every tick with all detailed bytes equal. The validation script explicitly asserts that delta; it does not silently ignore aggregate discrepancies. It then uses a current-format initial-load baseline and verifies exact complete tick records, including aggregates, after a512-tick save and resumes with1/4workers. Same-format full traces match byte-for-byte across worker counts. Native generation and all six Wasm fresh-map traces match completely. No original test assertion is relaxed.

The old fixture was last changed at3d896cbb95293da6d24caddd247a62c9ca99d77f, save173/SIM76. The intervening accepted SIM changes are recorded in reviewed-simulation-changes.txt. Source/map input hashes remain pinned and unchanged. A newer SIM revision has already versioned this behavior; this reference-only repair changes no simulation or compatibility gates. The unchanged full hosted master still must confirm recovery.
