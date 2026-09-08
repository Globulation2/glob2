# ADR 006: Reuse the 2D GPU renderer for the first WebGL2 backend

Status: in progress; browser support remains experimental.

Glob2 already has GPU implementations of its 2D drawing operations. The browser
build now compiles those implementations with the pinned Emscripten SDK's legacy
OpenGL compatibility layer, targeting WebGL2 exclusively. Sprites, fonts,
terrain, overlays and primitives are drawn by the GPU. This does not upload a
software-rendered screen once per frame.

This is a deliberate delivery compromise: preserve the shared drawing interface
and desktop behavior while establishing working GPU rendering and regression
coverage. It is not a bespoke modern shader renderer. The compatibility layer
adds overhead and uses SDK internals during context restoration. SDK upgrades
must run the rendering and recovery suite. A later direct GLES3 implementation
can replace that layer behind the same interface if benchmarks or browser
compatibility require it.

## Ownership and lifecycle

The browser host selects WebGL2 when requested with `?renderer=webgl2` and
available. Software remains the default, and `?renderer=software` explicitly
selects it. Lack of WebGL2 falls back to software.
The page retains only the game canvas. Browser builds use GLES-compatible headers
and flags; native builds retain their existing OpenGL dependencies.

The shared renderer retains CPU surfaces, including sprite atlases, as the
source for texture restoration. Texture names start at zero and cannot be used
before allocation. Atlas coordinates use the atlas's actual normalization,
including the power-of-two texture path used by the browser.

Viewport changes use the existing frame-boundary event. Updating the drawable,
projection, clipping and screen layouts leaves the simulation and camera intact.
One CSS pixel remains one drawing-buffer pixel.

On context loss the browser host suspends application execution through the same
lifecycle transition used for hidden tabs. On restoration it recreates the
compatibility shaders and streaming buffers, then asks the renderer to rebuild
textures and projection from retained CPU state. The screen stack resets its
timing baseline before execution resumes. Game state is not serialized or
reloaded. Context loss does not provide coordinated multiplayer suspension yet.

## Validation and remaining release gates

`browser/tests/rendering.spec.js` exercises real custom-game controls, checks the
actual WebGL2 context and drawing-buffer dimensions, checks software selection,
and loses/restores the real context repeatedly during a match. Multiplayer
correctness fixtures allow extra time for two clients sharing a headless software
GPU, and use explicit screenshots instead of continuous trace readback. These
timeouts do not establish the controlled performance gate. Viewport tests
inspect presented screenshots, because WebGL may clear its drawing buffer after
presentation.

Complete cross-browser single-player and viewport coverage, visual review,
native regression checks and controlled performance baselines are required.
Context loss during settings, the editor and its confirmation dialog is also
covered across all three browser engines, with retained controls verified after
restoration. Context loss during other legacy blocking dialogs/loading, unrecoverable GPU failure
UI, and fallback after an unexpected context-creation failure still need release
qualification. This milestone does not make the full platform stable.


## Performance gate remains open

A six-second local comparison on Apple M3 with Chromium's Metal backend reached
approximately 25 simulation ticks/second for both WebGL2 and software when rerun
without this task's other browser tests. An earlier comparison during concurrent
test activity measured about 18 for WebGL2 and 25 for software. These are sanity
checks, not the controlled release benchmark matrix; they demonstrate why the
reference environment must be controlled. The latest readings are recorded in
`browser/benchmarks/apple-m3-sanity.json`.

An earlier WebGL single-player run exceeded deadlines in editor-load and
startup-cancellation scenarios under headless Chromium. A fresh run on the
current build passes all 22 Chromium single-player, viewport and rendering
scenarios, including those two cases, without changing their deadlines. The
complete cross-browser GPU suite and controlled performance fixtures remain
release gates; software remains the default.

Run the complete existing suite against WebGL using
`GLOB2_TEST_RENDERER=webgl2 npx playwright test` from `browser/`. The dedicated
rendering scenarios always exercise WebGL2, even in the default suite. Renderer
optimization and a passing complete WebGL suite are required before changing the
default. The maintained interfaces and resource recovery added here remain useful
if the SDK compatibility layer needs replacement.

The comparison can be repeated against a locally served build with
`GLOB2_TEST_URL=http://127.0.0.1:8770 GLOB2_ANGLE=metal node benchmarks/rendering.cjs webgl2`
and then `software`, from `browser/`. Omit `GLOB2_ANGLE` to use Chromium's default
hardware backend. This opens a dedicated browser, selects the first custom map
through the menu and samples six seconds after warmup. Record the reported GPU
and eliminate competing workloads for meaningful comparisons; it does not yet
supply fixed-seed small/typical/late-game release fixtures.
