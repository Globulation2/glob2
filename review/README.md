Latest integrated feature revision: `e2b07c14957600d8e5882da94e4fd7bfd6017fc0`, based on master `da0d0d97f01083b98a954eaa5948f9f2992c6bc2`. See the final integration section below; older detailed checks retain their source revision.

Review and final validation for `3c3c1d7bc21e9bce239446e495f4f77c050e4b76`, based on master `0d26564bc27d547740b9fbda1395df667d5f1bb8`. Overlapping parallel asset preparation, WebP wire delivery and Windows encoder preparation changes are integrated. Linux x86_64, GCC 15.2, release C++20/O3, pinned SDL 3.4.16 and libwebp 1.6.0, Node 24.19, Mesa/llvmpipe/Xvfb, Playwright 1.63. Docker uses Ubuntu 24.04/GCC 13/Node 22.

Independent subagent review and self review fixed completed offscreen downloads retaining all four slots, bounded compressed buffers when disk writes fail, extracted worker bundle validation, clarified cache ownership, and added runtime encoder verification. The reviewer rechecked the fixes and integration, finding no remaining blockers.

Signed source identity now separates immutable PNG/WebP inputs from live WebP renditions. Authorization recomputes the source manifest, retaining compatibility with old descriptors. The CLI decodes verified source bytes directly, independently of the game's WebP-only asset loader. The migration follows master’s rendition migration as 0042. Static libwebp dependencies retain the correct link order.

Visual inspection exposed two browser regressions missed by download-only checks: unchanged serial canvas size assignments cleared software frames; startup completion terminated the replacement threaded gameplay loop. Fixed both, added actual bitmap assertions and startup/settings/exit coverage, and reviewed the shared canvas adapter. The native capture harness now pumps the production asset finalizer queue, so captures include live prepared meshes.

Final focused checks:
- Native: 63 cases / 2,632,862 assertions; production readback: 1 case / 9,120 assertions; software/portable renderer: 15 cases / 987 assertions. Fresh binaries record this revision. Readback compares all clips/directions/phases/materials and seven swarm meshes at three angles with the unchanged original export pixels and exact encoded alpha.
- Golden match: all 702 per-tick checksums match the committed trace. No simulation/save/replay/SIM_REVISION change.
- Platform/API/DB/worker/protocol/UI/account export: 44 tests in nine files; actual native CLI/worker: two tests, including original PNG sources and rejected CLI inputs. Typecheck, affected ESLint and worker formatting passed; no TypeScript changes followed those checks.
- Browser units: 51 tests; serial signed replays: six tests across Chromium/Firefox/WebKit; threaded signed replays: four tests across Chromium/Firefox; threaded startup/settings/clean exit: two tests. Software verifies opaque colored canvas pixels, downloaded WebP pages and absence of mesh source downloads/GL context. GL verifies live shader use or threaded asset readiness. Screenshots are attached in evidence.
- CI policy: 87 tests; native/mac image dependency inventories: 11 tests. Hosted checks are cheap contracts; expensive hosted jobs were not requested.

