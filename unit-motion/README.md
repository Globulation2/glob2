# Smooth unit motion (follow-up PR)

- `parity-off-vs-536.txt`: with the setting off (default), the same 30
  SoftwareRenderBenchmark captures as `../parity/` are byte-identical to the #536
  branch (`interp/` hashes in `parity-off-sha256.txt`, #536 hashes are the `head/`
  entries in `../parity/sha256.txt`). This includes the commit that moves terrain,
  resources, fog and overlays off the live Team mask.
- `unit-motion-slow-compare.webp`: threaded windowed games recorded with `-vs`
  (videoshots), setting off (left) vs on (right), slowest speed preset (160 ms ticks)
  so several frames fall in each tick; clouds, cloud shadows and building particles
  off so only units and the area-marker animation move; 1024x768, seed 5, 60 ticks.
  The two runs are not frame-aligned. Capture slows drawing to about 30 fps.
