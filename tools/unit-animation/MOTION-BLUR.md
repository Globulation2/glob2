# Unit motion blur

World units blend the poses covered by a full-frame shutter. The span is estimated
from the current action's delta advance and the current rendering interval.
Sampling wraps within the current action and direction; it does not retain prior
actions or directions. Standing/turning (direction 8), portraits, editor previews,
offscreen indicators, and credits remain sharp.

The effect defaults off. The main General Settings screen's **Motion blur** checkbox
saves with OK and restores the previous value with Cancel. F8 during gameplay
toggles and saves the same setting. Old preferences without the setting default off. Explicit saved choices are preserved.

The existing game-speed controls include 0.25x (160 ms/tick), 0.5x (80 ms), and
0.75x (53 ms, approximately 0.755x). Each draws once per tick. Normal remains
40 ms / 25 FPS; existing positive speed IDs and timing are unchanged. No render
interpolation or simulation/save-format changes are introduced.

## Rendering: GPU shader, no composite cache

There is no composite/final-image cache. A sharp draw and each pose of a blurred
shutter are ordinary `GraphicContext::drawSprite` calls -- one call per pose,
exactly the sequence Giszmo's review sketch describes.

On GPU with a working shader (`GraphicContext::hasUnitShader()`), each pose is one
textured quad: a GLSL 1.20 program samples the base layer and the *unrotated* team
layer, reproduces the CPU HSV hue shift in the fragment shader, composites team
over base in premultiplied space (matching the algebra the old CPU composite used),
and applies the pose's shutter weight to alpha only. No team-coloured or composite
texture is ever created on this path -- `Sprite::getTeamColorCacheBytes()` stays
zero for the whole game. The shader is created with the GL context
(`GraphicContext::createUnitShader`, called from `setRes`) and destroyed before it
is torn down. A compile/link failure logs once and disables it for that context's
lifetime; set `GLOB2_DISABLE_UNIT_SHADER=1` to force the same fallback for testing.

Without a working shader (software renderer, or the fallback above), each layer is
still one CPU HSV recolor (`Sprite::getColoredSurface`), but the unit sprite is
marked `dynamicTeamColor` and backs its recolored layers with a single sprite-wide,
byte-accounted **64 MiB LRU** (`Sprite::teamColorList`/`teamColorIndex`), keyed by
source frame, resolution (native/HD), and team color -- not by the composited
result. Least-recently-used entries are evicted before a new one is admitted; at
most one active, oversized single entry can push the total over the cap, and only
until a distinct entry next needs the room. This cache is shared by world units and
every unit UI preview (portraits, editor previews, indicators, credits), since they
all reach the same `DrawableSurface::drawSprite`. Other sprite types (terrain,
buildings, resources) are unaffected and keep their existing unbounded per-frame
`rotationMap`.

A shutter samples one resolution throughout via `Sprite::blockHasCompleteHD`: if any
frame in the current action/direction's 32-phase block lacks its HD counterpart,
the whole block falls back to native, rather than mixing resolutions pose to pose.
HD only changes texture sampling density; the destination quad is always drawn at
the sprite's logical (native) size.

## Validation and benchmarks

From the repository root (graphics checks require a display; use xvfb-run on Linux):

```sh
scons release=1 -j8 build/src/glob2 speed-tests unit-blur-tests unit-hd-cache-test unit-blur-benchmark twelve-team-benchmark
python3 test/run-game-speed-tests.py
build/src/UnitMotionBlurTest
build/src/UnitTeamShaderTest
build/src/UnitTeamColorCacheTest
build/src/UnitTeamColorCacheTest software
build/src/UnitHighResolutionCacheTest
build/src/UnitHighResolutionCacheTest software
build/src/unit-blur-benchmark gpu
build/src/unit-blur-benchmark gpu fallback
build/src/unit-blur-benchmark            # software
build/src/twelve-team-benchmark off 5000 river
build/src/twelve-team-benchmark on  5000 river
build/src/twelve-team-benchmark off 5000 craters
build/src/twelve-team-benchmark on  5000 craters
```

`UnitMotionBlurTest` covers shutter frame bounds, loop wrapping, alpha, and the
one-pose identity (no GL, no display needed).

