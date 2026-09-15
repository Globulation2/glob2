# Vultures: initial AI playtest and field tuning — 2026-09-15

[Design contract](../../map-generators/VULTURES.md) · [Accepted games, maps, saves, telemetry and checksums](https://github.com/Globulation2/glob2/blob/evidence/vultures/docs/artifacts/vultures/playtest-evidence.zip)

This is a light headless AI playtest of generator 33, revision 1. It checks whether colonies
can grow, find each other, fight, and experience a finite-food deadline. It is not a human
assessment of fun or a fairness tournament. The archive is 57 MB because it retains both
platforms' complete checksum traces from a 31,746-tick match.

## Settings and retained evidence

The optimized Linux x86_64 client was built from an isolated snapshot of the current source,
using `scons -j8 release=1 server=0 build/src/glob2`. Its bundle ID is
`27e7c8d3e5e8a1025c694f8fabfda17a65a0eeba25ab81376b11b7e815476ac6`.
`source-identity.json`, `tracked-source.diff`, the catalog and the immutable bundle manifest
in the archive identify the tested binary and data. Later edits to code comments and these
notes do not change the compiled generator. The accepted results and compressed `stdout.log`
artifacts can be read with `tools.tournaments.results.Results` after extracting the archive;
the original experiments also remain under `artifacts/vultures-playtest/` in this workspace.

The baseline generated 128×128 maps at seeds 7 and 11 with two colonies, four starting workers,
default controls, `candidates=0`, and two verified team rotations. Games used game seed 19 and a
32,000-tick cap. Four Nicowar mirrors, one Cortex mirror and one Maxima mirror ran with
`team-timeline` gameplay/AI/performance telemetry and final saves. `devlaptop.local` used two
worker slots and `pharaoh-dev-1.local` one; both were idle enough at the preflight check.
All eight baseline jobs, four coverage-probe jobs and three second-seed jobs completed and were
accepted; no generator, worker or artifact failures were recorded. The pilot did not use
`therig.local` or the other Pharaoh hosts.

The tested 125% Wheat amount setting keeps the same one-harvest, zero-fertility policy. It
changes the effective field threshold from 35% to 44% and initial wheat from 3,347 to 4,182
rations in both sampled Linux maps. The control already exists; no simulation or AI rule changed.

## Game observations

Nicowar first produced a warrior by tick 1,024 on all four baseline games. Its first recorded
combat occurred at ticks 7,168 and 10,752 on seed 7, and 7,680 and 6,656 on seed 11. The table
shows end-tick warrior counts and engine results. A cap means both colonies were alive at tick
32,000; it is an unresolved game, not a draw decided by the engine.

| Wheat amount | Map seed / rotation | Engine result | Warriors, teams 0 / 1 | Starvation deaths, teams 0 / 1 |
| --- | --- | --- | ---: | ---: |
| 100% | 7 / 0 | Cap at 32,000 | 61 / 14 | 4 / 19 |
| 100% | 7 / 1 | Cap at 32,000 | 59 / 15 | 2 / 12 |
| 100% | 11 / 0 | Team 0 won at 27,010 | 16 / 3 | 0 / 9 |
| 100% | 11 / 1 | Cap at 32,000 | 12 / 59 | 6 / 3 |
| 125% | 7 / 0 | Team 0 won at 28,994 | 51 / 2 | 2 / 11 |
| 125% | 7 / 1 | Cap at 32,000 | 29 / 69 | 9 / 2 |
| 125% | 11 / 0 | Cap at 32,000 | 46 / 41 | 5 / 16 |
| 125% | 11 / 1 | Cap at 32,000 | 59 / 28 | 3 / 9 |

Baseline seed-7 Nicowar sides harvested 715–985 wheat loads by the cap, while both sides still
had hungry units. The seed-11 win came after 604 and 602 loads respectively, leaving much of
the map's initial wheat unstripped. Food pressure is therefore local: distance, access to a
field edge, hauling, and inn capacity matter alongside total stock. A finite deposit does not
promise that every ration is reachable before a military decision. The 125% setting changed
starvation and match duration in both directions across the paired games. It gave no dependable
improvement on this small sample, so the default remains **100% Wheat amount / 35% cover**.
This retains the requested urgency; 125% remains an available fuller-field variant.

The Cortex baseline built 20 and 17 buildings, trained 13 and 3 warriors, and first recorded
combat at tick 30,208. The Maxima baseline and 125% probe each ended at four workers and three
buildings per side, with no warriors. Maxima harvested only about the 48 starting rations, so
extra exterior wheat did not fix its stall. These AIs should not be used as evidence that the
map always reaches a timely military resolution. A human player still needs to judge the
pressure and whether the home/field transition feels right.

## Compatibility check

Independently generating seed 7 on macOS ARM64 and Linux x86_64 produced 3,337 and 3,347
initial wheat rations respectively, so same-seed **map generation is not byte-identical across
these platforms**. Old Growth also produced different same-seed maps on the two platforms in
the control probe; this observation does not isolate a Vultures-specific cause. The game check
therefore copied one macOS-generated serialized map (SHA-256
`4f428b80a8ad0f2bc397fffd641b23c676d719b21416238d573b99f83a533687`) to Linux.

Both platforms loaded that **same map file**, game seed 19 and Nicowar mirror players. Their
4,096-tick checksum files matched byte for byte (SHA-256
`fbbb88b2707b3143a64bc57cfa81cb2690517c51a78cb7d573a3b51c50b7beae`). A full run
ended on both platforms at tick **31,746**, with team 0 winning and identical final units,
warriors and buildings. Both complete per-tick checksum files decompress to SHA-256
`005f4fa8526547298648e045f95ec64ddc2e585c7afc5d9ed59fcd8c02e0d85e`. The two
complete `.zst` files, early files, result JSON and map are in the archive. This verifies that
particular serialized initial map and AI game through its ending, not every seed or Windows.

## Reproduce and inspect

From the repository root, a production client can regenerate the sampled default map:

```sh
build/src/glob2 --generate-map --generator 33 --map-seed 7 \
  --param width=7 --param height=7 --param teams=2 --candidates 0 \
  --write-map true --output-dir /tmp/vultures-seed7
build/src/glob2 --run-game --map-file /tmp/vultures-seed7/map-r0.map \
  --game-seed 19 --player nicowar --player nicowar --ticks 32000 \
  --save final --telemetry team-timeline --output-dir /tmp/vultures-game
```

The pinned experiment JSON files, host list, result JSON, compressed stdout logs, map files,
final saves and checksum files in the archive specify the actual run more exactly. Extracting
the archive and using `Results('baseline')`, `Results('wheat-125')`, and
`Results('wheat-125-holdout')` reads accepted jobs in manifest order and checks artifact hashes.
For the cross-platform trace, decompress each `.zst` member and compare the decoded SHA-256;
`compat-summary.json` records both compressed-file hashes and the map hash.

## Limits for review

The sample is deliberately small and AI-specific. One baseline military win and several large
armies establish a playable opening and contact, but not broad fairness or enjoyable human
pacing. The 125% probe was tuned on seed 7 and checked on seed 11; two seeds do not estimate a
failure rate. The same-seed generation difference remains a platform limit even though the
shared serialized-map game matched. The earlier macOS save-continuation mismatch documented in
[the first evidence](README.md) remains unresolved and was not retested here. A maintainer
playing the map should decide whether the deadline is satisfying and whether the field layouts
offer enough useful choices.
