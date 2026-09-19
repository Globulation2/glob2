# The Comb: implementation evidence

See [design, support and limitations](../../map-generators/COMB.md).

- `preview.png`: revision 2 seed 401, 256 square, four colonies, defaults.
- `large.png`: revision 2 seed 102, 512 square, eight colonies, four peninsulas per shore.
- `played-high.png`: revision 1, wheat 300, seed 7, four peninsulas per shore, after 20,000 Cortex ticks.
- `validation-summary.json`: aggregate 4,256-map matrix, paired slider statistics,
  initial access bounds. Raw requests/reports remain in `artifacts/comb/parameter-v2`
  and `artifacts/comb/workers`.
- `game-summary.json`: completed opening-revision and final-revision games, including
  births, harvests, starvation, buildings and combat. Raw logs, initial maps and final
  saves remain in `artifacts/comb/playtest-v4` and `artifacts/comb/playtest-final`.
- `contracts.txt`: complete-save repeatability, telemetry neutrality, save/load,
  unsupported requests, lost-food rejection, extreme-wheat feeding courts, actual
  cross-channel damage, and native worker stone delivery enabling another shot.
- `report-equivalence.json`, `save-equivalence.json`: profiling preserved all 256
  complete reports and all 6 byte-compared saves across shapes, teams and extremes.
- `growth-comparison.txt`: native growth-potential calculation for held-out and
  extreme maps, compared with generator 39 (The Glacis) and Hedgerow Country. This measures
  terrain potential, not realized harvest; open ground behind a sand cap cannot be
  colonized by crops from the sealed farm. Use the nearby sealed-plot values.
- `defaults-check.txt`, `golden-check.txt`: Comb contracts pass; all 456 existing
  macOS golden rows match. Overall suites remain non-green because the concurrent
  Wadi implementation fails its own opening contract and lacks golden rows.

## Revision 2 follow-up

`preview-before-scatter.png` retains the original view. `preview.png` and
`played-scatter.png` show the scattered sand and contained crop pockets before and
after 20,000 Cortex ticks. `scatter-validation.json` records 1,232 passing requests
and both completed 20,000-tick games; `scatter-contracts.txt` records the repeated
mechanism/containment checks. The full reports and saves are under
`artifacts/comb/scatter`. This sweep used the baseline manifest's complete shape and
knob cases, 128 extreme cases sampled with seed 212, the first 512 random cases and
the worker matrix's 128 systematic cases. `scatter-timing.txt` records the current
464 CPU ms/map timing. The larger matrix and optimization comparison below concern
revision 1.

## Reproduction

Run from the repository root; map/save input paths must be absolute.

```sh
scons release=1 server=0 -j 6 build/src/glob2 comb-generator-test
build/src/CombGeneratorTest comb-tests "$PWD" artifacts/comb/contracts

python3 docs/artifacts/comb/study.py prepare "$PWD/artifacts/comb/reproduce"
cp build/src/glob2 artifacts/comb/reproduce/glob2
xargs -0 -n 1 -P 6 /bin/bash -c < artifacts/comb/reproduce/jobs.nul
python3 docs/artifacts/comb/study.py report "$PWD/artifacts/comb/reproduce"
# Repeat with `workers` instead of `prepare` and a different output directory.

build/src/glob2 --generate-map --generator 60 --map-seed 401 \
  --param width=8 --param height=8 --param teams=4 --write-map true \
  --report terrain --output-dir "$PWD/artifacts/comb/replay"
build/src/glob2 --run-game --map-file "$PWD/artifacts/comb/replay/map-r0.map" \
  --game-seed 19 --player cortex --player cortex --player cortex --player cortex \
  --ticks 20000 --telemetry team-timeline --save final \
  --output-dir "$PWD/artifacts/comb/replay-game"

build/src/CombGeneratorTest comb-profile "$PWD" artifacts/comb/profile --benchmark 24
build/src/CombGeneratorTest comb-profile "$PWD" artifacts/comb/profile --benchmark-trace 24
```

The main matrix uses paired seeds 7,29,401,913, shape seeds 7,23,101,401 and random
seed 82931; the worker matrix uses random seed 4158. `study.py` writes every exact
request before native parallel execution. The frozen pre/post binaries and macOS
`sample` call graphs remain in `artifacts/comb/profile`.

## Profiling

512×512, eight colonies, seeds 101–124, release build, macOS ARM64:

| Batch | Before CPU ms/map | After CPU ms/map |
| --- | ---: | ---: |
| First | 438.677 | 395.865 |
| Repeat | 439.086 | 417.283 |
| Mean | 438.882 | 406.574 |

About 7% less CPU time. Post-change telemetry-on batch: 412.565 CPU ms/map.
The machine was shared with other jobs, so wall time is not a fair speedup measure.
Sampling identified start scoring as the largest cost; it remains unchanged.
Optimization caches each shore's 4×4/2×2 footprint masks, reuses resource-distance
fields and bounds local-room floods to 24 steps. Full footprint ownership checks
and both finished-world validation passes remain intact.

## Limits

Ten final-revision AI games total 230,000 ticks; all colonies expanded beyond their
starting swarm. The 40,000-tick Cortex game also shows serious late starvation as
armies grow beyond their feeding capacity. Low-wheat games are slower; odd-team
expansion is asymmetric. These results establish generation reliability, opening
viability and actual combat, not human fun or competitive balance. Cross-platform
output comparison and native-speaker translation review were not performed.

A separate CLI generation-and-save timing at 512×512/eight colonies, seeds 101–103,
used process user+system CPU time: comb 0.510s, coral 0.337s, fjord-continent 0.330s.
Those include CLI startup and serialization and are not the microbenchmark above.

## Merge evidence

`seed401.map.gz` is the revision-2 initial map; `cortex401-final.game.gz` is its
20,000-tick Cortex save. Decompress with `gzip -dk` before loading.
`final401-result.json`, `cortex401-result.json` and `cabino401-result.json` retain
the generated request and both latest playtest results.
