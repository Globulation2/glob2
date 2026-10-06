# Evidence: terrain-border-variety (108f132ae on dbc12ed58)

Local / VM verification

- Tested commit SHA: 108f132ae (branch `terrain-border-variety`), base dbc12ed58 (master), branch = base + 1 commit.
- Environment: Linux 7.0.0-31-generic x86_64, system GCC, pinned SDL3 prefix (`scons/sdl3_dependencies.py`), Xvfb for display cases.
- Build: `GLOB2_SDL3_PREFIX=<prefix> scons -j24 release=1 server=0 engine-tests`.
- Coverage rationale: presentation-only change to `TerrainVisual` coverage resolution and catalog schema (version 3).
  Risks covered: shared-edge/torus consistency, partition sums, version-1 sampling kept bit-exact, C++/Python
  validator parity, compose cost, simulation checksum neutrality.
- Commands and results:
  - `python3 tools/terrain_tileset.py --check` -> Validated 5 materials and 80 texture frames
  - `python3 -m unittest discover -s tools -p test_terrain_tileset.py` -> 8 tests OK
  - `SDL_VIDEO_DRIVER=x11 env -u WAYLAND_DISPLAY xvfb-run -a -s "-screen 0 1920x1600x24" python3 test/run_tests.py --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'TerrainValidation/*'`
    -> 30 passed, 0 failed, 0 skipped (`terrain-suites-branch.log`). Version-1 geometry digest unchanged; the
    current-catalog digest updated after rendered review.
  - Gallery 256-tick simulation checksum trace: `checksums-master.txt` == `checksums-branch.txt` (identical).
- Visuals: `compare-1x.png`, `compare-2x.png` (prototype harness, master resolver vs branch, same seed/grid),
  `zoom-base4.png` vs `zoom-v3x4.png` (4x), `crop-v3-special.png` (lone cell, lone quadrant, diagonal pair, road),
  `crop-v3-ice.png`, `terrain-gallery-master.png` vs `terrain-gallery-branch.png` (in-engine gallery),
  `compare-gallery-2x.png`.
- Timing (`timing/`): gallery cold page composition, machine shared with other builds (load 5-28), so ranges only:
  master cold_ms 35.5 / 62.4 (dense 81 / 150) over 2 runs; branch cold_ms 50.2-92.1 (dense 130-381) over 6 runs,
  the lowest branch values measured at the lowest load. Warm/moving draw unchanged (~0.3-2 ms, noise).
  Standalone resolver harness, unloaded: master 65-70 ns/sample, branch 98-122 ns/sample for boundary tiles
  (pebble lookups and smoothstep). Composition results are cached per page, uniform tiles are unaffected.
- Omitted: Windows/Android/browser runs (pure integer C++; no simulation, save, replay or network data touched).