[Review logs, commands and captures](https://github.com/Globulation2/glob2/tree/codex/software-colony-skins-evidence/review) and [earlier full exports, normal/enlarged captures and animation comparisons](https://github.com/Globulation2/glob2/tree/codex/software-colony-skins-evidence) identify their revisions separately. The final crowd uses three distinct skins, 415 added units and 54.7 MiB decoded pages without measured warm cache churn. Frame timings come from a shared host and llvmpipe, not controlled hardware benchmarks.

Windows/macOS/mobile, hardware GPU and production rollout execution remain unavailable. WebKit uses serial fallback in this Linux environment. Native cross-platform simulation equivalence is not claimed; the presentation-only changes retain the local golden checksums. A maintainer playing the result remains part of visual/feel review.

The final worker image is identified in `final-container-state.log`. Its rootless, read-only container produced the linked `deployed-bundle/` in 57,585 ms, 2,962,792 bytes, on the first attempt. SIGTERM completed in 3 ms, exit 0. Native outputs use the final revision's binaries; prior root-level bundles are retained only as comparison fixtures.

The typecheck/ESLint/format logs precede the browser-only final fixes; TypeScript sources, dependencies and encoding settings are unchanged since those checks. All native/browser/CLI/worker execution and the 44 platform tests were refreshed after the final integration. Older captures/videos are labeled separately in the parent README.

Commands (from the feature checkout; Node 24.19 is used for TypeScript):

```sh
GLOB2_SDL3_PREFIX="$PWD/build/sdl3-ci/prefix" scons -j8 release=1 server=0 unit-tests portable-renderer-test skin-game-preview build/linux/client/release/src/glob2
scons target=web release=1 -j8 web-serial web-threaded
build/linux/client/release/test/glob2-unit-tests --test-suite=SkinAuthorization,SkinDownloads,SkinSprites,SkinMesh,SkinMaterialMap,SkinAtlasCache,SurfaceCoverage,ImageAssets,RenderFramePacer,AssetLoader,SpriteLoad --test-case-exclude='*[display]*'
GLOB2_SKIN_EXPORT_DIR="$PWD/artifacts/skin-sprites/current-export" SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/test/glob2-unit-tests --test-suite=SkinReadback
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/test/glob2-engine-tests --test-suite=SoftwareRenderer,PortableRenderer
build/linux/client/release/src/glob2 --verify-match test/fixtures/multiplayer/FourSquares1.g2mr --map maps/FourSquares1.map.gz --out artifacts/skin-sprites/verify-reviewed-final-current
cmp artifacts/skin-sprites/verify-reviewed-final-current/checksums.txt test/fixtures/multiplayer/FourSquares1.verify-trace.txt
node --test browser/unit/*.test.js
python3 -m unittest discover -s test/build_system -p 'test_ci*.py'
docker build --target skin-render-worker --build-arg JOBS=8 -t glob2-skin-render-worker:software-skins -f deploy/Dockerfile .
```

Platform, from `platform/`:

```sh
node node_modules/vitest/vitest.mjs run apps/skin-render-worker/test/process.test.ts apps/api/test/skinPublishing.test.ts apps/api/test/skins.test.ts apps/api/test/reliability.test.ts packages/db/test/schema.test.ts packages/protocol/test/fixtures.test.ts apps/api/test/accountExport.test.ts apps/web/test/skins-workspace.test.tsx
node node_modules/vitest/vitest.mjs run apps/api/test/webpRendition.test.ts
GLOB2_SKIN_RENDER_TEST_BINARY="$PWD/../build/linux/client/release/src/glob2" LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a node node_modules/vitest/vitest.mjs run apps/skin-render-worker/test/native.test.ts
```

Browser, from `browser/`, using the isolated platform e2e server on 4286 with `SKIN_E2E_RENDER_BINARY` set to the current native binary:

```sh
GLOB2_TEST_URL=http://127.0.0.1:4286 GLOB2_SKIN_REPLAY_API=1 GLOB2_SKIN_REPLAY_SOFTWARE=1 GLOB2_CHROMIUM_ANGLE=swiftshader npx --no-install playwright test tests/replay-skins.spec.js
GLOB2_TEST_URL=http://127.0.0.1:4286 GLOB2_SKIN_REPLAY_API=1 GLOB2_SKIN_REPLAY_SOFTWARE=1 GLOB2_SKIN_REPLAY_THREADS=threaded GLOB2_CHROMIUM_ANGLE=swiftshader npx --no-install playwright test tests/replay-skins.spec.js --project=chromium --project=firefox
GLOB2_TEST_URL=http://127.0.0.1:4286 GLOB2_TEST_ENTRY_PATH=/play/ GLOB2_CHROMIUM_ANGLE=swiftshader npx --no-install playwright test tests/runtime-build.spec.js -g 'threaded startup' --project=chromium --project=firefox
```

Crowded captures can be reproduced using the parent fixture files and `serve.cjs`, which serves lossless WebP live renditions while preserving the original bundle source in signed descriptors. Set `GLOB2_PLATFORM_DIR` to the checkout's `platform/` and run this server (8767); it refreshes the parent's fixture `assignment.json`. Use an absolute assignment/save/cache path, and relative capture paths in the feature checkout:

```sh
SKIN_PREVIEW_SAVE="$PWD/artifacts/software-skin-review/crowd-three.game.gz" SKIN_PREVIEW_ASSIGNMENT="$PWD/artifacts/software-skin-review/assignment.json" SKIN_PREVIEW_CACHE="$PWD/artifacts/skin-sprites/review-final-cache" SKIN_PREVIEW_CAPTURE=artifacts/skin-sprites/review-cpu.bmp SKIN_PREVIEW_BENCHMARK=artifacts/skin-sprites/review-cpu SKIN_BENCH_TEAMS=3 SKIN_BENCH_FRAMES=240 SKIN_BENCH_WARMUP=180 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 xvfb-run -a build/linux/client/release/src/skin-game-preview -G -m -s 800x600 -F -C
```

GPU uses the same command with `-g`, 48 frames/32 warmup, and a `review-gpu` prefix. CPU: classic mean 12.42 ms/p95 17.72; skinned 23.42/p95 25.50; decoded 57,326,148 bytes (54.7 MiB), 37 pages, zero measured warm misses/decodes/evictions. GPU/llvmpipe: classic 37.92/p95 42.92; skinned 269.34/p95 294.06. These timings include shared-host interference and llvmpipe atlas churn; they do not establish hardware performance.

Final integration with AI Music Studio
------------------------------------

Revision `e2b07c14957600d8e5882da94e4fd7bfd6017fc0` integrates master `da0d0d97f01083b98a954eaa5948f9f2992c6bc2`. The reviewer checked the merge: both Docker/Compose workers and packages remain intact; blob GC retains both features' references; skin migration is now `0043` after music's `0042`; optional account export access follows master. Regenerated protocol fixtures did not change. Fresh dependencies, typecheck, affected ESLint, Compose configuration, 44 platform/API/UI/protocol tests plus 14 DB/worker retention tests, two actual native CLI/worker tests and 87 CI policy tests pass. Native binaries were rebuilt; the 63-case suite and all 702 golden checksums pass at this revision. These `final-ai-*` logs supersede earlier matching checks.

The final worker image generated the same bundle in 57,485 ms, 2,962,792 bytes, first attempt. Health was healthy, rootless/read-only settings retained, and SIGTERM completed in 4 ms with exit 0. Fresh Chromium serial/threaded GL/software replays exercise the API and migration at the integrated revision. Renderer/browser source inputs are unchanged by this platform-only merge; the earlier three-browser/dual-runtime visual coverage remains identified at its own revision. The startup test's final change permits only WebKit's verified unsupported serial fallback: two passes and one explicitly unsupported skip, preserving required threaded mode on Chromium/Firefox.

Master was fetched again before merge; `c8d2d4c9f` advances only the unrelated UI synthetic presentation harness. It was not merged solely to chase unrelated commits.

Additional platform retention command:

```sh
node node_modules/vitest/vitest.mjs run apps/worker/test/reliability.test.ts packages/db/test/reliability.test.ts
```
