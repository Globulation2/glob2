# Terrain material renderer — review evidence

Tested implementation: [`6ed2e2f15332d88bad9f91494924afb146ba1d88`](https://github.com/Globulation2/glob2/commit/6ed2e2f15332d88bad9f91494924afb146ba1d88).
Integrated base: `9bfee5aeedac41e12f771f7aa3f05f419fb9a2a1`.
Original-renderer comparison: `e1634ecda9a2a2d31f47dfe766ddbcb40e364791`.

The later master `dbc6253427edc1ca71227ec91a8cd9ab66b5d326` was assessed with `git merge-tree --write-tree HEAD origin/master`: clean merge, tree `8112bedacd77e750401fdc74288d3ef50d41b0e7`. Its additional changes concern gradient preparation, a gradient benchmark CI contract and removal of an Emscripten include from audio. They do not modify the renderer or asset pipeline. That newer merge tree has not been compiled or executed; the runtime claims below apply to the tested head and integrated base above.

## Visual evidence

[Normal play scale](comparison-normal.png) · [2× nearest-neighbor detail](comparison-detail.png) · [before original](before-terrain-gallery.png) · [after original](after-terrain-gallery.png).

![Before and after](comparison-normal.png)

These are engine screenshots of the same 32×32 map, seed 7331, at 1024×768. Ice, thin cobblestone roads, isolated diagonal contacts, a road crossing sand/water, and wrapped corner cells are present. The side-by-side sheets add labels and crop/enlarge pixels; they do not synthesize terrain art. The default material retains the requested cobblestones while the engine uses upstream's Trail identity and stable ID 4.

The screenshots support visual inspection, not maintainer acceptance. Human in-game review remains outstanding. The map-import fixture [new-terrain.map.gz](new-terrain.map.gz) and [semantic export](new-terrain-export.png) are also supplied; this is a separate import/export fixture, not the gallery layout.

## Verification

- **78 engine cases passed**, 0 failed, 0 skipped: [JUnit](checks/final-integration.xml), [log](checks/final-integration.log).
- **80 unit cases passed**, 0 failed, 0 skipped: [JUnit](checks/unit-final.xml), [log](checks/unit-final.log). The runner groups headless cases into jobs; 13 jobs represent 80 cases.
- Asset compiler: 3 tests passed. Packaging: 27 tests passed with the pinned encoder Python. Web asset planning: 19 tests, 1 existing skip. Browser package contracts: 17 passed. CI selector: 20 passed. [Logs](checks/).
- Map-image CLI checks passed, including ice/Trail semantic import, save/load and export: [log](checks/map-image-final.log), [exact CLI invocations](commands.json).
- Upstream Trail artwork provenance remains valid: [log](checks/upstream-trail-assets.log).
- All **256 per-tick checksums are identical** before/after, also in all six benchmark runs. [Before trace](before-checksums.txt), [after trace](after-checksums.txt). SHA-256 of either trace: `10c411e808f59702902962f972d9219026baae8c832392af17760c72bdfeaafc`.

The trace uses two workers on ice and road, the same seed/map, and an empty order stream. It checks this fixture on Linux; it does not establish all-map or cross-platform determinism. Production simulation code, serialized terrain-frame selection and RNG calls are unchanged by this PR; the simulation revision is unchanged relative to the integrated base.

Coverage includes all 256 four-corner label configurations, legacy corner decoding against engine lookup, both torus axes, diagonal separation, 64 materials, an additional compositor material, deterministic weighted variants, malformed/stale/missing packs, source revision invalidation, animation, fractional zoom, native/partial HD assets, cache eviction/budget refusal, actual tile fallback, software/OpenGL pixel parity, portable renderer primitives, torus rendering, previews, fog/discovery, current-save continuation, legacy saves 84/88 and replay/network acceptance contracts. GPU page admission downsampling is exercised by the HD case; actual low-limit physical devices and injected allocation failures have not been exhaustively exercised.

The original comparison was built from an archived source tree with only the identical validation fixture appended. An audit of 1,739 source/build/test files found only that test-file difference: [audit](baseline-source-audit.json), [fixture patch](baseline-fixture.patch). No production objects from the refactored renderer were reused in that baseline executable.

## Performance and memory

CPU-pinned (`taskset -c 31`) alternating baseline/current runs, three pairs, same map and camera path. Warm uniform/dense results are medians of five batches of 60 frames per process, then medians across three processes. Mixed warm is 30 frames per process. Times cover terrain cache preparation and drawing; they exclude simulation and scrolling-ocean drawing.

| Scenario | Before ms | After ms | Change |
| --- | ---: | ---: | ---: |
| Mixed cold | 27.282 | 161.784 | +493.0% |
| Uniform cold | 17.422 | 61.339 | +252.1% |
| Dense boundaries cold | 38.130 | 443.819 | +1064.0% |
| Mixed warm (30 frames) | 3.738 | 4.071 | +8.9% |
| Uniform warm | 3.540 | 3.128 | -11.6% |
| Uniform moving camera | 4.145 | 3.537 | -14.7% |
| Dense boundaries warm | 3.819 | 3.296 | -13.7% |
| Dense boundaries moving camera | 4.268 | 3.326 | -22.1% |

[Raw runs, commands and host load](benchmarks/runs.json) · [medians](benchmarks/medians.json) · [runner](benchmarks/run.py).

The host was heavily shared and CPU-pinned timings differ substantially from unpinned runs. These are observations, not a release FPS claim. No median warm regression exceeded 10%; an earlier warm regression was investigated and reduced by comparing page neighborhoods once before decoding recipes. **Cold dense composition is substantially more expensive** than copying legacy sprites and can produce a first-view hitch. This remains a performance review concern, especially on slower machines and during rapid edits. The unpinned integrated run measured 79.8 ms dense cold composition; the contended pinned median above was higher. Large-map traversal, physical GPU upload costs and sustained editing should be reviewed before declaring performance acceptance.

Current median initial terrain source decode/preparation: **68.612 ms**, measured separately before composition. Global graphics fixture loading: **3554.730 ms** (all graphics/setup, not terrain-only). Baseline does not have this separate loading timer. [Integrated timing](after-timing.txt).

Each uniform/dense run recorded 4 cold page rebuilds and 3,350 cache hits; the 32×32 map fits four pages and does not measure continuous eviction pressure. The separate software renderer test traverses 40 pages and verifies bounded eviction.

| Allocation scope | Native software | Native OpenGL | Partial HD OpenGL |
| --- | ---: | ---: | ---: |
| Composed CPU page accounting | 4,508,000 B | 4,508,000 B | 17,090,912 B |
| Additional uploaded texture storage | 0 B | 4,194,304 B | 5,592,384 B |
| Prepared source pixels/pages | 4,456,448 B | 4,456,448 B | 4,517,888 B |

CPU page accounting includes conservative metadata reservation; GPU bytes are the renderer's texture allocation delta including uploaded mip levels. Prepared sources are additional CPU storage. These are terrain allocations, not whole-process RSS or physical VRAM measurements. Individual memory reports are stored alongside this file. Software composed pages retain the 32 MiB budget; GPU-mode composed pages use a separate conservative 128 MiB budget and reduce HD sampling when needed.

## Environment, commands and limits

Ubuntu 26.04.1, x86_64, GCC 15.2.0, release `-O3`, C++20, client build. SDL 3.4.16 / SDL_image 3.4.6 / SDL_ttf 3.2.2 / SDL_net 3.2.0 and libwebp 1.6.0, with repository-pinned SDL patches. [Environment and GL driver](environment.txt). OpenGL is Mesa 26.0.8 **llvmpipe**, not a physical GPU.

The final unit run explicitly selects the patched SDL prefix with `LD_LIBRARY_PATH`. An earlier run accidentally selected the machine's unpatched SDL family and failed the existing PNG16 normalization case; it passed with the correct runtime. Final engine and unit evidence uses the patched runtime consistently.

Build (exit 0):

```sh
GLOB2_SDL3_PREFIX=/tmp/glob2-terrain-sdl-patched/prefix scons -j8 release=1 server=0 tests build/linux/client/release/src/glob2
```

Tests (all exit 0):

```sh
export LD_LIBRARY_PATH=/tmp/glob2-terrain-sdl-patched/prefix/lib
python3 test/run_tests.py --binary engine --filter 'TerrainMaterials/*' --filter 'TerrainPresentation/*' --filter 'TerrainProperties/*' --filter 'SoftwareRenderer/*' --filter 'TerrainValidation/*' --filter 'HighResolutionIntegration/*' --filter 'PortableRenderer/*' --filter 'MapRenderGeometry/*' --filter 'Torus*/*' --filter 'MapPreview/*' --filter 'TeamStatsSave/*' --filter 'MatchSetup/*' --filter 'TerrainEcology/*' --artifacts artifacts/terrain/final-integration --junit artifacts/terrain/final-integration.xml --timeout 300 -j 4 --display-jobs 1
python3 test/run_tests.py --binary unit --filter 'Sprite*/*' --filter 'ImageAssets/*' --filter 'AssetLoader/*' --filter 'Replay*/*' --filter 'PlatformProtocol/*' --filter 'FertilityField/*' --filter 'MusicBuffer/*' --filter 'MusicProducer/*' --artifacts artifacts/terrain/unit-final --junit artifacts/terrain/unit-final.xml --timeout 300 -j 4 --display-jobs 1
python3 test/test_map_image.py build/linux/client/release/src/glob2
python3 -m unittest discover -s tools -p test_terrain_tileset.py
/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python -m unittest discover -s test/build_system -p test_package_assets.py
python3 -m unittest discover -s test/build_system -p test_web_assets.py
python3 -m unittest discover -s test/build_system -p test_browser_package.py
python3 -m unittest discover -s test/build_system -p test_ci_policy.py
python3 tools/artwork/validate_trail.py
python3 artifacts/terrain/benchmark-final.py
```

Not run: Windows, macOS, Android, browser/WASM execution, physical GPU drivers, cross-platform checksum comparisons, a full engine suite, or manual gameplay acceptance. Browser asset/package contracts do not establish browser rendering correctness. These gaps and cold-composition cost keep the PR in draft. Maintainer acceptance is pending; no acceptance is claimed on another person's behalf.
