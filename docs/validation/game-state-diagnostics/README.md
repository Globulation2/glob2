# Diagnostic execution validation

`results.txt` records a native macOS run from the same saved initial state with
the feature disabled and enabled. `commands.json` retains the exact commands
and environment overrides (replace the absolute checkout prefix to rerun).
`initial.game.gz`, `off.checksums.gz`, and `on.checksums.gz` retain the inputs and
complete per-tick traces; compare the decompressed sidecars byte for byte.
Generation used symmetric-arena, map seed 42, 64x64, two teams, game seed 731.
Each measured continuation runs through tick 3000 in an isolated profile.

The enabled run writes per-tile fields, field PNGs and three periodic snapshots;
reloading the tick-2000 snapshot must reproduce the remaining tick records.
`food-opportunity.png` is one of the exported overlays. The rendering fixture
also exercises success, invalid field dimensions and an unwritable output path
at 150% UI scale, with both software and native OpenGL graphics (`tests.txt`).
It verifies the original surface, dimensions, graphics flags, scale and clip.

```sh
scons release=1 server=0 map-render-test
python3 test/run-savegame-safety-tests.py --check-preferences build/src/MapRenderRegressionTest . artifacts/map-render
python3 test/run-savegame-safety-tests.py --check-preferences build/src/MapRenderRegressionTest . artifacts/map-render-gl --gl
```

Linux/Windows cross-platform checksum and graphics coverage remains outstanding.
