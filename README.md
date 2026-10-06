# Worker rig pipeline — reviewed local evidence

Tested source: `43a28506b14692cb91799c07345b8a0c79aaecaf`.
Base: `410ce0ff82d14c94463d2840d2591da0bf373210` (fetched before final validation).
The native runner records a clean source tree and its content digest in the attached logs.

This is an **opt-in worker-walk vertical slice**, not default activation. The six other animated clips remain baked, and the worker manifest is marked `accepted: false`. Gameplay timing, paint bytes/UVs, skin identities, authorization, saves and simulation version are preserved. The art candidate still needs socket/paint acceptance; physical platform and release performance gates remain open.

## Independent review and resolutions

Two sub-agents reviewed separate areas before cleanup, then reviewed the fixes.

| Finding | Resolution and regression coverage |
| --- | --- |
| C++ and TypeScript could disagree at serialized numeric boundaries | Define binary32 endpoints and binary64 validation arithmetic; round normalized storage consistently; shared adjacent-float acceptance/rejection fixtures cover duration, scale, heading, normal, quaternion and weight thresholds. |
| Weighted homogeneous coordinates could perturb affine camera translation | Accumulate a 3D position and apply the camera with w=1. A shared rounded-weight fixture reproduces 0.0390625 logical-pixel error with the old shader and zero after the fix. This is a focused correctness regression, not a claim that the general 0.05px budget was violated. |
| Exporter could produce metadata rejected by production decoders | Validate quantized camera dimensions/transforms, inverse binds, hierarchy scales, duplicate clips and frame-time rounding before returning bytes. Re-authoring produces byte-identical worker bytes; provenance refreshed. |
| Adapter accepted a pose request but ignored its sample | `SkinMesh::fromModel(model, clip)` now explicitly adapts all clip frames. Draw requests choose frames; unused native request type removed. Incomplete baked pose buffers fail without overwriting caller output. |
| Renderer mixed setup, uploads, binding and draw logic | Extract focused helpers, cache uniform locations once at link, retain palette uniforms across batches, and keep geometry caches bounded. Mixed rig/baked and two-paint tests verify pixel reuse and GL state restoration. |
| Studio evaluator allocated arrays per vertex | Use scalar accumulation and caller-provided output; projection shares the deformed output buffer. Shared pose/projection tests verify behavior. |
| Numerical browser checks were only local scripts | Add a repository Playwright conformance suite using the production shader and evaluator, with three engines and shared analytic/affine/worker assets. |

Neither reviewer found a remaining concrete renderer lifetime/state defect after cleanup. Their review does not replace physical-backend or appearance acceptance.

## Environment and build

- Linux x86_64; GCC 15.2.0; AMD Ryzen Threadripper 2950X.
- Native GL: Mesa 26.0.8 llvmpipe / LLVM 21.1.8 under Xvfb. This is software OpenGL, not physical-GPU performance evidence.
- SDL 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, SDL_net 3.2.0 and WebP 1.6.0 built using the repository dependency script.
- Blender 3.6.23; Node 22.22.1; dependencies from the platform lockfile.
- Native flags are recorded by the runner: C++20, `-O3`, release client, `server=0`, OpenGL enabled.

```sh
python3 scons/sdl3_dependencies.py --prefix artifacts/rig/sdl-prefix \
  --work artifacts/rig/sdl-sources --jobs 8
GLOB2_SDL3_PREFIX="$PWD/artifacts/rig/sdl-prefix" scons -j16 release=1 server=0 \
  unit-tests skin-preview skin-game-preview build/linux/client/release/src/glob2
```

[Build log](logs/review-final-build.log) and [final incremental build](logs/review-final-incremental-build.log). The incremental build includes the final shader correction and exact tested commit's provenance.

## Checks on the final source

| Check | Result | Evidence |
| --- | --- | --- |
| Native skin, rig, sprite readback, authorization/download suites | 44 tests pass | [log](logs/review-final-native-tests.log) |
| Adjacent asset, image, sprite and rectangle batch suites | 29 tests pass | [log](logs/review-final-render-regressions.log) |
| Web rig/projection and API publication | 66 tests pass | [log](logs/review-final-web-tests.log) |
| Chromium, Firefox, WebKit numerical conformance | 9 tests pass; all clip/frame mappings tested | [log](logs/review-final-browser-tests.log), [measurements](data/review-shader-agreement.json) |
| Asset/provenance and shader contracts | 4 tests pass | [log](logs/review-final-contracts.log) |
| Blender exporter rejection tests | 7 tests pass | [log](logs/review-final-exporter.log) |
| Full platform and web TypeScript, focused ESLint/Prettier, git diff checks | pass | [lint](logs/review-final-eslint.log), [format](logs/review-final-prettier.log) |
| Actual Studio preview: paused brush coverage, animation, context loss/restoration | pass; no page errors | [log](logs/review-studio-smoke.log), [capture](captures/studio-worker.png) |

Exact focused commands:

```sh
GLOB2_TEST_DISPLAY=1 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build/linux/client/release/test/glob2-unit-tests \
  -ts=SkinMesh,SkinAtlasCache,SkinMaterialMap,SkinReadback,SkinSprites,SkinModel,SkinModelRender,SkinAuthorization,SkinDownloads
GLOB2_TEST_DISPLAY=1 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build/linux/client/release/test/glob2-unit-tests \
  -ts=AssetLoader,SpriteLoad,SpriteSheets,ImageAssets,OpaqueRectangleBatch
npm exec --prefix platform -- vitest run --root platform \
  apps/web/test/skin-rig.test.ts apps/web/test/skin-projection.test.ts apps/api/test/skinPublishing.test.ts
PLAYWRIGHT_JSON_OUTPUT_NAME="$PWD/artifacts/rig/review-browser-report.json" \
  npm exec --prefix platform -- playwright test -c platform/apps/web/e2e/rig.config.ts --reporter=list,json
npm exec --prefix platform -- tsc -p platform/tsconfig.json
npm exec --prefix platform -- tsc -p platform/apps/web/tsconfig.json
PYTHONPATH=test/build_system python3 -m unittest test_skin_assets test_skin_shader_parity
artifacts/rig/blender/blender --background --factory-startup -t 1 --python-exit-code 1 \
  --python tools/skins/test_rig_export.py
```

