# Landscape picker input evidence

Source: `1028826bc` (PR #583). Baseline: `fe33142db145d3025bc28ea6c1ee52a3d96511eb`.
Linux x86_64, GCC 15.2.0, optimized SCons client build, SDL 3.4.16 Linux SDK.

## Results

- Engine test binary built successfully (`build.log`).
- All four MapPreview cases passed, including native widget events and custom lobby captures.
- Picker finger swipe moved grid offset 2680 to 2630, with unchanged map pan/zoom and selection. Image-area selection and mouse-wheel scrolling checks passed, followed by remaining picker/setup assertions.
- The encompassing CustomGameSetup case fails its final translation check for `[[Start quality]]`. Unchanged master produces the same failure (baseline log and JUnit attached). This is an existing failure, not a green overall test run.
- PNGs show the list before/after the synthetic SDL finger swipe on a loaded image at 640x480. They are Linux captures, not physical-device captures.
- Physical Android/iOS, macOS, and Windows not validated locally. Hosted checks are tracked by the PR.

## Commands

```sh
GLOB2_SDL3_PREFIX=<Linux SDL SDK> CCACHE=1 scons -j16 release=1 server=0 engine-tests
xvfb-run -a -s '-screen 0 1024x768x24 -noreset' python3 test/run_tests.py --binary engine --filter 'MapPreview/*' --filter 'CustomGameSetup/custom game screens*' --artifacts artifacts/landscape-picker/tests --junit artifacts/landscape-picker/evidence/junit.xml --verbose
```

For the baseline comparison, restore the two changed C++ files from the baseline revision, rebuild through SCons, and run the same CustomGameSetup filter (without MapPreview). All other compiled sources already match baseline. Restore PR sources and rebuild afterward.
