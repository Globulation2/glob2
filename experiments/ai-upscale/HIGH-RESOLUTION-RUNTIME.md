# High-resolution artwork and map zoom experiment

Implemented in `codex/ai-upscale-experiment`, separate from the main checkout.

## Try it

From the experiment checkout:

```sh
scons release=1 -j8 build/src/glob2
build/src/glob2 -g
```

**High-resolution artwork** defaults on and is applied at game/replay/editor session load. Turn it off in General Settings to return to classic artwork on the next load. Explicitly saved preferences are respected. Original artwork remains installed. OpenGL map zoom works independently of this setting.

- Alt + wheel: zoom about the pointer, 50%–300%, including fractional wheel events.
- Bottom-left − / 100% / +: zoom about the map center. Every session starts at 100%.
- Plain wheel keeps its existing building controls.
- F11: toggle fullscreen without recreating the GL context.
- Window resizing retains the configured logical resolution and scales/letterboxes the viewport. It preserves the world center; it does not reflow the sidebar.
- Software mode uses original artwork at 100%; zoom controls display “GL only”.

## Runtime pack

`data/highres/v1` is the standalone pack: 487 frames (73 building/state/wall/flag/shared frames, 272 terrain frames, 65 resource frames and 77 other world frames), 543 layer PNGs, four padded terrain and four resource atlas levels, and two manifests. No model, Python, or generation dependencies are needed by the game. SCons install/dist includes this directory; the macOS bundle already copies the data directory.

`manifest.json` records logical dimensions, 4× scale, recipe, layer roles and dimensions, original/selected-source/output hashes, and atlas metadata. `frames.txt` is the compact runtime index, starting with `GLOB2_HIGHRES 1`; each following row contains:

```text
frame-id logical-width logical-height scale base-file-or-dash team-file-or-dash
```

The runtime validates version, logical size, physical layer dimensions and required layers. Invalid combinations fall back atomically to the original frame. Atlas rejection falls back to all original terrain frames, preserving batching. The exporter/validator verifies hashes; the C++ loader does not perform cryptographic integrity checks.

Pool0b0 and school1b0 retain the selected baseline bytes. Deterministic finishing retains separate base/team layers. Both generated swarm states use team pixels extracted from their own green geometry and neutral base/shadow pixels; they never reuse the original, misaligned mask. Recoloring uses the engine’s existing hue rotation.

Terrain atlas slots have 64 source pixels of extrusion around each 128-pixel tile. Each of the four levels is downsampled per tile before packing and border extrusion. The renderer clamps the maximum mip level to three, sufficient at 50% zoom. All terrain and the other non-unit world artwork use the pack; unit artwork remains original. The generated water material keeps the existing scrolling presentation.

Reproduce and validate the export (requires Pillow, NumPy and the committed selected experiment sources; create a local virtual environment first):

```sh
python3 -m venv .cache/ai-upscale/venv
.cache/ai-upscale/venv/bin/pip install Pillow numpy
.cache/ai-upscale/venv/bin/python experiments/ai-upscale/water_material.py
.cache/ai-upscale/venv/bin/python experiments/ai-upscale/connected_terrain.py
.cache/ai-upscale/venv/bin/python experiments/ai-upscale/export_runtime.py
.cache/ai-upscale/venv/bin/python experiments/ai-upscale/validate_runtime.py
```

## Rendering and camera

Original surfaces remain available for software/CPU operations and logical dimensions. GPU draws select high-resolution surfaces while retaining original destination rectangles, including differing base/team layer sizes. GL uses normalized 2D textures; high-resolution artwork uses linear filtering and mipmaps, while legacy artwork retains its filtering policy.

`MapCamera` supplies fractional world origins, visible bounds, conversions, pointer anchoring and toroidal normalization. Gameplay/replays/editor share this camera and the screen-space zoom controls. The world pass transforms directly into the drawable framebuffer, including on HiDPI displays. Sprite batches flush across transform boundaries; UI renders after restoration. Particles retain world positions. Placement, selection, editor brushes, panning and minimap navigation convert through the camera. Maps smaller than the viewport repeat to fill it; repeated appearances retain one game identity and picking wraps to the underlying tile.

Camera state and artwork preference do not enter simulation orders, replay commands, or save state. The preference itself is stored in local settings. Team recoloring is lazy and session caches are released on closing the game/editor.

## Automated verification

Run from this checkout, with a working display/GL context for the integration harness:

```sh
c++ -std=c++20 -Ilibgag/include test/MapCameraTest.cpp -o /tmp/glob2-camera-tests
/tmp/glob2-camera-tests
scons release=1 -j8 highres-integration-test
build/src/HighResolutionIntegrationHarness
build/src/HighResolutionIntegrationHarness software
```

The integration harness creates an isolated settings profile and replay fixture. It calls real game/editor event handlers directly, bypassing unreliable native mouse automation. It checks placement orders and editor brush tiles at 50/100/200/300%, fractional Alt-wheel order isolation, controls, fullscreen center preservation, replay rendering, small maps, resource release and 50 ticks of matching simulation checksums with original/HD rendering. It writes GL captures and benchmark logs under `experiments/ai-upscale/runtime-check`.

