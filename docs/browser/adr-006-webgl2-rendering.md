# ADR 006: reuse the 2D GPU renderer for WebGL2

Status: accepted.

Glob2 already has GPU implementations of its 2D drawing operations. The browser
build compiles those implementations with the pinned Emscripten SDK's OpenGL
compatibility layer and targets WebGL2. Sprites, fonts, terrain, overlays, and
primitives are drawn by the GPU rather than uploading a software-rendered frame.

This preserves the shared drawing interface and desktop behavior. The
compatibility layer uses SDK internals during context restoration, so SDK upgrades
must run the rendering and recovery suite. A direct GLES3 implementation could
replace it behind the same interface if browser compatibility or measured
performance requires that later.

## Ownership and lifecycle

The browser host uses WebGL2 by default when the browser provides a
hardware-accelerated context, and software rendering otherwise. The host probes a
scratch canvas: a context the browser flags with a major performance caveat, or
one drawn by a CPU rasterizer such as SwiftShader or llvmpipe, counts as
unavailable, because emulated WebGL2 costs several CPU cores and drops frames
where the software renderer does not. `?renderer=webgl2` forces WebGL2 even when
emulated, and `?renderer=software` selects software. Failure to create WebGL2
falls back to software. Native builds retain their existing OpenGL dependencies.

The renderer keeps CPU surfaces, including sprite atlases, as texture-restoration
sources. Texture names begin at zero and cannot be used before allocation. Atlas
coordinates use the atlas's actual normalization, including power-of-two textures.

Viewport changes update the drawable, projection, clipping, and screen layouts at
a frame boundary without replacing the context. One CSS pixel maps to one drawing
buffer pixel.

On context loss, the browser host suspends application execution. Restoration
recreates compatibility shaders and streaming buffers, then asks the renderer to
rebuild textures and projection from retained CPU state. The stack resets its
timing baseline before execution resumes; game state is retained in memory.

## Validation

`browser/tests/rendering.spec.js` drives real controls, verifies the actual WebGL2
context and drawing-buffer dimensions, exercises software selection, and loses
and restores the context repeatedly during a match. It also verifies retained
settings, editor, and confirmation controls after restoration. Playwright runs
across Chromium, Firefox, and WebKit, and a local run can select WebGL2 for all
applicable scenarios with:

```sh
GLOB2_TEST_RENDERER=webgl2 npx playwright test
```

Viewport tests inspect presented screenshots because WebGL may clear its drawing
buffer after presentation. Multiplayer tests disable continuous trace readback
and capture explicit screenshots so two clients can share a headless GPU without
changing simulation behavior.

## Performance measurement

`browser/benchmarks/rendering.cjs` compares WebGL2 and software rendering against
a locally served build. Results depend on browser, GPU, driver, and concurrent
load, so machine-specific output stays outside the repository.

From `browser/`, run:

```sh
GLOB2_TEST_URL=http://127.0.0.1:8770 GLOB2_ANGLE=metal node benchmarks/rendering.cjs webgl2
GLOB2_TEST_URL=http://127.0.0.1:8770 GLOB2_ANGLE=metal node benchmarks/rendering.cjs software
```

Omit `GLOB2_ANGLE` to use Chromium's default hardware backend. The benchmark
starts a custom map and samples six seconds after warmup; it is a local comparison,
not a portable release threshold.
