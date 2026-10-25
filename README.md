# PR #1258 verification

Tested head: `c6bb80d80f8f6e4ed2c8af61412652f541b95ecf`, clean working tree.
Base: `c51449e92f7557f9e136b897e9bf81b0753ed030`, fetched before final validation; PR head directly descends from that master.

Ubuntu 24.04.5, Linux x86_64; GCC 13.3.0; Python 3.12.3; Node 24.19.0; PostgreSQL 16 Docker container for isolated worker integration databases. SDL 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, libwebp 1.6.0. Existing pinned platform lockfile. Fast development build, -O0 -g1, C++20; no PCH/unity. Xvfb and Mesa 25.2.8 llvmpipe software GL. These are correctness/visual captures, not release performance measurements.

## Results and scope

- Full affected native rebuild, client, unit/engine harnesses and both preview tools: exit 0.
- 88 native cases, 40 jobs: 0 failed, 0 skipped. Includes bounded phase selection, historical catalog save continuity, derived meshes, invalid rotations, old/new malformed manifests, shared page hashes, HD logical shadow dimensions and graphics backends.
- 18 worker contract/process cases and 2 real renderer/publication integration cases: all passed. Integration stores and authenticates the complete 36-page derivative through PostgreSQL/blob storage.
- 28 Python tests: 5 asset/camera generator, 15 rotation, 8 render pipeline; all passed.
- TypeScript typecheck, focused ESLint and Prettier: exit 0.
- Fresh fixture PNG export: original 29 movement/swarm pages plus 7 turn pages, 224 distinct turn poses, authenticated hashes/sizes, transparent unused rows, no alpha at tile boundaries.
- Optional positive PNG-export/WebP-live readback: all 2,037 movement, turn and swarm poses match alpha exactly, RGB within existing tolerance. Covers opaque grayscale material ID 3/fur as well as invalid-ID rejection.
- Scene capture: 32 distinct frames with actual draw calls; classic/skinned simulation state checksums match over 34 frames. See scene-verification.json and scene.gif. Contact sheet and skin-turns.gif show all seven unit clips.
- Independent stationary-turn and publication reviewers completed with no outstanding findings. Follow-up fixes added native deployment capability validation, camera input guards, documented positive material regression and repaired immutable-frame publication in the standalone Scene tool.

Presentation only: simulation timing, serialized state, save offsets and replay/network gates are unchanged. No SIM_REVISION bump is needed. Smoother stationary rotation and restored fur in newly published PNG derivatives are intentional visual effects.

Omissions: other operating systems, physical GPUs, browser/mobile packages, release performance and hands-on gameplay were not run. Linux software-renderer checks and captures support the affected rendering and publication changes; they do not establish these omitted platforms. No new simulation algorithm requires cross-platform checksum qualification. No dependency was added.

Maintainer acceptance: the author accepts this focused evidence for this revision under AGENTS.md. Hosted checks may still be pending; their results are separate from this local qualification.

## Exact commands

Run from repository root, with the pinned SDL prefix and bundled Node on PATH. Logs omit no test failures. Temporary output paths are retained below to make the commands reproducible.

```sh
export GLOB2_SDL3_PREFIX=$PWD/artifacts/unit-turn/sdl-prefix
export LD_LIBRARY_PATH=$GLOB2_SDL3_PREFIX/lib
export LP_NUM_THREADS=2
export PATH=/home/bradley/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin:$PATH
export TEST_DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55432/postgres
python3 tools/dev_build.py -j16 tests build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 skin-preview skin-game-preview
python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary all --filter '*Skin*/*' --filter 'UnitAnimation/*' --filter 'UnitCatalog/*' --filter 'UnitHighResolutionCache/*' --filter 'UnitTeamColorCache/*' --filter 'UnitTeamShader/*' --display-jobs 2 -j8 --junit artifacts/skin-turns/pr-final/native.xml --artifacts artifacts/skin-turns/pr-final/native-artifacts
platform/node_modules/.bin/vitest run --root platform apps/skin-render-worker/test/bundle.test.ts apps/skin-render-worker/test/process.test.ts
GLOB2_SKIN_RENDER_TEST_BINARY=$PWD/build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 xvfb-run -a -s '-screen 0 1600x1400x24' platform/node_modules/.bin/vitest run --root platform apps/skin-render-worker/test/native.test.ts
python3 test/build_system/test_skin_assets.py
python3 tools/unit-animation/test_turns.py
python3 tools/unit-animation/test_render.py
platform/node_modules/.bin/tsc -p platform/tsconfig.json
(cd platform && node_modules/.bin/eslint apps/skin-render-worker/src/bundle.ts apps/skin-render-worker/src/main.ts apps/skin-render-worker/test/bundle.test.ts apps/skin-render-worker/test/native.test.ts)
(cd platform && node_modules/.bin/prettier --check apps/skin-render-worker/src/bundle.ts apps/skin-render-worker/src/main.ts apps/skin-render-worker/test/bundle.test.ts apps/skin-render-worker/test/native.test.ts)
xvfb-run -a -s '-screen 0 1600x1400x24' build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 assets render-skin --manifest artifacts/skin-turns/inputs/manifest.json --texture artifacts/skin-turns/inputs/texture.png --material artifacts/skin-turns/inputs/material.png --output-dir artifacts/skin-turns/pr-final/bundle
GLOB2_SKIN_EXPORT_DIR=$PWD/artifacts/skin-turns/pr-final/bundle python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'SkinReadback/*' --display-jobs 1 --junit artifacts/skin-turns/pr-final/roundtrip.xml --artifacts artifacts/skin-turns/pr-final/roundtrip-artifacts
GLOB2_USER_DATA_DIR=$PWD/artifacts/skin-turns/pr-final/scene/profile GLOB2_SKIN_PREVIEW_DIR=$PWD/artifacts/skin-turns/preview-assets SKIN_PREVIEW_SAVE=$PWD/games/gd-small-2ai.game.gz SKIN_PREVIEW_CAPTURE=artifacts/skin-turns/pr-final/scene/final.bmp SKIN_PREVIEW_BENCHMARK=artifacts/skin-turns/pr-final/scene/turns SKIN_BENCH_STATIONARY=1 SKIN_BENCH_FRAMES=34 SKIN_BENCH_FRAME_PREFIX=artifacts/skin-turns/pr-final/scene/frame xvfb-run -a -s '-screen 0 1600x1400x24' build/linux/client/debug/dev-dev_fast-true-linker-auto/src/skin-game-preview --renderer legacy-gl --window-size 1024x768 --no-fullscreen --mute
python3 artifacts/skin-turns/pr-final/preview.py
```

Source PNG inputs are derived from `test/fixtures/skins/authorization.json` sourceTextureHex/sourceMaterialHex and its canonical manifest claims. The bundle is included here for independent replay of the readback test; download the directory and point GLOB2_SKIN_EXPORT_DIR at it. Build-provenance records clean source SHA, tree digest and actual compiler flags. XML reports retain exact case inventory.
