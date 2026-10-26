# Anthill revision 6 evidence

Review evidence for the Anthill terrain-catalogue rebuild (commit `9575c81eb`, base `e154bda4c`).
Revision 5 maps come from a master build of `e154bda4c`'s Anthill code (unchanged since 2026-10-19);
revision 6 maps from a fast development build (`tools/dev_build.py`, -O0) of the PR commit on
macOS arm64 (Apple clang 21).

## Pictures

- `compare-256-seed7.png`: native previews, seed 7, 256x256, 4 colonies.
- `compare-gameview-queen.png`, `compare-gameview-quarry.png`: in-game renders (`glob2 map render`),
  seed 5, 128x128, 2 colonies. The "before" plain chamber and "after" quarry are the same site.
- `seeds-256-after.png`: seeds 7, 11 and 23 at 256x256 with 4 colonies.
- `before/`, `after/`: every map's preview and its full `--report-file` JSON.

## Room and stone

| seed-size-colonies | queen 4x4 sites r5 | queen 4x4 sites r6 | map 4x4 sites r5 → r6 | buildable tiles r5 → r6 | quarries r6 | queen stone tiles r6 |
|---|---|---|---|---|---|---|
| 5-128-2 | 201–205 | 212–214 | 94 → 103 | 1904 → 1770 | 2 | 5–5 |
| 7-256-4 | 202–207 | 204–216 | 159 → 174 | 7052 → 6569 | 12 | 5–5 |
| 11-256-4 | 204–206 | 209–219 | 181 → 209 | 7342 → 7005 | 11 | 5–5 |
| 23-256-4 | 205–207 | 212–217 | 178 → 200 | 7504 → 7004 | 13 | 5–5 |
| 42-512-8 | 202–208 | 208–216 | 314 → 373 | 28476 → 26590 | 49 | 5–5 |

Queen sites are overlapping 4x4 anchors (the generator's telemetry). Map sites and buildable tiles are the
report's `space` block. Buildable tiles fall mainly because the one-tile strips along tunnels, never wide
enough for a building, are now mixed rock-face tiles.

## Shape sweep

`logs/sweep-revision5.txt` and `logs/sweep-revision6.txt`: `sweep_shapes.sh` (8 shapes x 7 colony
counts x 3 seeds). Both list only 64x64 with 3 or more colonies as refused, identically.

## Tests

`logs/engine-tests-summary.txt`: `glob2-engine-tests -tc="*<anthill>*,editor and lobby defaults
contract*,framework contracts*,stress rejection and registration contracts*,water algae distribution*,
catalog generation contracts*,landscape contracts*"`, 7 cases, 19,544 assertions, all passed.

## Commands

```
glob2 map generate anthill --seed S --width W --height H --teams T --output X.map \
  --preview X.png --preview-scale 4 --report-file X.json
glob2 map render X.map.gz --output gameview.png --render-max-pixels 4096
MapGeneratorGoldenTest <profile> --update --only=anthill
```
