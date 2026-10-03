# PR 261 local verification evidence

Feature revision `7369a24dd81ea70f18a6e02c7b6aaaa5dd32b710`, rebased onto `d46638d859e228a63e0471e2dd45108b112e9454`. This evidence branch is separate from the implementation PR.

Repeats default to 1, follow colony/base controls, and remain under collapsed advanced controls. `desktop-default.png` captures the default; `phone-repeat.png` captures an explicit 2×2 touch selection. Native save/reload, map-copying, farm ownership, scripted/capacity refusals, CLI and match-record contracts are covered by `native.xml`.

Commands from the repository root:

```sh
python3 test/run_tests.py --no-display -j8 --timeout 2400 --junit artifacts/pr261/native-diagnostics.xml --artifacts artifacts/pr261/native-diagnostics
GLOB2_UI_ONLY=map-repeat SDL_VIDEO_DRIVER=x11 LP_NUM_THREADS=2 python3 test/run_tests.py --tag display -j3 --display-jobs 3 --timeout 600 --junit artifacts/pr261/display-diagnostics.xml --artifacts artifacts/pr261/display-diagnostics
python3 -m unittest discover -s tests/build_system
python3 test/check_sim_revision.py --base origin/master
python3 data/check_translations.py --strict --json
python3 test/test_check_sim_revision.py
python3 test/test_translations.py
```

Display runs unset DISPLAY/WAYLAND_DISPLAY and use the runner's private Xvfb plus Openbox. Private Openbox binaries/data came from `/tmp/pr261-wm`; SDL3 was built under `/tmp/glob2-sdl3/prefix`. Compiler/flags/source hash are retained in `build-provenance.json` and `summary.json`.

The concurrent display report preserves three stalls: the portable icon case timed out at 600 seconds, and two portable renderer cases were stopped after their unchanged isolated runs passed (1.0 and 2.9 seconds). The separate rerun reports cover all three; assertions were unchanged. Other nonpassing cases, if present, must be resolved before merge. This is not a claim that the initial concurrent run was wholly green.

Final UI sweeps select the map repetition fixture in all six viewports. Earlier complete all-screen sweeps passed at `b7f89e595` and are retained separately; they are not represented as final-head executions. Native Linux software/OpenGL/portable paths were exercised locally. Physical Android/iOS devices, Windows, browser execution and cross-platform per-tick checksum comparisons were not performed locally. Hosted checks were queued; the maintainer explicitly authorized merge based on passing relevant local tests.
