Latest reviewed revision and refreshed evidence: [review/README.md](review/README.md). The captures and logs below retain their original revision.

# Software colony skin verification

Feature revision: `13949a60f6cd1907be11bdf6cf4bb1cdc8af9f18`. Integrated base: `857b69530254983efe1659b85467a17fa1973dfb`. Current master was fetched and overlapping WebP, PNG decoder, theme and render-pacing changes were integrated. Evidence belongs on this separate branch, not in the feature diff.

## Environment

Linux x86_64, kernel 7.0.0-31-generic; GCC 15.2.0; release C++20/O3; pinned SDL3 3.4.16 family including the PNG normalization patch; pinned libwebp 1.6.0; Node 24.19.0; Vitest 5.0.3; Playwright 1.63.0; Mesa/llvmpipe with Xvfb. The worker image builds with Ubuntu 24.04 GCC 13 and Node 22. This shared development host was running other builds: frame times are observations, not isolated hardware benchmarks.

## Verification commands

From the feature checkout, with platform/browser dependencies installed and the isolated test PostgreSQL database configured:

```sh
python3 scons/sdl3_dependencies.py --prefix "$PWD/build/sdl3-ci/prefix" --work "$PWD/build/sdl3-ci/sources" --jobs 8
GLOB2_SDL3_PREFIX="$PWD/build/sdl3-ci/prefix" scons -j8 release=1 server=0 unit-tests portable-renderer-test skin-game-preview build/linux/client/release/src/glob2
scons target=web release=1 -j8 web-serial web-threaded
docker build --target skin-render-worker --build-arg JOBS=8 -t glob2-skin-render-worker:software-skins -f deploy/Dockerfile .
build/linux/client/release/test/glob2-unit-tests --test-suite=SkinAuthorization,SkinDownloads,SkinSprites,SkinMesh,SkinMaterialMap,SkinAtlasCache,SurfaceCoverage,ImageAssets,RenderFramePacer --test-case-exclude='*[display]*'
GLOB2_SKIN_EXPORT_DIR="$PWD/artifacts/skin-sprites/current-export" SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/test/glob2-unit-tests --test-suite=SkinReadback
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/test/glob2-engine-tests --test-suite=SoftwareRenderer,PortableRenderer
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/skin-sprites/verify-final-revision
cmp artifacts/skin-sprites/verify-final-revision/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
```

Platform (run from `platform/`, using Node >=22.18):

```sh
node node_modules/vitest/vitest.mjs run apps/api/test/skins.test.ts apps/api/test/skinPublishing.test.ts apps/skin-render-worker/test/process.test.ts packages/db/test/schema.test.ts
node node_modules/vitest/vitest.mjs run apps/web/test/skins-workspace.test.tsx packages/protocol/test/fixtures.test.ts
node node_modules/vitest/vitest.mjs run packages/db/test/reliability.test.ts apps/api/test/reliability.test.ts
GLOB2_SKIN_RENDER_TEST_BINARY="$PWD/../build/linux/client/release/src/glob2" LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a node node_modules/vitest/vitest.mjs run apps/skin-render-worker/test/native.test.ts
npm run typecheck
```

Real browser replay coverage uses `platform/apps/web/e2e/server.ts` with `SKIN_E2E_RENDER_BINARY` set to the native binary (bakes the fixture), and the `/play/` deployment isolation headers. From `browser/`:

```sh
GLOB2_TEST_URL=http://127.0.0.1:4286 GLOB2_SKIN_REPLAY_API=1 GLOB2_SKIN_REPLAY_SOFTWARE=1 GLOB2_CHROMIUM_ANGLE=swiftshader npx --no-install playwright test tests/replay-skins.spec.js
GLOB2_TEST_URL=http://127.0.0.1:4286 GLOB2_SKIN_REPLAY_API=1 GLOB2_SKIN_REPLAY_SOFTWARE=1 GLOB2_SKIN_REPLAY_THREADS=threaded GLOB2_CHROMIUM_ANGLE=swiftshader npx --no-install playwright test tests/replay-skins.spec.js --project=chromium --project=firefox
```

The tests verify signed match/JWKS fetches, downloaded WebP pages and absence of source texture/material downloads in software mode, live shader use in serial GL, renderer readiness in threaded GL, and no browser exceptions/GL errors. WebKit successfully runs both serial renderers. Its automatic threading probe falls back to serial here; the initial explicit threaded assertions expose that limitation in `browser-current-threaded.log`. This is unavailable threaded coverage, not a passing threaded result.

## Export and visual fixtures

`input/` and `black-input/` contain source manifests and images. `current-export/` and `current-black-export/` contain all 29 pages plus the atomic bundle manifest. For either fixture:

```sh
LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/src/glob2 --render-skin --manifest input/manifest.json --texture input/texture.png --material input/material.png --output-dir new-export
```

The detailed fixture produces 2,957,408 image bytes, all 29 lossy; the black fixture produces 186,312 image bytes, all 29 lossless. Every page compares WebP Q90/method6 against lossless Q75/method4, preserving exact alpha. Ties select lossy. Recipe `bundled-images-v3-webp-only` is shared with the asset pipeline. No alternate image format selection applies to these sheets.

