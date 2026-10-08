# Water travelling waves: review evidence

- `open_water_before_after_2x.gif`: open water at 2x over one 96-tick loop. Left: base (16 hash-random variants, 4 phases x 24 ticks). Right: branch (4x4 positional block, 16 phases x 6 ticks). Built from the committed frames tiled exactly as the engine selects variants for uniform cells (uniform cells copy the variant's pixels).
- `archipelago_1to1_crop.png`, `archipelago_overview.png`: `glob2 --render-game maps/Archipelago.map.gz --render-max-pixels 4096` at cf762a286 (macOS arm64, release build), 1:1 crop with shores and a downscaled overview.
- `bench-scope.txt`: SoftwareRenderBenchmark on Archipelago (1470x810, software backend, map mode, whole map, animation advanced every frame by a local-only benchmark patch, 960 frames after 60 warm-up), interleaved 3x: `nomasks` = branch with MaskBudget forced to 0, `masks` = branch. `cache_mean_ns` is the render.terrain_cache telemetry scope per frame.
