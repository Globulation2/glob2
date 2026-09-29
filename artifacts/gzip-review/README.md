# PR 315 gzip verification

Gzip/import validation source: `6fb2aa3f71d4690a839036e6a85f1d31d9ec9ad7`. The full native suite and 10 Chromium integration checks ran at `4e048761ce26aef9dce756846b1aefb8dd32ee31`; the final import-only followup was rebuilt and passed the save-safety harness, with focused browser import checks recorded separately. Host: macOS arm64. All commands run from the repository root unless stated otherwise.

## Native checks

| Check | Result / retained evidence |
| --- | --- |
| Fixture migration | `migration.json`: system `gzip -dc` matches the original bytes of all 36 retained migrated maps/saves |
| Save safety | `safety.log`: gzip round trip, corrupt/truncated/trailing data rejection, bounded inflation, failed writes retain old file, production autosave decompresses to direct serialization, unchanged restored checksum components, gzip import |
| Autosave regression before fix | `safety-before.log`: expected gzip autosave missing before background compression was enabled |
| Map CLI | `map-cli.log`: 50 commands passed; generated outputs in archive |
| Reports | `map-report.log`: schema, repeatability and legacy save checks |
| Custom game | `custom-setup.log`, `lobby-ui.log`, `preferences.log`; `lobby-map.png` shows FourSquares1 selection and preview |
| LAN | `lan.log`: two real loopback join/leave cycles, each download matches the 48,770-byte source gzip exactly |
| Legacy saves | `legacy-v84.log`, `legacy-v88.log`, `wrapped-load.log`, `entering-load.log`: raw inflated fixtures load with expected state |
| Continuation | `echo-continuation.log`: both AI combinations at two save boundaries; `maxima-continuation.log`: 512 fixture hashes and 256 post-reload records |
| Headless CLI | `tournament-cli.log`: 17 cases; full inputs, saves, replays, manifests and checksum traces in `native-evidence.tar.gz` |
| Browser unit checks | `browser-unit.log`: 16 passed |

## Commands

```sh
CCACHE=1 scons -j6 release=1 savegame-safety-test custom-setup-test lan-test
CCACHE=1 scons -j6 release=1 build/darwin/client/release/src/glob2 team-stats-save-test building-footprint-test entering-unit-save-test map-report-test
python3 test/run-savegame-safety-tests.py build/darwin/client/release/src/SavegameSafetyHarness
python3 test/test_map_cli.py build/darwin/client/release/src/glob2
python3 test/test_map_report.py build/darwin/client/release/src/glob2 build/darwin/client/release/src/MapReportHarness
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/darwin/client/release/src/CustomGameSetupHarness
GLOB2_USER_DIR="$PWD/artifacts/gzip-review/lobby-profile" SDL_AUDIODRIVER=dummy build/darwin/client/release/src/CustomGameSetupHarness artifacts/gzip-review/lobby-ui
python3 test/run_lan_session_test.py build/darwin/client/release/src/LANSessionHarness --output artifacts/gzip-review/lan
python3 test/check_echo_save_continuation.py build/darwin/client/release/src/glob2
python3 test/maxima/check_save_continuation_fixture.py build/darwin/client/release/src/glob2
python3 test/tournament_cli_integration.py --binary build/darwin/client/release/src/glob2 --output artifacts/gzip-review/tournament-fixed
node --test browser/unit/*.test.js
```

Legacy fixtures are inflated using `test/inflate_gzip_fixture.py`, then passed to the existing harnesses (`--legacy` for TeamStatsSaveHarness, `--load` for the footprint/entering harnesses). The checked-in expected v84/v88 output is compared using `run-savegame-safety-tests.py --expect-stdout`.

## Coverage limits

Native results here are macOS arm64 only. Cross-platform CI is tracked on the PR. No gameplay/schema/protocol version changes are intended. These tests do not replace maintainer play review or independent human approval for the container/network architecture change.

## Browser integration

The WebAssembly client built locally. `browser-integration.log` records 10 Chromium tests passing: chooser corruption, editor storage failure/export/retry, save/map/replay import, import persistence failure, and save persistence/restore failures. Screenshots are in `browser-screenshots/`. The final import followup is in `browser-import-final.log`.

```sh
scons -j6 target=web release=1 emsdk=/path/to/browser-emsdk
(cd browser && GLOB2_CHROMIUM_ANGLE=swiftshader npx playwright test --project=chromium tests/import.spec.js tests/editor-storage.spec.js tests/storage.spec.js tests/chooser-errors.spec.js)
```

The final native import tests cover uppercase gzip suffixes and collisions in both directions between raw and gzip files. `import-before.log` demonstrates that the expanded import regression fails against the prior importer.

## Final Windows portability fix

While CI ran, master advanced to `4f05b6ac5` (PR #389). GitHub's merge build picked up a new `near` local in `Farmland.cpp`, which conflicts with the Windows headers' `near` macro. The branch now includes that master commit and renames only the local to `rimPlanted`, without changing the calculation. Final PR head: `c2f5d77442d7575c58c5c74f1317cf8db2518745`.

`windows-macro-before.log` reproduces the exact parse errors with `-Dnear=`. `windows-macro-after.log` records successful syntax checking after the rename. Final CI: https://github.com/Globulation2/glob2/actions/runs/36508734905 and https://github.com/Globulation2/glob2/actions/runs/36508734912.

## Session and trace fixture alignment

CI exposed a session-test fixture still creating `blocked.map` instead of blocking the actual `blocked.map.gz` output. `EngineSessionHarness` now exercises the gzip destination; `engine-session.log` records the complete harness pass, including preservation of live editor metadata after a failed replacement. The native and WebAssembly determinism runners both read `games/cross-replay.game.gz`.

`native-trace/` and `wasm-trace/` contain matching 1,500-tick traces. SHA-256 for each: `110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`. See `browser-determinism.log` and `native-trace.log`. Final test-path cleanup head: `df531cd8386997f659ef0b57528b5c08cc030783`.

`browser-save-flows.log` records 19 additional Chromium cases passing, covering single-player and responsive presentation, including saved games, exports, editor decisions and landscape selection.
