# Local terrain border verification

Source: HEAD 159f99a12e18c2f5139b27208ba5beb779c6edb0 plus implementation.patch, SHA256 a17f249d1b2966a45ceda5592b2a9b118c674eb19285c0468671a09952df0893.
Fetched origin/master 6f8cf442f before final validation. Its changes since HEAD affect unrelated hiring/simulation components; no terrain, renderer, editor, dependency, or CI integration changes. No rebase performed.

Environment: macOS 26.6.2 (25G83), ARM64; Apple clang 21.0.0 (clang-2100.3.34.2), C++20 release -O3, SDL 3.4.18, SDL_image 3.4.8, Python 3.14.7. Build provenance including flags/source fingerprint appears in focused test logs. No new runtime dependencies.

Commands:
- scons -j8 release=1 server=0 engine-tests
- python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'SoftwareRenderer/*' --filter 'MapRenderResize/*' --display-jobs 1 -j4 --junit artifacts/terrain-borders/tests.xml
- python3 test/run_tests.py --binary engine --filter 'EditorTerrainPaint/*' --filter 'EditorActionCoverage/rerolling*' --filter 'EditorFlows/rerolling*' --display-jobs 1 -j4 --junit artifacts/terrain-borders/editor-tests.xml
- python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/halo edits*' --display-jobs 1 -j1 --junit artifacts/terrain-borders/halo-tests.xml
- python3 -m unittest discover -s tools -p test_terrain_tileset.py
- python3 tools/terrain_tileset.py --check
- python3 tools/terrain_profile_curves.py (dry run)
- git diff --check

Results: native build succeeded; 59 focused renderer/terrain/resize cases passed, none skipped. Python validator: 11 cases passed. Catalog: 29 materials / 1040 frames validated. Curve authoring dry run and whitespace checks passed. Seven editor cases passed, none skipped. After strengthening the halo test with exact cached/uncached pixels after edit/undo/redo, rebuilt successfully and reran that case: passed. The last change affected only that test; product code was already covered by the 59-case run. Final targeted report: halo-tests.xml.

Coverage: halo-only edits and restoration across page and wrap seams, normalized weights and seed determinism, preserved corner cores/narrow regions, six-pixel interior limit, unchanged sharp/ambiguous/junction masks, cached/uncached software and OpenGL pixels, fractional/HD zoom, animated water, renderer resize, seed save/load and unchanged simulation checksum.

Visual evidence: artifacts/tests/TerrainMaterials/natural_border_visual_fixture_and_cold_composition_timings_display_artifacts contains matching before/after native, HD (4x), and half-zoom PNGs, with composition.txt. Nine panels cover diagonal beach, headland, bay, S gully, one-cell channel, land strip, island, checkerboard, and three-material junction. Native/HD/half-zoom after images inspected: smoother diagonal flow and corners; channels, strips and island remain visible. Native and HD checkerboard panels were also checked pixel-identical before/after. Sharp constructed/hazard borders checked numerically against patch masks, not separate visual panels.

Performance: median of 5 full cold compositions, identical fixture/artwork/seed, contextual profiles disabled vs enabled in the same final binary. Native: 26.0721 -> 23.9879 ms (-8.0%). HD: 347.979 -> 323.978 ms (-6.9%). Median of 5 sets of 100 warm cached frames: 2.43931 -> 2.35191 ms (-3.6%). All within 15% cold / 5% warm budgets. This is a feature-toggle comparison, not separately compiled parent-commit binaries; the recipe/cache layout is shared. Do not generalize these fixture timings to other maps or platforms.

Plan adjustment: preserve the original mask in a 1px tile-edge band and blend to midpoint-based contextual geometry by 4px. This joins unchanged ambiguous/multi-material junctions without tile cracks and retains small historical edge detail. The six-pixel bound applies to contextual interiors, not legacy bands/fallback cells. Detailed rationale/limits documented in docs/assets/terrain-materials.md.

Limits: local macOS ARM64 software and OpenGL coverage only. Windows/Linux, browser/WebAssembly, Android and other GPU backends were not executed. No simulation code/storage changes; no save migration or SIM_REVISION change. Rendering fixture checksum 904197346 remains unchanged after both presentations. Halo undo/redo coverage restores/reapplies vertex state; terrain painting itself has no general terrain undo UI. Human play review of travel-boundary readability remains outstanding.

All evidence is ignored local development material. Product commit af4275195c9bc447467c5537aaceeddea3477bea, PR #972. Evidence published on a separate branch; no generated evidence enters master.

Initial halo test failure retained in glob2-terrain-tests.log: setup inadvertently used three-material cells, so the resolver correctly ignored the edited halo. Changed fixture background to sand, producing ordinary two-material shores; all final runs pass.

## Committed revision, merge preparation

Clean committed revision af4275195c9bc447467c5537aaceeddea3477bea rebuilt with the same flags. Binary provenance: committed-build-provenance.json, source tree SHA256 853277773007d843488a9a6cd9be377a3aa1a9729c607ac9662617384e455c05.
Latest fetched master b20563a8909f30c111e9955a2fbe5c46abe42df1. git merge-tree --write-tree HEAD origin/master succeeds (tree 3cfc5138cf3b166a12cdbc229b8fdc853b70b7d7). Additional changes since prior base affect browser replay fixtures, CLI smoke checksum text and the LLVM 18 signed repository setup for Ubuntu generator tooling. Reviewed CI configuration change: unrelated to local terrain build/validation; no changed terrain/editor/renderer/dependency inputs. No conflicts, no rebase needed. Final verification is PR head, not an executed merge-result build.

Final commands (exit 0):
- scons -j8 release=1 server=0 engine-tests
- python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'SoftwareRenderer/*' --filter 'MapRenderResize/*' --filter 'EditorTerrainPaint/*' --filter 'EditorActionCoverage/rerolling*' --filter 'EditorFlows/rerolling*' --display-jobs 1 -j4 --junit artifacts/terrain-borders/committed-tests.xml
- python3 -m unittest discover -s tools -p test_terrain_tileset.py

66 native tests passed, zero failed/skipped (97.2s); 11 Python tests passed (67.498s). Final median native cold 24.593 -> 21.5545 ms, HD 322.361 -> 291.277 ms, warm 2.31684 -> 2.28535 ms. All within budgets; feature-toggle comparison, not an independent parent binary. Simulation checksum remains 904197346. Latest screenshots regenerated by final committed run.
Maintainer acceptance: user reviewed the images and explicitly requested merging in this chat. Cross-platform/browser execution and human in-game play review remain unperformed; user authorized proceeding with these stated limits.
