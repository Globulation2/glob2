# Savannah light playtest and tune

The accepted tune enlarges the *contained growth space* around home wheat and wood,
while keeping their initial 40/28 deposits at default abundance and their western
sand edges where nearby inns could be built. It leaves the home reservation and the
two five-tile town approaches unchanged. The candidate maps retain two neutral ponds
on each tested four-colony seed.

## Matched games on macOS arm64

Each row is one 128x128, four-colony, 24,000-tick game with map seed 1 or 2 and game
seed 23. Seed 1 seats Nicowar/Numbi/Maxima/Nicowar; seed 2 seats
Numbi/Maxima/Nicowar/Nicowar. Baseline and final use the same seed, seats and tick
cap, although changed map geometry makes combat paths and AI decisions differ.
Counts come from production `GLOB2_MEASURE` final records and production map reports.

| Map seed | Version | Wheat tiles initial→final | Wood tiles initial→final | Starvation deaths | Combat damage |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Baseline | 236→66 | 168→82 | 10 | 15,644 |
| 1 | Final | 236→80 | 168→63 | 7 | 11,614 |
| 2 | Baseline | 236→93 | 182→33 | 23 | 18,087 |
| 2 | Final | 236→104 | 182→45 | 12 | 14,784 |

Numbi harvested wheat by tick 4,096 on both final maps (12 tiles each) and retained
food buildings. All final games reached combat. These two seeds support the modest
extra crop room; they do not prove a general survival or win-rate improvement.
Wood still declined substantially on both. Starvation still occurred, notably for
Maxima, and damage differs because armies met differently after the terrain change.

The rejected westward crop shift planted 50 initial wheat tiles per home, but
Numbi harvested **zero wheat** on both seeds by tick 12,288 and began starving at
tick 5,120, before its first damage dealt. Its early food-inn placement needs
buildable grass near wheat. A subsequent wheat-only growth-room tune restored
Numbi harvest but left global wood at seven/zero tiles by tick 24,000. The final
version also enlarges contained wood room while keeping its western margin fixed.
`analysis.json` retains per-team births, deaths, damage, first starvation/contact
and early wheat harvest for every candidate; no failed game was discarded.

## Separate Linux game and platform checks

`pharaoh-dev-3.local` had the lowest observed load among the offered hosts. An
isolated Ubuntu x86_64 optimized build ran the final candidate's macOS-generated
128x128 four-colony map seed 3005 for 24,000 ticks (game seed 23;
Maxima/Nicowar/Numbi/Maxima). It completed with 10,323 damage dealt, one eliminated
Maxima seat, and 37 starvation deaths (24 and 11 for the two Maxima seats, one each
for Nicowar and Numbi). Wheat remained 111/236 initial tiles and wood 120/182.
The final save and replay are retained locally. The macOS CLI loaded and reported
the Linux saved game successfully.

After an incremental rebuild with the final Savannah source, Linux also generated
map seed 3005 successfully. Linux and macOS generated maps from that seed have
similar quality (fairness .9555 and .9548) but **different tile layouts**. The
cause of that layout difference was not isolated in this light round; the golden
fixture records rows by platform. This round checked that a *saved Linux map* loaded
on macOS with identical terrain, resources, space, fertility, movement and starts.
Preview rendering differed at 119 of 65,536 pixels, and recomputed quality numbers
varied only at floating-point roundoff. It did not compare per-tick simulation
checksums or run Windows. No simulation behavior was changed by this tune.

## Focused structural checks

The final optimized macOS build, defaults harness (including 4,096 unattended
resource-growth calls), and Savannah shard sweep (46/46, zero failures) passed.
The golden update required `--force` because eight unmerged Savannah revision-1
fixture rows were generated before this tune; only those eight rows changed.
Existing generator fixture rows remained unchanged. Save/reload and full training/
held-out matrices from the initial implementation are in the parent artifact tree;
this tuning round adds the matched games and focused structural checks above.

## Reproduction and files

From the repo root, build with `scons release=1 server=0 -j2 build/src/glob2
map-generator-defaults-test map-generator-golden-test`. Generate a default candidate
with `build/src/glob2 --generate-map savannah --seed 1 --width 128 --height 128
--teams 4 --output /abs/path/map.map --json /abs/path/report.json`.
Run a matched game with:

```sh
build/src/glob2 --run-game --map-file /abs/path/map.map --game-seed 23 \
  --player nicowar --player numbi --player maxima --player nicowar \
  --ticks 24000 --save initial --save every:8000 --save final --replay true \
  --telemetry team-timeline --output-dir /abs/path/game-output
```

For seed 2, use `--seed 2` and the seat order above. The held-out Linux game used
the same command with seed 3005 and its recorded seat order. `request.json`
records all case settings. `candidate3-*` files are the final tuned maps and games;
`local-*` files are the matched baseline. Reports, previews, saves, replays,
`GLOB2_MEASURE` logs, build/check logs and the analysis script remain alongside this
summary. Human play still needs to judge whether ponds attract conflict and the
plains permit useful flanking.
