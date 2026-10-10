# Adaptive zoom detail

How presentation fades detail into overview at different zoom levels. These rules do not change simulation state.

## Adaptive zoom detail

The map zooms from the fitted whole map up to 500% (`MapCamera::MAX_ZOOM`). With the
`adaptiveZoomDetail` graphics setting (default on), map elements change
representation with the zoom instead of scaling uniformly. Presentation only: none of
it is saved, checksummed or read by the simulation.

- `ZoomDetail::forView` (`src/render/ZoomDetail.h`) is the single source of the
  curves. Its input is the size of one tile in screen points
  (`32 * zoom / logicalUnitsPerPoint()`), so thresholds mean the same on a phone, a
  high-density display and a desktop. Every threshold is a named constant there; each
  ramp is a smoothstep between two tile sizes, so representations cross-fade.
  `Game::drawMap` computes it once per frame into `MapRenderState::detail`.
  The map has two looks, the detailed map and the strategic overview. Everything
  that differs between them (terrain, unit and building sprites, flags, zone
  outlines, status pips, the territory wash) cross-fades inside one narrow window,
  from 9 down to 7 points per tile, so each look is undistorted over a range of
  zooms either side of it. Keep new representations on that window.
  A small map cannot zoom out far enough to reach the overview by tile size,
  so the view's owner sets `MapRenderState::minimumZoom` and `ZoomDetail::rampTile`
  remaps the far range: fully zoomed out is the full overview on a map of any size,
  it holds up to 1.5 times that tile size (at most 16 points), the cross-fade above
  it is as narrow as on a large map, and from normal size in nothing changes. A
  map still at 20 points or more per tile when fully zoomed out is left detailed.
  Overlay sizes always follow the true zoom. Disabled,
  it returns the values of uniform scaling and the passes take their original paths.
- `MapOverlayQueue` (`src/render/MapOverlayQueue.h`) holds overlays of constant screen
  size. Passes queue them at a map position while the map transform is active;
  `drawMap` flushes once after the air units (under clouds and fog) and once on
  return (flags), inside one `beginScreenOverlay` scope. Bars therefore draw above
  every unit and building rather than interleaved with them. `Game::anchorBars` names
  the map point a bar keeps fixed while its size changes.
- Bars hold their 100% size from 48 down to 20 points per tile and change slowly
  outside that. Between 20 and 16 points every bar fades together. From there down
  to the overview a status pip marks what needs attention (a starving unit or one at
  60% health or less; a damaged building, one with under half its workers, an inn
  without food, a tower without ammunition).
- Zones cross-fade from pattern sprites with an outline to a flat translucent tint
  without one: an area keeps its shape at any scale where a one-pixel line cannot.
  `GraphicContext::drawMapFill` snaps fill edges to target pixels so translucent
  neighbours tile without seams. The fog-of-war shade draws one fill per horizontal
  run of whole squares at one fade level with `drawMapTileFill` and its edge sprites
  with `drawMapTileSprite` (see [Smooth fog of war](rendering.md#smooth-fog-of-war)). Both snap to pixels only in the software rasteriser, where
  truncated coordinates otherwise leave one-pixel gaps between tiles; accelerated
  renderers place sprites at exact fractions, and a snapped fill beside them
  leaves hairline seams.
  The outline stroke stops thickening at two points.
- In the cross-fade `Game::drawMapOverview` fades in terrain palette colours sampled
  from the detailed compositor's material coverage of each cell's corners. Four samples per tile axis keep coastlines aligned
  during the fade; resource minimap colours tint their gameplay cells over that
  ground. In the overview this replaces the water, terrain and resource passes.
  The reusable image stretches over the map in a single draw, avoiding thousands
  of translucent fills.
- Units cross-fade to team-coloured markers (dot worker, triangle warrior, diamond
  explorer). Bullets, explosions, death animations, the magic effect and the
  level-up number go with the unit sprites. Building sprites cross-fade to chips in the
  team's colour carrying a white icon of the building's purpose, with a pip per
  upgrade level and a paler chip for a construction site; flags become discs of
  constant size; walls become plain team-coloured tiles. Chips are 15 to 26 points
  and placed in priority order (damaged, flags, towers, hives, the rest); one that a
  placed chip would cover by more than 15% is left out. The icons are
  generated from `data/gfx/mapicon*.png` sources and loaded as WebP in the runtime
  tree. The sources are rasterised at seven pixel sizes from the SVGs in
  `datasrc/icons/map/` by `python3 tools/icons/export_map_icons.py` (needs
  `rsvg-convert`); the renderer draws the largest frame that fits, pixel for pixel.
  Frame order is shared between that script and `MapOverlayQueue.cpp`.
- In the overview `Game::drawMapTerritory` washes the land around each team's
  visible buildings in its colour, inside a solid border two points wide. The
  team colour is brought up to a common brightness first, so a dark colour reads
  as well as a light one. An under-attack event raises the same pulsing mark as a
  player's ping.
- The torus view draws its map texture through the same map transform at the
  camera's zoom (`TorusView::draw`), so it shows the same detail, overlay sizes and
  overview as the 2D view at that zoom. Its tiled atlas chooses terrain sampling
  density from the complete map capture, with enough room for every terrain page
  and its renderer allocation to remain resident. Large captures use filtered
  native terrain pages and a matching reduced atlas; this reduces fine texture
  detail when magnified, while preserving contour coverage and the shared zoom
  detail transitions. The atlas uses the device texture/viewport limits, splitting
  only when necessary. Narrow edge tiles reuse the same sampling density and warm
  pages. Streaming fallback follows the same density choice.
  Overview palette samples are cached in canonical CPU pages independently of
  zoom and animation. Terrain vertices (including the contour halo), the terrain
  seed, registry/asset replacement and resource visibility invalidate those pages;
  camera movement and blend opacity do not resample unchanged contours.

When tuning, capture the same save across zooms with `SoftwareRenderBenchmark`
(`PROFILE_ZOOM`, `PROFILE_CAPTURE`); `PROFILE_ADAPTIVE_ZOOM=0` draws uniform scaling
from the same build for a before/after pair. Set it explicitly on every run: the
benchmark saves preferences, so the last value otherwise carries into the next run.
`torus-render-benchmark` takes `GLOB2_BENCH_ZOOM`, `GLOB2_BENCH_PAN_X`/`_Y` (the
camera's top-left tile), `GLOB2_BENCH_AREAS=1` (zones), `GLOB2_BENCH_FOG=1`
(with `GLOB2_BENCH_SMOOTH_FOG=0|1` for the fade),
`GLOB2_BENCH_FRACTION` (a camera offset in map pixels, which seams need to show) and
`GLOB2_BENCH_ADAPTIVE_ZOOM=0|1` for the same comparison on OpenGL. Buildings,
including custom swarm artwork, fade out as their icon chips fade in. Unit sprites
stay opaque while markers fade in over them. Check draw calls as well as time
when changing a cross-fade, since a translucent sprite can leave its batch.


See [software rendering](software-rendering.md).
