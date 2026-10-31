# Evidence: shoreline surf

Review evidence for the shoreline surf change. Not for merging.

- `before-after.png`: an island coast rendered by the engine on master and with
  surf (aqua shallows, wet sand, foam wash, the gentler `beach` contour).
- `shoreline-surf.gif`: one full wash period, 24 frames from the
  `TerrainValidation/shoreline surf animation frames around an island` test.
- `bench-cpu-time.txt`: process CPU time on a coast-heavy 64x64 map, 1280x800
  view, software terrain cache, unoptimized macOS dev build, five interleaved
  runs. `oldmargin`/`newmargin` toggle the edge-distance method; `plain`/`surf`
  toggle surf. `cold_ms` is the first draw of the view; `animated_frame_ms` is
  each animated frame afterwards.