The isolated direct X11 commands avoid this host's test wrapper selecting unavailable Wayland. Browser-reported GPU names may be masked: Chromium reports SwiftShader; Firefox/WebKit names do not establish Windows or macOS hardware coverage. The browser suite tests the shared shader body, not a compiled WebAssembly game. The Studio smoke is a component integration test, not the full authenticated workspace.

The additional native transform-feedback harness uses the exact production shader body and C++ evaluator across all 2834 worker vertices and 256 frames: maximum error 0.000001133 logical pixels / 0.000000290 normal vector. [Log](logs/review-native-shader-agreement.log), [harness](tools/gpu-agreement.cpp). This supplements the committed native raster and browser numerical tests.

The native mixed-render regression verifies lazy shader creation, every fixture frame, caller GL state, paint variants, buffer reuse, renderer resource recreation and forced CPU fallback. Existing sprite/authorization tests remain unchanged except for an added publication test proving that old and new immutable render revisions can coexist. No simulation change is intended, so replay checksum/save-format/version changes were not needed.

## Captures, sprites and size

The gameplay diagnostic uses an uncompressed copy of `test/fixtures/javascript/profile1-initial.game.gz`, the production Scene renderer and preview paints, with `GLOB2_SKIN_RIGS=1`. It captures skins visible, hidden and restored; it does not advance or save the diagnostic placements. [Gameplay](captures/gameplay-rig.png), [log](logs/review-game-preview.log).

The Studio [capture](captures/studio-worker.png) uses the actual production component and evaluator. The local smoke harness and native numeric capture harness are included under `tools/` for inspection; they originally run from `artifacts/rig/` in the source checkout.

Sprite export uses `--render-skin` twice with the same fixture manifest/paint/material, toggling `GLOB2_SKIN_RIGS=0/1`, under `SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a`. Both outputs are local only; no production skin was republished. [Sprite comparison](data/review-sprite-comparison.json), [rig manifest](data/rig-manifest.json), [baked manifest](data/baked-manifest.json).

All 58 output page hashes/sizes verified. Only the four worker-walk pages differ between the two recipes; the other 25 pages are byte-identical. Thumbnail extraction from the rig bundle also passed. [Thumbnail](captures/worker-thumbnail.png).

The sprite commands used the same `artifacts/rig/sprite-inputs` files (included under `inputs/`):

```sh
GLOB2_SKIN_RIGS=1 SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a \
  build/linux/client/release/src/glob2 --render-skin \
  --manifest artifacts/rig/sprite-inputs/manifest.json \
  --texture artifacts/rig/sprite-inputs/paint.png \
  --material artifacts/rig/sprite-inputs/material.png \
  --output-dir artifacts/rig/review-sprites-rig
# Repeat with GLOB2_SKIN_RIGS=0 and --output-dir artifacts/rig/review-sprites-baked.
python3 tools/skins/studio_thumbnails.py --sprite-bundle artifacts/rig/review-sprites-rig \
  --clip worker-walk --output artifacts/rig/review-thumbs
```

Studio smoke commands: `VITE_SKIN_RIGS=1 npm exec --prefix platform -- vite --host 127.0.0.1 --port 4297 platform/apps/web`, then `node artifacts/rig/studio-smoke.mjs`. The supplied harness was executed from that path. Gameplay capture command:

```sh
GLOB2_USER_DATA_DIR="$PWD/artifacts/rig/review-profile-game" \
  SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GLOB2_SKIN_RIGS=1 \
  GLOB2_SKIN_PREVIEW_DIR="$PWD/artifacts/rig/preview-assets" \
  SKIN_PREVIEW_SAVE="$PWD/artifacts/rig/preview.game" \
  SKIN_PREVIEW_CAPTURE=rig-game.bmp SKIN_PREVIEW_HIDDEN_CAPTURE=rig-hidden.bmp \
  xvfb-run -a build/linux/client/release/src/skin-game-preview -g
```

Worker asset size is unchanged by cleanup: 279,056 bytes, gzip 156,126 versus baked 17,502,756 / gzip 16,181,474 (**0.965% compressed ratio**). [Size data](data/asset-size.json). Full-catalog size/memory acceptance remains unverified.

## Coverage rationale and remaining release gates

Focused validation covers changed boundaries: parser/evaluator cross-language agreement; shader math; GL cache/state/fallback behavior; Studio projection; existing paint compatibility and exporter provenance; sprite recipe/publication compatibility. The native client and both render harnesses were built against the current base, including its swarm alpha change.

Still required before rollout: worker appearance approval (including visible socket faceting and representative published paints), six clip conversions, all-material/checker review, complete played-match acceptance, macOS/Windows and physical Linux GPUs, a lower-power GPU, full browser game integration and context restoration, authenticated Studio/publication/refresh workflows, and the specified M3/512/2,048-unit ten-pair p95/forced-miss/startup/upload/memory benchmarks. Earlier exploratory mean-frame benchmarks are not final-revision p95 evidence and are not used to claim acceptance here. No hosted CI or physical-backend pass is claimed.

The PR remains draft and the baked path remains default until these gates are met.