`UnitTeamShaderTest` (GPU) compares the shader's HSV hue shift against an
independent CPU reference over all 1,792 poses (native and HD, three fixed team
colors) and over the 12 in-game team hues plus the existing 16-hue test palette
(one representative pose per action/direction); then compares a sharp draw and a
30-delta motion-blur sequence's rendered framebuffer against a from-scratch CPU
composite (premultiplied accumulate, unpremultiply once, blend poses in shutter
order -- the removed cache's algebra, just not cached). Measured on the reference
machine: HSV hue shift mean 0.059/255, max 1/255, zero alpha mismatches, over
10,074,752 channel comparisons; sharp/motion-blur framebuffer max error 2.27/255,
over 4,194,304 channel comparisons. Both are within the required 2/255 and 3/255
bounds.

`UnitTeamColorCacheTest` proves GPU rendering (sharp and blurred) creates zero
team-colored cache entries; with the shader forced off
(`GLOB2_DISABLE_UNIT_SHADER=1`, also exercised by the `software` build), it drives
20,000+ distinct team colors through the cache and asserts the byte total never
exceeds 64 MiB, that repeated draws of the same key are hits (no new entry), and
that a long-evicted color regenerates correctly. Measured on the reference machine:
the bounded cache settled at 3,028 entries / ~64.0 MiB on the GPU-fallback path and
11,618 entries / ~64.0 MiB on the software path (native surfaces are smaller, so
more fit).

`UnitHighResolutionCacheTest` covers all 1,792 layer mappings, HD/native switching,
zoom, and a synthetic corrupted-HD-install case confirming `blockHasCompleteHD`
forces a whole 32-phase block back to native rather than mixing resolutions.

`unit-blur-benchmark` reports warmed sharp/blur sprite-only timings for 10 and 300
units at two speeds, plus the fallback cache's byte usage when relevant. These are
sprite-only timings, excluding first-use texture uploads and every other draw call
a real frame makes.

## 12-team benchmark

`twelve-team-benchmark <blur:on|off> [ticks=5000] [river|craters]` is a **synthetic
render-loop soak test**, not a full AI-driven match: it does not generate a real
river/crater-lakes map or run Cortex AI. It reproduces the mechanism the composite
cache regressed on exactly -- 12 teams, 156 units (matching the ~157-unit baseline),
continuously varying poses and positions, drawn every tick through the real
`GraphicContext::drawSprite` / `drawUnitMotionBlur` path with a full-viewport redraw
("full-map revealed drawing") -- for as many ticks as asked. Unit positions evolve
from a fixed, map-name-derived seed via a tiny deterministic step that rendering
never touches, so a same-tick checksum match between blur variants is a structural
property of this harness (render and simulation are separate steps), not an
emergent one; it demonstrates the same decoupling the real engine relies on rather
than substituting for it. Every 250 ticks it reports units, the bounded cache's
entries/bytes, GPU-allocated texture bytes, process RSS, frame-time p50/p95/max
over that window, simulation time, and a checksum of the synthetic unit state.

Measured on the reference machine (Apple M3, GPU/shader path, 5,000 ticks, 156
units across 12 teams, both generated-map seeds):

| Map | Blur | RSS @2,500 | RSS @5,000 | Growth | Frame p50 | Frame p95 | 5,000 ticks |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| river | off | 172,785,664 B | 172,785,664 B | 0 B | 0.74 ms | 0.83 ms | 3.96 s |
| river | on | 173,326,336 B | 173,244,416 B | -81,920 B | 1.65 ms | 1.73 ms | 8.25 s |
| craters | off | 172,589,056 B | 172,589,056 B | 0 B | 0.74 ms | 0.84 ms | 3.85 s |
| craters | on | 173,113,344 B | 173,064,192 B | -49,152 B | 1.66 ms | 1.70 ms | 8.33 s |

`team_color_cache_bytes` and `gpu_allocated_bytes` (42,991,616 B, the base unit
texture atlas, loaded once) were exactly flat across every tick in all four runs.
Checksums matched exactly between the `on`/`off` runs of the same map at every
250-tick mark. All four runs meet every requirement: zero GPU dynamic team/
composite bytes, footprint growth far under the 256 MiB budget (measurably zero or
negative -- ordinary allocator/RSS noise, not growth), identical checksums between
blur variants, and p95 far under the 40 ms / 50 ms targets.

Preserved as the failing baseline this replaces: **blur-off reached 4.0 GiB of
composite storage and a 5.79 GiB peak footprint after 5,000 ticks with 157 units**
under the removed cache.

## High-resolution artwork

When the HD pack is active, unit draws sample its fourfold texture resolution and
mipmap chain while retaining native logical dimensions. A shutter falls back
entirely to native layers if any participating frame in its 32-phase block is
missing an HD counterpart (`Sprite::blockHasCompleteHD`). Toggling
`Sprite::setHighResolution` clears the bounded team-color cache along with the
HD layer arrays.