The readback test compares all 1,792 animated poses and 21 rotated swarms (seven mesh choices at 0,127,359 degrees) against production GL compositing, with all four materials. It also compares stored WebP alpha against the corresponding readback, across all clips/directions/phases. The native adapter tests validate actual CLI success, invalid inputs, unavailable GL, partial-output cleanup and source/recipe identity. Unit and API tests cover signature/hash/layout/bounds/corrupt caches, late readiness, pinned appearance, moderation, expiry, bounded retries, demand sharing, eviction, atomic readiness and immutable derivative records.

`comparison-zoom1.mp4` and `comparison-zoom2.mp4`: GL on the left, software on the right; same 32 animation phases at 800x600 per renderer and 25 fps. `comparison-zoom*.png` are matched final frames. Three skin sources exercise worker/warrior/explorer mixtures, separate ground shadows, fog edges and swarms in a 415-added-unit crowd. The flat black source intentionally renders black silhouettes. `scene-comparison-zoom*.png` and `scene-*-hidden.png` show the selected swarm, normal placement/fog, and the Show colony skins preference. Native placement/filtering tests include exact scales, enlarged scales and destination clipping. Restoring the preference at normal zoom restores identical captured pixels.

The replay screenshots are dynamic playback observations, not frame-exact GL/software pairs. Videos show visual correspondence; lossy RGB is deliberately allowed to differ. A maintainer should still play the result to assess feel.

## Scope and limitations

Simulation source, saves, replay encoding and SIM_REVISION are unchanged. The golden 702-checksum trace must match byte-for-byte; the diagnostic compares classic and skinned state and checks every draw leaves the simulation checksum unchanged. These are Linux checks. Windows, macOS, Android/iOS, hardware OpenGL, real Safari, multi-platform per-tick comparisons and production deployment are unavailable. Chromium/Firefox/WebKit are Playwright Linux engines. This validates a locally built deployed worker container, not a rollout to a public instance. No expensive hosted CI coverage is claimed.

## Final results

- Native focused suites: **46 cases, 2,632,179 assertions** passed; exhaustive readback plus stored alpha: **9,120 assertions** passed; software/portable renderers: **15 cases, 987 assertions** passed. Logs include clean revision provenance.
- Platform API/publication/worker/DB/UI/protocol/reliability: **8 files, 42 tests** passed; actual native worker/CLI integration: **2 tests** passed. Typecheck, focused lint/format, and **87 CI policy tests** passed. The shared pinned image exporter tests also passed (**27 tests**) along with Web asset/install contracts.
- Serial Chromium/Firefox/WebKit: **six replay cases** passed. Threaded Chromium/Firefox: **four replay cases** passed. WebKit threading is unavailable here and its serial fallback is recorded separately.
- Final container: healthy, first-attempt generation **95.124 seconds**, **2,962,792 total bytes** including manifest, all 29 images referenced atomically. SIGTERM logs graceful shutdown completion in 6 ms. An earlier integrated run took 58.673 seconds under a different host load. Both are retained; neither is a production throughput estimate.
- Simulation: the **702-checksum** native trace matches the committed golden reference byte-for-byte. Benchmark drawing preserves checksum state for all 240 CPU/48 GL frames in each mode, plus the animation captures.

Final shared-host software observations (three skins, 415 added units; 800x600):

| Zoom | Classic mean/p95 ms | Skinned mean/p95 ms | Decoded page bytes | Warm misses/decodes/evictions |
| --- | --- | --- | --- | --- |
| 1x | 17.91 / 21.89 | 28.66 / 39.55 | 57,326,148 | 0 / 0 / 0 |
| 2x | 69.08 / 100.73 | 33.60 / 35.99 | 57,326,148 | 0 / 0 / 0 |

Both software runs retain 37 demanded WebP pages totaling 2,705,954 compressed bytes; complete bundles for the three skins total 5,923,982 image bytes. Cache memory is about 54.7 MiB, within the 64 MiB limit; each software measurement discards 180 warmup frames and measures 60. GL measurements discard 32 and measure 16; llvmpipe skinned mean/p95 is 386.18/450.73 ms at 1x and 199.94/255.99 ms at 2x. The existing live mesh path can churn its atlas with many poses on this software GL driver. These unequal, concurrently loaded samples establish neither hardware GPU performance nor a controlled before/after speed improvement. The live path remains unchanged by the feature. Full raw samples and counters are in `performance.json` and the benchmark logs.

The final fixtures and comparison videos were regenerated using the final native build. Small non-benchmark preference captures precede the render-pacing merge; their skin rendering source is unchanged and they are supplemental visual evidence. Browser final-revision logs and captures use the rebuilt final runtimes. Build logs are compressed to keep this evidence branch small.

For local crowd reproduction, copy the fixture directories, saves and `serve.cjs` to `artifacts/skin-sprites/` in the feature checkout; run `node artifacts/skin-sprites/serve.cjs`, then `bash artifacts/skin-sprites/final-checks.sh` after removing an existing `current-green-export` output (the exporter intentionally refuses to overwrite it). The fixture server signs eight team tickets with the repository’s public test key and serves only loopback test artwork. The shell script contains the exact diagnostic frame counts, zooms, flags, cache directories and capture names.
