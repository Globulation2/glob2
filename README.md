# PR 828 verification

Tested head: 26d12e57b0294e2dbd51bd75c4b0f4b76c3f46f0
Base and fetched master: 01848dea7790884bd341c66754253e2b9845e85a (unchanged; head includes this base).
Source tree SHA256: 115f35181086c7ece10aa4cad614bda158e0265520ad5ee505a3b12defb9899f.
The compiled provenance says dirty solely for unrelated untracked build-software-terrain/; no tracked modifications.

Ubuntu 26.04.1 x86_64, Threadripper 2950X, Clang 18.1.8, Python 3.14.4; Emscripten 4.0.15 / Node 22.22.1 for the isolated coverage kernel probe. Repository SDL runtime: SDL 3.4.16, image 3.4.6, net 3.2.0, ttf 3.2.2, WebP 1.6.0, repository X11/PNG16/kerning patches. The dependency manifest and rebuild log are archived. Build headers in the old prefix and fresh runtime prefix are byte identical for all 87 headers. Tests use the ABSOLUTE fresh runtime LD_LIBRARY_PATH to avoid the runner child working-directory change selecting stale libraries.

## Commands and results

Run in the repair checkout; ROOT denotes its absolute path. Asset encoder uses the repository pinned Python runtime at /home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python. The build generated matching runtime assets and the pinned recording dependency.

```sh
GLOB2_ASSET_ENCODER_PYTHON=/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python GLOB2_SDL3_PREFIX=build/sdl3-ci/prefix scons --build=build/native-coverage-torus release=0 server=0 engine-tests unit-tests CC=clang-18 CXX=clang++-18 CFLAGS='-g -O0 -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE' CXXFLAGS='-g -O0 -fprofile-instr-generate -fcoverage-mapping -DGLOB2_TEST_COVERAGE' LINKFLAGS='-g -fprofile-instr-generate' -j16
taskset -c 20-29 python3 scons/sdl3_dependencies.py --prefix build/sdl3-ci-current/prefix --work build/sdl3-ci-current/sources --jobs 10
```

Final source refresh: build-head26-final.log (exit 0). Compiled provenance is TestBuildProvenance.h.

All final test commands use LD_LIBRARY_PATH=$ROOT/build/sdl3-ci-current/prefix/lib and LLVM_PROFILE_FILE=$ROOT/artifacts/torus-coverage-repair/<run>-%p.profraw. Original normal/triple commands, exact environment and CPU affinities are recorded in head26-commands.json; both runner exits are 0 in head26-results.json. Deadlines remain 900 seconds, normal 221.8 seconds runner / 221.38 seconds case, triple 232.1 seconds runner / 231.762 seconds case. Fixtures/assertions/cache budgets unchanged.

```sh
taskset -c 12-15 python3 test/run_tests.py --build-dir build/native-coverage-torus --binary unit --timeout 900 -j2 --display-jobs 1 --write-inventory artifacts/torus-coverage-repair/head26-unit-inventory.json --junit artifacts/torus-coverage-repair/head26-unit-junit.xml --artifacts artifacts/torus-coverage-repair/head26-unit
taskset -c 8-11 python3 test/run_tests.py --build-dir build/native-coverage-torus --binary engine --filter '*Terrain*/*' --filter '*Surface*/*' --filter '*Batch*/*' --filter '*Portable*/*' --filter 'SoftwareRenderer/*' --filter 'TorusRender/native tiled*' --filter 'TorusRender/*software rendering*' --filter 'TorusRender/HD tiled*' --timeout 900 -j4 --display-jobs 1 --write-inventory artifacts/torus-coverage-repair/head26-focused-inventory.json --junit artifacts/torus-coverage-repair/head26-focused-junit.xml --artifacts artifacts/torus-coverage-repair/head26-focused
```

836 unit + 76 focused engine + 2 original Torus = 914 cases, no failures/errors/skips; each runner exit 0. Two new regressions also passed separately in 9.8 seconds runner (9.61469 case sum). Regression checks prove density stays native and warm chunks are retained for narrow atlas edges; HD tiled interiors/gutters match whole-map pixels within the existing RGB tolerance of 1 and allocation fallback remains covered. Native/portable/software render paths, terrain goldens, mixed terrain simulation trace, source alpha, clipping/atlas allocation and unit batch tests passed. Pixel/fixture files are in head26-focused/ and head26-regressions/.

Isolated coverage-sample.cpp, actual unchanged TerrainMaterials implementation: native and WASM digest 4432830292701315765 for 256 material configurations at native/2x/4x. Probe was built at the earlier kernel revision 116736e; TerrainMaterials sources are identical at final head. It establishes kernel parity, not whole-browser rendering parity. Ten interleaved O0 kernel pairs in kernel-final-repeat.json: baseline median 3.69384 seconds versus optimized median 2.131515; these are local kernel measurements while other jobs were active, not whole-CI savings.

## Failure evidence and limits

Original hosted master failure: https://github.com/Globulation2/glob2/actions/runs/37411833035/job/112108963526 ; retained hosted artifact: https://github.com/Globulation2/glob2/actions/runs/37411833035/artifacts/11392817685 . Baseline original complete fixtures reproduced 900-second timeouts locally. The initial kernel-only repair also timed out, including with fresh SDL: final-current-normal/triple retain that failure. CPU affinity of those earlier runs changed after about 95 seconds (recorded), so these are not strict speed-comparison measurements. The final coherent-density revision passes both original cases.

Earlier ImageAssets PNG16 unit failures came from the stale local runtime missing the current PNG patch. The unchanged older cached unit binary fails against stale libraries and passes against the rebuilt patched runtime. Old-unit logs and manifest/header evidence are retained. Relative LD_LIBRARY_PATH experiments also selected stale runtime after child cwd changes; only absolute-prefix final head26 runs are claimed as passing fresh-dependency validation.

No full engine matrix, sanitizer, Windows, Android or full browser verification was run locally. Hosted full affected verification including these platforms is requested and in progress: https://github.com/Globulation2/glob2/actions/runs/37426567122 . Presentation-only changes do not compute simulation state or alter save/replay/network boundaries; SIM_REVISION is unchanged. This repair changes HD atlas edge antialiasing when whole-capture budget selects native density, aligning edges with wider tiles. Full master CI will confirm recovery asynchronously. The prior Firefox startup flake remains under observation, not claimed fixed.

Logs, JUnit, inventories, commands and generated fixtures are included; raw coverage profiles and large binaries remain local, and canonical failed hosted profiles stay in the linked artifact. Successful test stdout is not retained by the runner; passing JUnit and corresponding source assertions establish the pixel tolerance rather than a claimed zero difference.

Maintainer acceptance: Codex, acting under the repository author's explicit authorization, accepts this focused evidence as sufficient for head 26d12e57 against base 01848dea under AGENTS.md. Remaining hosted platform checks will be monitored without bypassing repository protections.