`RuntimePackCheck.cpp` additionally exercises every frame in all 16 hues, checks logical dimensions and GL errors, confirms bounded cache reuse/release, and benchmarks a synthetic dense scene. It supports `original`, `software`, and `fallback` modes. Tested negative packs include missing files/layers, incorrect dimensions, a missing pack and an unsupported version.

The eight focused scroll-wheel tests pass. The clean PR branch based on master passes all 171 CppUnit tests. The original experiment checkout had 195 tests with three failures in additional Maxima placement cases; the unrelated AI commits are excluded from the PR.

## Measurements and review limits

Measurements are from the development Mac, with other processes active; they are smoke/performance comparisons, not stable cross-device benchmarks. The original-art baseline uses the updated renderer, not an independently built historical renderer.

- HiDPI starter gameplay: approximately 41 MB GPU allocation with original artwork and 161 MB with HD artwork. At 50%, HD adds one draw call for the terrain atlas, rather than one call per tile.
- All 89 frames in all 16 hues: approximately 614 MB CPU and 1.19 GB GPU allocation, 880 colored frames. Repeated drawing does not grow the cache; session close releases HD resources. This deliberately maximal case is much larger than normal session use and matters on low-memory hardware.
- Dense four-team map with 64 buildings and 192 units: 50% original/HD approximately 11.3/16.5 ms, 300% approximately 2.0/2.1 ms. HD used about 171 MB GPU and 90 MB CPU, with 10–11 lazily created colored frames. The 50% cost increase warrants checking slower hardware on slower devices.
- Synthetic HD dense scene: about 6.8 ms at 50% (932 calls), 2.3 ms at 300% (34 calls), in the latest run. See integration.log for real map benchmarks, including the dense four-team fixture.

Captures cover all selected assets, all swarm hues, enlarged damaged/construction states, gameplay/replay/editor, HiDPI/fullscreen, small maps, and terrain adjacency. Collect hands-on play feedback and results on other GPU/OS combinations. The PR asks reviewers whether to retain the user-facing classic-artwork switch; it remains available for now. The minimap viewport indicator remains quantized to tiles. Whole-map clipping is applied during the transformed pass; UI clipping is restored afterward.

## Resources and remaining world artwork

All 65 resource frames now use the constrained Real-ESRGAN RGB pass, with exactly preserved bilinear source alpha. A separate padded atlas accommodates their different dimensions and builds mip levels independently for each frame. Terrain coverage expands to all 272 frames; shoreline masks now follow shared corner topology, with compatible RGBA edges at every mip. Water, bullets, explosions, magic and particles use constrained finishing. Fog, clouds and area markings use faithful 4× bilinear resampling because they should retain their soft mask structure. Unit sprites and unit death animations remain with the unit animation follow-up.

Regenerate with `upscale_resources.py` and `upscale_world.py` (both accept `--cache`), followed by `water_material.py`, `connected_terrain.py`, `export_runtime.py`, `validate_runtime.py` and `pr_comparisons.py`. Selected corrected sources and model provenance are retained; inference inputs, raw trials and model binaries are excluded from the runtime pack.

## Expanded pack validation (September 8)

The final 487-frame pack passes all 16 team hues, logical-size and GL checks, bounded cache reuse and session release. Dense-map frame times (HD/classic): 11.77/8.25 ms at 50%, 1.55/1.71 ms at 300%. GPU allocation: 358/45 MB. Draw calls: 24,634/24,623 at 50%, 613/613 at 300%. The full 16-hue stress test uses 856 MB CPU / 1.50 GB GPU, with 944 cached team frames. These replace the smaller-pack measurements above for this revision.

Pixel checks verify that resource and terrain atlas textures actually produce color; GL error checks alone do not catch an incomplete mip chain. The atlas loader defines level zero and subsequent mips at the same exact dimensions, overriding the legacy power-of-two allocation. Repeated-map tests compare rendered building copies against explicit positions, keep one building identity, check identical wrapped picking in multiple visible periods, and advance visual state only on the primary draw.

## Connected terrain construction

Terrain is built as a connected tileset. Shared grass and sand textures are made periodic while retaining their grain. Transition masks follow the engine's four-corner grass/sand/water lookup, with shared boundary irregularity and seeded interior variation. Compatible edge profiles and corner pixels are matched in RGBA at every mip level before padding and packing. This changes the shoreline artwork, while tile IDs, simulation terrain and picking remain unchanged. All 91,136 allowed directed joins across four mip levels are checked against the exported atlas, along with every corner class.

Run `connected_terrain.py` before `export_runtime.py`. The connected-terrain manifest records material and topology source hashes. `validate_runtime.py` reads actual atlas pixels to verify legal neighbors and corner junctions independently of generation. Terrain source alpha is intentionally rebuilt from topology; resource and building alpha policies are unchanged.

Gameplay/replay zoom buttons now occupy the bottom of the right sidebar. Building repair/upgrade/demolish controls and their hit targets move up to reserve the footer. Editor zoom controls remain at the bottom left.
