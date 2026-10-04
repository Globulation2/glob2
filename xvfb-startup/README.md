# Owned Xvfb / Openbox readiness repair

Clean tested revision: `02ee9f769bdbdb6b95078cd14187475150383f5e`, base `526f607f919ea57c9aa1735944848953c8941d69`. Ubuntu 26.04 x86_64, GCC 15.2, SDL 3.4.16, Openbox 3.6.1, Xvfb/X.Org 21.1.21 (installed package details can be checked in build logs).

## Hosted failures

- [Master run 37177326417](https://github.com/Globulation2/glob2/actions/runs/37177326417), GCC 13 shard 3, job 111366877178: AITelemetryUI full-game desktop/phone display case times out before reaching assertions. Diagnostic stack is SDL X11_ShowWindow -> SDL_CreateWindow -> GraphicContext -> HeadlessGlobals. X11 shows the application still IsUnMapped and Openbox's client list empty after 300 seconds.
- [Repaired-revision run 37178754963](https://github.com/Globulation2/glob2/actions/runs/37178754963), GCC 11 shard 4, job 111368417033: terrain atlas display test likewise stalls in SDL X11_ShowWindow and is killed after 300 seconds. All other 705 unit cases pass. The raw log retains its multi-thread diagnostic stack.

## Defect and change

The runner accepted _NET_SUPPORTING_WM_CHECK as readiness. Openbox publishes that identity in [screen_annex](https://github.com/danakj/openbox/blob/master/openbox/screen.c#L185-L193), before [client-event initialization and its startup callback](https://github.com/danakj/openbox/blob/master/openbox/openbox.c#L286-L340).

Launch Openbox with a startup callback that sets a private root property using the already-required xprop utility. Require both the valid EWMH identity and that callback's property before launching the test. Keep the five-second readiness deadline, process cleanup, SDL's real mapping wait, every test assertion, and existing test timeouts.

A new boundary regression feeds a valid identity while initialization is unfinished. The old runner launches immediately and fails the regression; the fixed runner waits until the callback property appears. This proves the faulty readiness boundary. Twelve real fresh sessions independently observed the identity before the startup callback (roughly 25–35 ms vs 67–102 ms with the Python observer callback). Those observer timings are evidence of ordering, not a benchmark or a claimed CI saving.

The hosted SDL stall itself did not reproduce locally in twelve executions of the original helper using the actual failing CI engine and SDL runtime. Twelve executions with the repaired helper also pass. The race diagnosis is supported by the early readiness defect and stalled-window evidence; it is not a locally reproduced proof of the exact lost-map-event sequence. Full hosted validation is still required, and another stall after this fix would require further investigation.

## Local verification

Commands, from repository root:

```
python3 -m unittest discover -s test -p test_run_tests.py -v
python3 -m unittest discover -s test -p test_ci_failure_aggregation.py -v
python3 test/test_recording_tool.py
"$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s test/build_system -v
GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix scons release=1 -j8 build/linux/client/release/test/glob2-engine-tests build/linux/client/release/test/glob2-unit-tests
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib python3 test/run_tests.py --build-dir build/linux/client/release --binary unit --artifacts artifacts/ci-repair/x11-unit-final --junit artifacts/ci-repair/x11-unit-final.xml
LD_LIBRARY_PATH=/home/bradley/glob2-verify/integrator/sdl3/prefix/lib python3 test/run_tests.py --build-dir build/linux/client/release --binary engine --filter 'AITelemetryUI/*' --filter 'WindowResize/*' --filter 'GameGUITouch/*' --filter 'GameplayRecording/*' --filter 'FrontendUI/*' --filter 'TorusRender/*' --fullscreen --artifacts artifacts/ci-repair/x11-engine-final --junit artifacts/ci-repair/x11-engine-final.xml
```

- Runner contracts: 35 pass, including early identity and cleanup/exit-status boundaries.
- Full unit suite: 14 subprocess groups / 706 cases pass, no skips. Includes the terrain atlas display case that stalled in CI.
- Focused engine suite: 17 cases pass, no skips. Includes all four WindowResize software/OpenGL/fullscreen checks, telemetry desktop/phone captures, touch interaction and toroidal rendering.
- Shared build contracts: 297 pass, three existing dependency/tool skips (NSIS, encoder-Python fontTools, expected SDL_ttf fixture archive). Failure aggregation and recording extraction contracts also pass; raw combined log records counts.
- Actual failing hosted c4 engine/SDL runtime: 12 before and 12 after executions of the telemetry full-game display case pass locally. Script and every result are retained; no physical-stall reproduction is claimed.

This changes Linux test-session startup only. It changes no game code, simulation computation, save format, replay/network version or intended game feel. Full current-master development-matrix verification remains outstanding.
