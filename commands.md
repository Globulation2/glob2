# Reproduction commands

Run from the repository root at source commit `39ecd4ad903a2eae7d6962f39596136c9b57ec25`.
The evidence files preserve their original `artifacts/terrain/organic/` paths inside `evidence.tar.gz`.

```bash
export GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix
export LD_LIBRARY_PATH="$GLOB2_SDL3_PREFIX/lib"
scons -j16 release=1 server=0 engine-tests unit-tests
# Combined build exits 2: upstream Map::tiles access failures in unit fixtures.
scons -j16 release=1 server=0 engine-tests
# Engine-only build exits 0.
python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'TerrainValidation/*' --filter 'HighResolutionIntegration/*' --filter 'TorusRender/*' --filter 'MapRenderResize/*' --artifacts artifacts/terrain/organic/final/engine --junit artifacts/terrain/organic/final/engine/junit.xml --timeout 300 -j4 --display-jobs 1
encoder_python=$(python3 tools/package_assets.py --encoder-python)
"$encoder_python" -m unittest discover -s test/build_system -p test_package_assets.py
"$encoder_python" -m unittest discover -s tools -p test_terrain_tileset.py
"$encoder_python" -m unittest discover -s test/build_system -p test_artwork_package.py
python3 tools/terrain_tileset.py --check
python3 artifacts/terrain/organic/capture-comparison.py
python3 artifacts/terrain/organic/make-comparison.py
python3 artifacts/terrain/organic/final/build-benchmark.py
taskset -c 16 python3 test/run_tests.py --binary engine --build-dir artifacts/terrain/organic/final/benchmark-build --filter 'TerrainContourBenchmark/*' --artifacts artifacts/terrain/organic/final/benchmark --junit artifacts/terrain/organic/final/benchmark/junit.xml --timeout 600 -j1 --display-jobs 1
python3 artifacts/terrain/organic/final/analyze-benchmark.py
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/render/terrain -Ithird_party/nlohmann-json/include artifacts/terrain/organic/check.cpp src/render/terrain/TerrainMaterials.cpp -o artifacts/terrain/organic/final/check-sanitized
ASAN_OPTIONS=detect_leaks=1 artifacts/terrain/organic/final/check-sanitized
git diff origin/master...HEAD --check
```

The supplied SDL prefix is a local SDL 3.4.16 installation used for the approved preview and final checks. Adjust it for another host; dependency versions and dynamic-library paths are in `environment.txt` and `dynamic-libraries.txt`. The asset tests require the pinned encoder interpreter, as documented in the repository; an initial system-Python run did not satisfy that setup and is not counted as product validation.

The temporary benchmark compiles with the terrain harness flags from `v2-build.log` and links the final integrated engine objects using `final/engine-build.log`. It alternates both catalogs within one process, clears `compiled_pack` on both for identical source preparation, uses seed 7331, and records 12 paired 1024-frame blocks for stationary and moving cameras. Eight cold samples use fresh terrain caches after source preparation; they exclude asset-loading time. The final benchmark is pinned to CPU 16; unrelated builds were active on this host, so thread CPU time is reported alongside wall time. It measures software native terrain-cache prepare/draw, excluding screen presentation and the rest of a gameplay frame. It does not establish hardware-GPU or browser performance.
