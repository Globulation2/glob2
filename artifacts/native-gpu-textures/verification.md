# Native GPU texture preparation verification

Tested clean source: **709f21bdc8e60964e270a747cc9dfc7be7489028**; PR base **79cd352c41ec43a76b72f0359ab2092026249215**.
Current fetched master **68ca36e77590047794778c4936192b92d657aee5** integrates without conflicts (git merge-tree result 1478872fc55d90ee78de62ffe9267099ae4fa38b). Newer changes cover release tooling, optional iOS camera/HID exclusion and safe static-style cleanup, without changing this renderer's algorithms or native/browser dependency configuration. No rebase required. Tests run on PR head, not that merge tree.

## Environment and builds

Ubuntu 26.04.1 x86_64; GCC15.2.0; SDL3 3.4.16/image3.4.6/ttf3.2.2/net3.2.0. Actual NVIDIA RTX2070 SUPER, driver580.178.04/OpenGL4.6/GLSL4.60; forced legacy Mesa26.0.8 llvmpipe OpenGL2.1/GLSL1.20 with GL_EXT_gpu_shader4. Native release GAG library and unit binary use -O3 -s, no fast/PCH/unity mode. Final release binary records the exact clean commit and flags. Native development client, unit and engine binaries fully built with -O0 -g1 for correctness only.

Both browser release variants built with Emscripten4.0.15, -O2 -g2/web_profile=1. Last commit only updates native TerrainPresentation test double; production browser source matches final built revision. Chromium153/Playwright1.63. The changed five production renderer files cross-compile with actual Android NDK28.2.13676358 arm64/API24 release -O2 -g -fPIC, pinned Android SDL dependencies; all exit0 (full argument vectors in android-compile-results.json).

## Results

- Native GPU correctness: 4 cases, 481 assertions passed on NVIDIA; SDL OpenGL ES GPU/fallback: 2 cases, 74 assertions passed. SDL premultiplication compares all 65,536 channel/alpha combinations, revisions/queued draws, clipping and renderer state. Native mip tests compare every CPU-reference level, alpha extremes and restored GL state/fallbacks.
- Forced OpenGL2.1 integer extension path: 2 native cases passed under process-isolated display runner; standalone probe also passes 15 exact mip chains.
- Release unit regressions: 12 headless ImageAssets/SpriteSheets; 6 GPU/SpriteSheets display; 7 ordered sprite/unit batches and cold frame preparation cases passed.
- Development engine regressions: 6 PortableRenderer/HighResolutionIntegration display cases passed, including software and OpenGL HD art/zoom/cursor/replay screenshots; 5 TerrainPresentation headless cases passed, including terrain save/load/checksum contract. OpenGL HD fixture takes 171 seconds on llvmpipe, not a release performance measurement.
- Build contracts: 461 tests passed, 3 skipped using the managed asset-encoder Python environment.
- Browser: hardware Vulkan WebGL2 playable match/resize and serial many-core HD startup pass. Threaded many-core HD startup passes with Chromium SwiftShader (41.6 seconds), 15-16 workers, bounded heap and no allocation/GL errors; see diagnostic JSON and screenshot. Vulkan retry fails the worker WebGL2 capability probe and correctly selects serial; direct worker probe confirms Atomics/SAB pass but offscreen WebGL2 is unavailable in that configuration. This local hardware/Chromium worker capability limit is explicit, not presented as a threaded hardware GPU pass.

## Release texture preparation benchmark

Four warmups + 32 measured updates each, same final release binary; includes GPU completion via glFinish (native) or render/readback (SDL), not just enqueue time. CPU native mip comparison uses the production AssetImage alpha-weighted filter. Shared host with other builds; these are texture preparation timings, not game FPS or mobile measurements.

| Path | Square size | CPU completed ms | GPU completed ms |
|---|---:|---:|---:|
| SDL linear preparation + render/readback |128|0.5034|0.4357|
| SDL linear preparation + render/readback |256|0.9929|1.6174|
| SDL linear preparation + render/readback |512|3.3556|2.6009|
| SDL linear preparation + render/readback |1024|9.6590|9.4872|
| Native mip preparation + upload |128|0.8989|3.0988|
| Native mip preparation + upload |256|1.0575|3.9728|
| Native mip preparation + upload |512|2.9535|6.3663|
| Native mip preparation + upload |1024|25.0262|7.6908|

GPU is standard at every supported size as requested; small preparation operations can be slower from setup/driver overhead. Authored worker-prepared/compressed mip chains keep their existing path. Pixels and opacity remain equivalent; no visual style/pacing/rules changes intended.

## Coverage and limits

This changes texture preparation/cache ownership and mobile asset requests, so focused pixel/state/renderer/asset regressions address the actual risks. No simulation, scheduling, saved-state format, replay/network acceptance or dependencies changed; no SIM_REVISION update or new cross-platform per-tick corpus is required for presentation-only work. Terrain save/load/checksum contract and HD replay visual fixture pass. Recording and simulation threading are unchanged and were qualified in #1094.

Full native release client/engine build and Android release client builds were deliberately stopped after focused renderer validation; release unit library, full native development client/engine, full serial/threaded release browser builds and Android changed-object compilation succeeded. Android application link/package/physical-device performance and Windows/macOS/iOS/Metal/big-endian execution remain unverified on this host. Visual screenshots inspected; extended maintainer gameplay is not claimed. GPU preparation fallbacks remain available.

Retained failures: initial API test-double mismatch was fixed in 709f21b and full native development client/harness builds passed; host Python without WebP support caused asset contract failures, resolved by running the pinned managed asset Python (full461 suite passes); mixed headless/display SDL environment and in-process Xvfb reset issues were rerun with explicit dummy/x11 and process isolation; early browser startup timeout and Vulkan worker-context unavailability are retained alongside passing SwiftShader threaded evidence. Full logs, XML, provenance and screenshots are included. Commands are in commands.txt.

Maintainer acceptance: author-owned merge requested explicitly by Bradley; focused local evidence accepted for this revision with the above omissions. Hosted PR checks are cheap contracts only, not full platform qualification. Full master verification runs asynchronously after merge.
