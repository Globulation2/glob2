# Breachable Highlands: initial AI playtesting

September 15, 2026. Final generator revision **4**.

## Result and choice

Keep the larger wheat farm, permanent farm lanes, and northwest starting position.
They produced larger colonies and earlier melee contact in every matched game in
this small final sample. All 32 final-batch colonies remained alive at the cap,
versus 31/32 in the matching original games. This is an improvement in development
and interaction, **not a solution to starvation or proof of competitive balance**.

The map now has two wheat plots (north half and southeast quarter), a southwest
wood quarter, and permanent east/west/south sand access. Wheat has the engine's
extra one-in-three growth gate; the 3:1 growing-area allocation compensates for
that difference as a design heuristic. Northern wheat reserves are retained and
a second, independently seeded plot adds growing area and harvesting frontage.
The northwest town position favours wheat access; the wood quarter stays on the
western side too. Stone ridges, home exits, pass widths and dry wooded saddles
retain their original design.

The implementation comments and [design document](../../../map-generators/BREACHABLE_HIGHLANDS.md)
explain the quotas, geometry budgets, seed selection, resource containment and
validation. The generator revision changed; save and network versions did not.

## Matched final comparison

Four 256×256 maps, four colonies, four initial workers per colony, default controls,
no quality-candidate selection. Map seeds **61001, 61002, 61005, 61006**, game seed
**19**, homogeneous Maxima or Nicowar rosters. Each game ran to **65,536 ticks**
(43 minutes 41 seconds at 25 ticks/second). Every game ended at the cap without an
engine winner. Values below are per-game means, summed across all four colonies.

| AI | Version | Surviving units | Buildings | Starvation deaths | First melee sample, ticks |
| --- | --- | ---: | ---: | ---: | ---: |
| Maxima | Original, r1 | 107.50 | 35.00 | 324.25 | 27,136 |
| Maxima | Final, r4 | 185.25 | 42.50 | 351.25 | 19,584 |
| Nicowar | Original, r1 | 210.25 | 76.50 | 50.25 | 18,304 |
| Nicowar | Final, r4 | 279.50 | 79.25 | 74.75 | 16,000 |

The larger colonies also generated more births and deaths. Across each four-game
cohort, starvation deaths divided by births plus the 16 initial units per game
changed from **70.3% to 58.7%** for Maxima and **16.9% to 17.2%** for Nicowar.
These descriptive ratios do not erase the raw death increase. Final critical-hunger
counts also increased. Nicowar's last seed produced fewer buildings despite a larger
population. Food management remains demanding, particularly for Maxima.

Melee damage increased from 32,994 to 50,812 per game for Maxima and from 45,578 to
100,064 for Nicowar. First melee times are sampled at 512-tick intervals, not exact
first-hit timestamps. This supports earlier interaction, but does not establish
that AIs deliberately recognised and opened useful wooded saddles.

See [comparison.json](comparison.json) and the per-team summaries for exact values.
All final maps and replays are in [food-area-games.zip](food-area-games.zip).
[Original games](baseline-games.zip) and [fresh-seed original games](baseline-fresh-games.zip)
provide the matching controls.

## Tuning sequence and limits

There were **36 full-length games**, plus a 16,384-tick pilot and short cross-platform
save continuations. All game execution used `tools/tournaments`, with an SSH worker
on `therig.local`; it ran one pilot slot, then four simulation slots. No AI or
simulation policy was changed.

| Revision | Change | Games | Observation |
| --- | --- | ---: | --- |
| 1 | Original pond-6 layout | 12 | All games reached the cap; Maxima suffered heavy starvation and weak development. |
| 2 | Permanent east/west farm lanes | 8 | Isolated plots correctly, but reduced average development; rejected as a standalone fix. |
| 3 | Also move starts northwest, toward wheat | 8 | Mixed training results; substantially helped Maxima on the new seeds, but Nicowar remained mixed. |
| 4 | Also give wheat three quarters of the farm, move wood southwest | 8 | Larger surviving populations and earlier melee contact in all eight matched games. Kept with food-pressure caveat. |

