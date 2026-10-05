# Threaded session fixture verification

Tested commit: 759dd2a782f1cbf09a3df0f6dd8197e94f4dc13c. Base: a88ff621c3959ce74bfe6761bf562d668c5d0b38 (fetched before final verification).
Ubuntu 26.04.1 x86_64, GCC 15.2, Python 3.14.4, SDL 3.4.16/image 3.4.6/ttf 3.2.2. Release software renderer, opengl=0. No production change or simulation version change.

Build:
```
GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix GLOB2_RECORDING_PREFIX=build/linux/client/release/recording/prefix GLOB2_ASSET_ENCODER_PYTHON=<managed Pillow 12.2 encoder venv>/bin/python scons -j12 release=1 server=0 opengl=0 --build=build-software-terrain engine-tests
```
final-build.log: exit 0, rebuilt committed final revision. Generated provenance records that revision. The ignored nonstandard build directory is listed as untracked; it is generated output.

Controlled scheduler reproduction: compile lower-worker-priority.c with `cc -shared -fPIC -o lower-worker-priority.so lower-worker-priority.c -ldl -lpthread`; run `python3 probe.py` on the unmodified binary. The attached script uses taskset on the first available CPU and LD_PRELOAD to set new pthreads to nice 19 while the main thread remains normal priority. The original fixture fails its unchanged 4,000-frame assertion. `python3 probe-after.py` on the rebuilt repair passes. Five further repeats all pass (pressure-N JUnit and logs). The synthetic 60-second suspension and all existing assertions remain in place. No timeout/frame allowance is increased.

Normal verification:
```
env -u DISPLAY -u WAYLAND_DISPLAY python3 test/run_tests.py --build-dir build-software-terrain --binary engine --filter 'EngineSession/*' --junit artifacts/threaded-session/final-all.xml --artifacts artifacts/threaded-session/final-all --verbose
```
Both nondisplay cases pass, including the affected incremental sessions case. Two display cases could not initialize this locally built SDL display backend (no display added); their initialization failures are retained, not claimed as passes. The initial attempt with inherited display state is also retained. We omit broader engine/browser/platform matrices because this change only gives the threaded test loop CPU scheduling time; all production code is unchanged. Hosted GCC 11 failure log is included; macOS and GCC 11 repair execution remain unverified locally. Subsequent master CI confirms coverage asynchronously.
