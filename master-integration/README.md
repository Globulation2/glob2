# Hive Mind master integration validation

PR #605 rebases only the Hive restoration onto master d8d2cd64140832b07640e44ead02930d691d9598, after #516 and #518 landed. Integrated revision: 70a644e4dd5f1e6ee40cdeb5143b4fbf8c709756.

The integration preserves current platform security and browser packaging. New database migrations are numbered 0018 and 0019; a regression upgrades the foundation schema while retaining existing accounts. Event cursor IDs are serialized as strings at the wire boundary. Browser adapters now live under browser/, consistent with the platform boundary checks. Concurrent PR updates were retained.

## Reproducible checks

- `cd platform && npm run check`: lint, format, types; 383 tests passed and 5 skipped. This ran before subsequent browser-only adapter changes.
- `python3 -m unittest discover -s tests/build_system`, using the pinned asset encoder Python: 255 tests, 3 skipped, no failures. The default system Python has different Pillow/WebP versions and causes two asset-packaging fixture failures; using the prescribed pinned encoder resolves them.
- Linux native release game and test build: `GLOB2_SDL3_PREFIX=<SDL3 SDK> scons release=1 server=0 -j6 tests build/linux/client/release/src/glob2`.
- `python3 test/run_tests.py --filter 'HiveMind*/*'`: 10 cases passed. Display tests use the local desktop wrapper to select X11 and desktop layout, since this host's default Wayland backend exposes no usable display.
- `python3 test/run_tests.py --filter 'JavaScript*/*'`: 55 cases passed, including save/load compatibility.
- Settings software/OpenGL layout and persistence at 1000x700: 2 cases passed in desktop mode.
- Browser release build: `scons target=web release=1 -j6`; Chromium selected determinism, worker and staged-assets suites, excluding the separately hosted shared scripting corpus.
- Browser unit tests: 34 passed.
- Strict translation validation: zero structural errors. CI policy contract tests: 80 passed.

The attached four 1500-tick browser traces (serial and 1/2/4-thread variants) exactly match the native trace. The committed match record also has an identical 703-line trace. No simulation or save-format change was introduced by the adapter fixes.

Native/browser builds and focused suites were rerun after the browser-adapter changes. Earlier screenshots and live-model evaluations under ../merge-readiness and ../revision describe the preserved feature and are not new live integration playtests.

## Remaining gates

Hosted current ready-PR Relevant checks passed is mandatory and was pending when this evidence was published. Local Linux/Chromium evidence does not establish Windows/macOS or non-Chromium completion. No production feature or sales flags were enabled. Real multiplayer playtesting, platform worker containment, payment end-to-end validation, dedicated production credentials and configured prices remain production release gates.
