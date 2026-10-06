# Evidence: terrain-map-salt (4c4c625ba on master 16970b936)

Local / VM verification

- Tested commit: 4c4c625ba, branch `terrain-map-salt`, base master 16970b936 (branch = base + 1 commit).
- Environment: Linux 7.0.0-31-generic x86_64, system GCC, pinned SDL3 prefix, Xvfb for display cases.
- Build: `GLOB2_SDL3_PREFIX=<prefix> scons -j24 release=1 server=0 engine-tests`.
- Coverage rationale: new presentation field on Map with a save-format bump (138) and protocol bump (58);
  risks are save/load continuity for older files, checksum neutrality, generator determinism, cache
  invalidation on reroll, and resolver consistency under seeds (partitions, shared edges, torus wrap).
- Commands and results:
  - `xvfb-run ... python3 test/run_tests.py --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'TerrainValidation/*' --filter 'EditorActionCoverage/*' --filter 'MapGeneratorDefaults/*'`
    -> 67 passed, 0 failed, 0 skipped (`terrain-editor-generator-suites-seed.log`).
  - `xvfb-run ... python3 test/run_tests.py --filter 'SavegameSafety/*'` (loads older fixture saves and maps)
    -> 1 passed, 0 failed (`tests-savegame.log`).
  - `python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'` regenerated
    `FourSquares1.g2mr` for the new sim version key (`regen.log`); `FourSquares1.verify-trace.txt` is byte-identical,
    so the simulation did not change. `python3 test/check_sim_revision.py --base origin/master` -> passes.
  - `LD_LIBRARY_PATH=<prefix>/lib python3 test/test_font_coverage.py` -> OK (new menu string).
- Gallery checksum trace (`checksums-seed.txt`) differs from master's (`checksums-master.txt`) from tick 0 because
  `MapHeader::checkSum()` folds in the file-format minor version (137 -> 138); the golden verification trace above
  is the simulation-equivalence evidence.
- Visuals: `compare-map-seeds-2x.png` (same cells, seeds 0/1/2 through the prototype harness),
  `terrain-gallery-seed.png` (in-engine gallery, seed 0 fixture).
- Omitted: no Windows/Android/browser runs (integer hashing and a serialized Uint32; no platform APIs).