Seeds 61001–61002 informed tuning. Revision 2 was also checked on 61003–61004.
New seeds 61005–61006 were reserved for the later check; revision 4's parameters
were frozen before their revision-3 results were inspected. All observations are
retained, including the rejected versions. With only four final map seeds and one
game seed, do not infer statistical significance, seat fairness or broad AI balance.
No seat-rotation tournament or human playtest was performed.

The 44-minute cap does not establish late-game resolution. Human review should
specifically check whether the single home entrance creates tower stalemates,
whether breaching an expansion ridge is worth the clearing labour, and whether
larger colonies make farm traffic frustrating. The automatic shortcut regression
still finds a seven-step colony-to-colony saving on seed 1; tactical value can be
larger than that shortest-path saving, but has not been demonstrated by human play.

## Verification and evidence

- Local and Linux generator contracts pass, including a new destructive check:
  remove sand containment, restore valid beaches, retain existing deposits, and
  require rejection of future wood-to-food growth paths.
- Each platform passes all **256 golden-map rows**. The eight new-generator rows
  have matching Linux/macOS fingerprints; existing-generator rows are unchanged.
- Final telemetry on/off verification: **96 cases, zero semantic differences**,
  including serialized worlds and RNG restoration. Timings are diagnostic and were
  gathered under mixed machine load, not a controlled performance comparison.
- **70 final-version maps pass**: 54 Linux maps across 27 boundary configurations,
  plus 16 fresh macOS default maps. No telemetry is missing or truncated. The
  minimum measured local 4×4 anchor count is 321 (overlapping origins, not separate
  buildings). See [validation.json](validation.json) and the
  [full final telemetry and experiment](stress-v4-telemetry.zip).
- Load the identical saved state at tick 16,384 and continue to 17,408 with the
  final Linux and macOS builds: the complete checksum streams match byte-for-byte.
  [Continuity summary](continuity-summary.json) and
  [initial save, checksums and both execution records](continuity-evidence.zip)
  retain the evidence. Windows was not tested.
- Native previews were inspected for the initial layout and a 65,536-tick saved
  game. These minimap previews show terrain/resources, not a full gameplay UI review.

[Initial final-version preview](final-preview.png),
[original Maxima map at the cap](baseline-maxima-65536.png),
[final Maxima map at the cap](final-maxima-65536.png).

Each game archive contains its immutable experiment, accepted execution records,
and the exact final `GLOB2_MEASURE` lines extracted from verified stdout artifacts.
The original/final archives also contain the maps and replays. Rejected-version
archives retain records and final measurements; their full raw logs, saves and
replays remain in the workspace results directories. Do not sum repeated history
records; [analyze.py](analyze.py) reads each exact final gameplay record once.

## Reproduce

Build from the repository root:

```sh
scons release=1 server=0 -j4
build/src/glob2 --generate-map --generator 33 --map-seed 61001 \
  --param width=8 --param height=8 --param teams=4 --candidates 0 \
  --write-map true --output-dir /tmp/highlands-map
build/src/glob2 --run-game --map-file /tmp/highlands-map/map-r0.map \
  --game-seed 19 --player maxima --player maxima --player maxima --player maxima \
  --ticks 65536 --telemetry team-timeline --replay true --save final \
  --output-dir /tmp/highlands-game
```

Use fresh output directories. Structured width/height values are exponents.
Replace all four players with `nicowar` for its mirror. The full experiment JSON
in each archive specifies every job and output request. Register a matching
binary/data bundle, then use the documented `tools.tournaments submit` / `run`
workflow in [distributed tournaments](../../../tournaments.md).

[Build identities](builds.json) record the source base, dirty snapshot identities,
platform, options and binary hashes. Each revision patch is relative to the retained
revision-1 source snapshot; `revision4.patch` describes the final generator changes.
The original source archive, supplied bundles, complete accepted results and logs
are under `artifacts/breachable-highlands/playtest/` in the workspace. Remote build
and worker files are isolated under `/home/bradley/glob2-highlands-20260915/`.
