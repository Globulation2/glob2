# Unit motion blur

World units blend the poses covered by a full-frame shutter. The span is estimated
from the current action's delta advance and the current rendering interval.
Sampling wraps within the current action and direction; it does not retain prior
actions or directions. Standing/turning (direction 8), portraits, editor previews,
offscreen indicators, and credits remain sharp.

The effect defaults on. The main General Settings screen's **Motion blur** checkbox
saves with OK and restores the previous value with Cancel. F8 during gameplay
toggles and saves the same setting. Old preferences without the setting default on.

The existing game-speed controls include 0.25x (160 ms/tick), 0.5x (80 ms), and
0.75x (53 ms, approximately 0.755x). Each draws once per tick. Normal remains
40 ms / 25 FPS; existing positive speed IDs and timing are unchanged. No render
interpolation or simulation/save-format changes are introduced.

## Cache

Each Sprite caches final team-colored RGBA composites by team color and the exact
ordered frame/alpha list. Cache hits need one surface draw. Sharp world frames use
one-frame composites in the same cache. On first use, the old source recolor maps
are cleared; subsequent sharp portrait/indicator draws use a single transient
recolor surface instead of retaining a second cache. Original source layers remain.

Entries are generated on demand and retained until Sprite destruction, with no
size limit or eviction. Memory grows with distinct combinations actually used.
Byte counters include CPU pixels and estimated GPU pixel allocation, excluding
container/driver overhead. First-use generation still has a cost; warmed-cache
benchmarks do not measure it.

The composite uses premultiplied accumulation and stores straight RGBA. It preserves
the ordinary alpha-over sequence of the proposed shutter, including overlapping
shadows and changing silhouettes; this is not a physically exact exposure average.
GPU output is within rounding error of repeated draws. Software output avoids the
repeated rounding/background darkening in the legacy integer blit loop.

## Validation and benchmark

From the repository root (graphics checks require a display; use xvfb-run on Linux):

```sh
scons release=1 -j8 build/src/glob2 speed-tests unit-blur-tests unit-blur-benchmark
python3 test/run-game-speed-tests.py
build/src/UnitMotionBlurTest
build/src/UnitCompositeCacheTest
build/src/UnitCompositeCacheGPUCheck
build/src/unit-blur-benchmark gpu
build/src/unit-blur-benchmark
```

The speed harness covers persistence, settings OK/Cancel, the checkbox and F8,
selector offsets, speed bounds, multiplayer/replay restrictions and simulation
checksums. Sprite checks cover all seven actions/eight directions, phase and
team-color separation, cache reuse, and retention of an early entry after exceeding
64 MiB. The benchmark compares sharp draws, repeated blur draws, and cached blur
for 10/300 workers at normal/fast unit speeds; it measures warmed sprite rendering,
not full game frame times.

Native-resolution macOS validation before the HD-renderer rebase (2026-09-09): all checks passed. The three-color GPU
comparison had maximum RGB error 3/255; legacy software comparison mean 5.38/255,
maximum 10/255. Retention test: 13,172 entries and 76,099,904 CPU pixel bytes.
For 300 fast workers, warmed OpenGL timings were 1.05 / 3.46 / 0.80 ms for
sharp / repeated blur / cached blur; software was 0.35 / 1.42 / 0.27 ms.
All measured lookups hit. Across the four benchmark cases, 3,456 composites used
38.1 MiB including GPU pixels, or 19.0 MiB in software. Results are machine-specific.

## High-resolution artwork

When the HD pack is active, final unit composites use its fourfold texture
resolution and mipmap sampling while retaining native logical dimensions.
A shutter falls back entirely to classic layers if any participating HD frame
is missing. Both native and HD intermediate recolor maps are cleared when final
composite caching starts. Changing the artwork pack invalidates cached composites;
otherwise the cache remains unbounded, including when blur is toggled off/on.

Use `build/src/unit-blur-benchmark gpu hd` after installing the HD pack to
measure the same benchmark with HD textures. The native timings and memory
figures above do not describe the larger HD composites.

HD validation on the rebased renderer (2026-09-09): the full HD cache and
game/editor/replay integration checks passed, as did software fallback and
the installed speed/settings suite. Cached versus repeated GPU compositing at
one texture pixel per screen pixel had maximum RGB error 3/255. For 300 fast
workers, warmed HD timings were 1.57 / 5.49 / 1.18 ms for sharp / repeated blur /
cached blur, with no measured misses. The four benchmark cases retained 3,456
HD composites, 1,527,344,640 estimated CPU/GPU pixel bytes (1.42 GiB), including
mipmaps and excluding source textures. First-use generation is not measured.

On the same rebased renderer, the native GPU benchmark measured 1.58 / 5.37 /
1.17 ms for 300 fast workers, with 3,456 composites using 76,584,960 estimated
CPU/GPU bytes (73.0 MiB). Both native and HD runs had zero measured misses.
