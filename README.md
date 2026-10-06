# Building-gradient forward-port verification

Source: `772d0c0566785d6476b94270cf68bce244d0b281`, base: `e53a673b2609175e781ad2608a15a220866cf2fa` (PR851, PR855 and browser fixture PR856). Source tree: `bb533de0b14bb5e721fa7ac13e0d1cfe9a1200b9`. PR789 remains draft and all experiments remain off by default.

The final history-only rebase for PR856 left the entire Git tree identical to validated revision `82abffd9d`. Native test binaries record the revision present during their build; manifests identify exact executable hashes. The experiment-off control was built at f0ff8384b; PR856 changes only browser fixture files, not its executable source.

## Results and scope

- Native macOS arm64 / Apple clang 21 and Ubuntu 24.04 x86_64 / GCC 13.3: 254 cases each, 253 passed, zero failures, one display-only Markets panel case skipped. Focused registry includes gradients, resource scheduling, executor failures/slow work, invalidation, custom terrain, market fetch/selection, headers, replay acceptance, match setup/network verification and Maxima economy/food ledger.
- Linux: 3 checkpoint workloads × 4 policies (eager, staffing, partial, combined) × 5 worker counts (0/1/2/4/8), 256 continuation ticks each. All 60 complete checksum traces match within their saved configuration. MacOS worker4 traces match Linux for all 12 workload/policy combinations.
- Audit checksums: 3 workloads × 4 policies × on/off, 32 ticks each. All 12 paired complete traces match. These validate observational auditing, not long-run gameplay safety.
- Experiment off: 3 legacy workloads, fresh 768-tick runs and older-format v139 save continuations from tick512 to768, against current master control. All 6 paired scenarios match team/entity per-tick records. Aggregate checksum includes format version and is deliberately excluded only in this older/newer-format comparison.
- Save/resume: 28 phase/worker cases on each platform: every phase of delays 2/4/8, resumed workers0/4, eager policy. Complete per-tick continuations match. Native tests separately cover partial continuation, invalidation after capture, simultaneous deadlines, destination reuse/deletion, new children, oversized fallback, and modern terrain/supplier seeds.
- Golden multiplayer match regenerated and verified for SIM_REVISION23. Protocol package 21 tests, platform typechecks, 27 Python analysis tests and 5 translation tests pass.
- Native Studio reference: macOS/Linux traces both hash `3bfb63dd4724281d4c162fd144d137b6da64ed05d35e821511cefb71f28c22fe`, 1,849,612 bytes. Browser replay fixture is format140. Browser runtime/Playwright, Windows, Android and display execution were not verified in this port.

These are compatibility tests, not new timing measurements. Earlier timings and the 32-seed cohort apply only to historical source `47f88f01140b0ed2d74fd599f1f0f8896fc80e69`, preserved on `codex/building-gradient-pre-851-evidence`. No performance promotion is proposed. No new 10,000-tick population audit or discrepancy-specific causal fork campaign was performed here.

## Reproduction and retained evidence

`manifests/` contains exact commands, input/output/binary hashes, worker/policy configuration and save-phase comparisons. `native/` contains test logs and JUnit, golden verification, protocol/typecheck/Python results. `checksums/` retains all Linux worker traces, the paired Mac traces, audit on/off, off-reference, save-phase and native Studio traces. `checkpoints/` contains the three exact tick512 starting states and pending-publication saves for every delay phase. Original legacy workload inputs are tracked in the source repository as compressed files.

From the source root, use the registry selectors in `drivers/` through SCons. MacOS used `CCACHE=1 GLOB2_SDL3_PREFIX=/Users/bradley/glob2/artifacts/recording-sdl3/prefix /opt/homebrew/opt/scons/libexec/bin/python artifacts/building-gradient-rebase-851/focused-mac-build.py`. Linux used `GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/sdl3/prefix GLOB2_RECORDING_PREFIX=/home/bradley/glob2-building-gradient-20261004-9520/recording-prefix LD_LIBRARY_PATH=/home/bradley/glob2-verify/sdl3/prefix/lib python3 ../focused-linux-build.py`. Substitute local SDK/runtime paths; native flags and platforms are recorded in `manifests/provenance.json`.

Native command: `python3 test/run_tests.py --no-display -j4 --junit <output.xml> --artifacts <output>`; golden: `python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'`. Python: `python3 -m unittest discover -s test -p 'test_building_gradient*.py'` and `-p '*translation*.py'`. Protocol: `npm --prefix platform test -- packages/protocol/test`. Exact simulation and continuation commands appear in manifests. Drivers expect source-root execution and ignored artifact output directories; adapt those directory paths or copy them under the corresponding ignored source directories.

## Integration with concurrent engine snapshots / AI streams

Capture remains isolated in `BuildingGradientCapture.h`; `GradientRuntime` accepts an injected asynchronous executor. No competing engine snapshot store or AI scheduling rules were introduced. The eventual combined merge should project catalog/terrain/resource/occupancy/area/resource-field components directly, avoid repacking an extra full Cell array, and replace the executor backend with the engine pool. Building publication delay D=2/4/8 stays independent of AI delay X; PR851 resource reservation/preparation/publication stays intact. AI snapshots should read route/swim access metadata through routeAccess. Combined save/replay/network version gates must be reconciled and both pending stream continuations revalidated; matching version numbers alone do not establish compatibility.
