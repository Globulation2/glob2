# Trail local verification

Base: e1634ecda (origin/master fetched before validation; identical to HEAD).
Host: Linux x86_64; GCC 15.2.0 g++; release=1, server=0, native SDL/OpenGL.
Dependencies: /tmp/glob2-terrain-sdl-patched/prefix, versions match scons/sdl3-versions.json; current pinned embedded recording prefix built locally.

Static checks passed: strict translation validation across 33 catalogs, git diff --check, Python compilation, 16 unique opaque base frames, all 256 variant edge pairings, 15 clipped alpha overlays, unchanged ice overlays, reproducible asset pixels, identical compiled terrain-property dumps before/after.

Native release client, all native test binaries and MapReportHarness built successfully. Final focused run: 124 cases, zero failures/errors/skips; map-report contracts passed. Strict translations have zero structural errors; existing 134 pending unrelated entries in non-English catalogs are unchanged. The baseline is a git archive of the base revision; its test registry is narrowed to TerrainProperties for a local evidence driver. Temporary review includes produce scenes, saves and per-tick traces; they are removed from the working source after use. No production behavior is changed by the driver; its includes were removed and the final client/tests rebuilt before the final focused run.

Cross-platform checksum execution is not available in this local verification. Windows, macOS, Android and browser builds are omitted; the change preserves numeric IDs and all compiled terrain property values, with no simulation revision or format change.


## Commands and evidence

Build: `CCACHE=1 GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix scons release=1 server=0 tests build/linux/client/release/src/glob2 map-report-test -j12`.

Focused runner commands: `run-focused.sh`; output `focused-tests-final.log`, JUnit `tests/junit.xml`. Includes terrain presentation/properties/ecology, movement/map query, fertility, editor, settings/experiments, scripting, match setup. Map-report tests include import/export colors and legacy report names.

Visual driver: `run-review.sh`, `ReviewHarness.inc` and baseline driver. Successful output `review.log`. Before/after normal-scale editor and running colony screenshots for software and OpenGL are in `before/TrailReview/Trail_review_editor_and_moving_colony_display_artifacts/` and `after/TrailReview/Trail_review_editor_and_moving_colony_display_artifacts/`. Scenes include crossings, diagonal cells, narrow strips, isolated cells, broad patches, buildings, units and grass/sand/water/ice neighbors. Selected treatment B; initial treatments and comparisons are retained here. Settings captures include English, French, German, Arabic and Simplified Chinese; long help uses scrolling. All 33 translations have not had native-speaker review.

`python3 artifacts/trail/compare-traces.py` compares 513 per-tick checksums from identical seed 719, setup and orders before/after in both renderers. All four traces are byte-identical, SHA256 73134261bc47b47a6ae5b7ef5f32650d97b1087ad3ceed4fd98095ba71c7c0f5. Output `checksum-comparison.log`. Movement checks and actual moving-unit screenshots accompany these traces.

Pre-change complete game save at tick 512 loaded by both clients with `--run-game --load-game <before fixture>/legacy-road.game --ticks 640 --telemetry checksums --save final --output-dir <continuation directory>`. Both completed at tick 514 (the one-team fixture triggers victory); checksum sidecars are byte-identical. Results retain `road-terrain` and `ice-terrain`. New client's final save loaded again, completed at tick 546 and saved again (`roundtrip-load`). Old map fixture loads successfully (`old-map-load`), retaining the same required experiments; its empty colony ends at tick 2. These are load/roundtrip acceptance checks, not long CLI continuation benchmarks; the 512-step review driver supplies the sustained checksum comparison.

Static checks include unchanged version/revision files and external identifiers, no local review includes in production source, and 31 final PNG replacements. Source/prompt/conversion recipes are retained in `datasrc/gfx/trail/`. All source/runtime provenance hashes validated. Master fetched again before final validation, still the base revision, with no overlapping changes. No PR or commit created; evidence remains local and ignored and would need uploading for a future PR.


## Independent review cleanup

Two sub-agents independently reviewed C++/compatibility and artwork/docs/translations, then reviewed the fixes. Both report no outstanding concrete defects. Findings addressed: isolated Trail compatibility tests with explicit ground/air/build/resource invariants; remaining terrain test names/comments updated; experiment table repaired; pinned interpreter used directly in copyable source recipe; named artwork constants/fraying helper; context-managed image inputs; non-mutating durable artwork validator.

Final cleanup build passed (`review-build.log`), focused suite passed 125 cases plus map-report contracts (`review-focused-tests.log`), and five AI continuation cases passed (`review-ai-tests.log`). Strict translations still pass (`review-translations.log`). Regeneration with Pillow 12.2.0 preserved every runtime SHA256 and ice output; validator passed. Temporary corruption tests verified non-opaque pixels and broken joins are rejected even after updating the fixture's provenance hashes. No production simulation or runtime artwork changed during cleanup, so the preceding deterministic traces/screenshots still apply.

Master advanced to c963fbf5c784c952370fd981904f0e953668f334 during final fetch: only test/test_map_cli.py and test/test_map_image.py changed (profile isolation/timeout evidence). No production or source conflict; newer image fixture executed directly against the final client for integration validation. No merge or rebase needed.

Current-master map image CLI checks passed (`review-current-master-image.log`), including palette/import/export roundtrips and retained Trail color mapping.

Tested committed revision: e0e498a7708455d60d137f1123d24068c6abf881, based on e1634ecda9a2a2d31f47dfe766ddbcb40e364791, with current master c963fbf5c784c952370fd981904f0e953668f334 integration assessed as above. Re-ran focused tests, map reports, assets and strict translations on the committed revision (committed-*.log). Final build inputs are unchanged from the tested working tree.
