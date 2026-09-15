# Vultures verification — 2026-09-15

[Design and tuning rationale](../../map-generators/VULTURES.md) ·
[256×256 preview](vultures-7-large.png) · [128×128 duel](duel-7-large.png) ·
[Seven-colony rectangle](rectangular-7-large.png) ·
[After the Nicowar match](nicowar-final.png) · [Playable maps and evidence](evidence.zip) ·
[Initial Linux AI playtest and field tuning](PLAYTEST.md) ·
[Final bulk generation and tuning](BULK.md) ·
[All 33 locale translations](translations-check.json)

This original package exercised generator **33**, revision **1**, using the optimized
macOS ARM64 client. The final revision **2** adds the opt-in crowded-lattice fallback;
its already playable macOS golden fingerprints are unchanged. The final 461-job Linux
bulk results and revision-2 source identity are in [BULK.md](BULK.md).
[Build/source identity](build-manifest.json) and [reproduction commands](reproduce.sh) identify
what was exercised. No simulation, AI, save-format, replay or network-version code was changed.

## Initial revision-1 verification results

- **Build:** optimized client and both generator harnesses passed.
- **Contracts:** final defaults harness passed, including all four retained compact-map failures.
- **Golden comparison:** 256/256 final macOS rows matched; only the eight new Vultures rows
  were added to the committed table.
- **Telemetry:** 96/96 cases generated; zero semantic failures. Enabled/disabled collection
  produced identical serialized worlds, restored the gameplay RNG, and repeated telemetry
  records exactly, with no dropped/invalid observations.
- **Formatting:** touched generator files and tests pass clang-format, and `git diff --check`
  passes. The whole-tree check reports existing formatting violations in nine untouched files;
  [their byte identity with HEAD](preexisting-formatting.json) is recorded. Those files were
  left unchanged.

For Vultures seeds 1–3, generation milliseconds (off/on) were 635.862/310.458,
547.700/540.256 and 504.415/503.687. Across all generators the totals were 60,511.359 ms off
and 57,424.696 ms on. These measurements were taken on a shared, busy machine and vary with
cache/scheduling; they do not establish a speedup. The byte/RNG comparisons are deterministic
checks, whereas these timings are observations. Raw results are in `tests/telemetry.log` in
the archive.

The archive contains the playable `.map` files; generation requests/reports; all final matrix
reports and their command manifests; test logs; the Nicowar winning replay/final save; Maxima
results; and the save-continuation checkpoint plus first differing checksum records. Uncompressed
working copies and larger diagnostic traces remain under `artifacts/vultures/`.

## Generation and resource contracts

- The defaults harness passed the existing framework/UI/control tests and the new stock-cap,
  disconnected-patch, finished-terrain-mask and starting-access checks. The retained compact
  rectangle seeds 201–204 generate successfully.
- The depletion test harvested every other wheat tile, then ran 2,048 real engine resource-growth
  passes. Consumed wheat stayed consumed and remaining wheat stayed at one harvest per tile.
- The focused Vultures sweep passed **46/46** seeds: 128×128 with 2/4 colonies; 256×256 with
  2/3/4/6/8/12 colonies; 512×512 with 4/12 colonies. Every accepted cell succeeded on every seed.
- The golden update compared existing rows before writing and added only eight Vultures rows.
  All **248 existing macOS golden fingerprints**, including Old Growth's eight, were unchanged.
- [Seventeen final parameter probes](final-matrix.csv) all passed. They cover both rectangle
  orientations, odd counts, one/eight starting workers, one/twelve colonies, small and large
  homes, no/maximal lakes, zero/maximum resources, and 512×512 at twelve colonies with both
  resources at maximum. No collected telemetry was dropped or invalid.
- A 64×64 request with twelve colonies was rejected before generation with the expected
  request-level crowding diagnostic. A request for 32 colonies was rejected by the shared
  control domain (the current playable maximum is twelve).

| Final parameter group | Seeds | Success | Worst wheat reach | Fewest reachable 4×4 origins |
| --- | --- | --- | --- | --- |
| 256×128, five colonies, home 30, no lakes | 201–204 | 4/4 | 9 | 484 |
| 64×256, four colonies, eight workers, home 16 | 301–302 | 2/2 | 4 | 614 |
| 128×64, one colony/worker, no ambient wheat, max wood | 301–302 | 2/2 | 8 | 831 |
| 512×512, twelve colonies, eight workers, max resources | 301 | 1/1 | 2 | 1,016 |
| 128×128, four colonies, both resources zero | 401–404 | 4/4 | 5 | 1,441 |
| 128×128, four colonies, max resources/lakes/lake size | 401–404 | 4/4 | 5 | 277 |

Building origins overlap; these are placement options, not counts of independent buildings.
All seventeen maps had zero unreachable directed colony pairs. At zero resource amounts, the
four colonies retained 192 food rations and 96 starter wood tiles. High resource amounts did
not require relaxing the dry-wheat or shoreline-wood constraints.

The preliminary eight default seeds had 17,041–17,061 food rations, 741–807 wood tiles and at
least 1,061 reachable local building origins per colony. Preliminary parameter reports and
failed compact seeds are retained separately from final reports: they document why the
multi-patch operation was introduced and must not be pooled as the final build's failure rate.

## Played games

All games used the retained 128×128 two-colony map, map seed 7 and game seed 19.

| Game | Result |
| --- | --- |
| Nicowar mirror, uninterrupted, cap 40,000 ticks | Military victory at tick **31,746**. Winner: 127 units, 60 warriors, 15 buildings. Eliminated colony: five surviving units, one building. |
| Nicowar mirror at tick 20,000 | 49/45 units, 9/5 warriors and 12/8 buildings: both sides established economies and armies. |
| Maxima mirror, cap 20,000 ticks | Both sides remained at four workers and three buildings, with no army. This AI does not provide a useful opponent for the finite-food scenario in this test. |

The uninterrupted Nicowar game consumed or cleared 1,517 of the initial 3,337 wheat rations;
1,820 remained at victory. Wood stock rose from 781 to 3,894 through ordinary shoreline growth.
This demonstrates a playable opening, economic growth and a decisive attack with finite food.
It does not prove every AI, seed or multiplayer size has good pacing, nor that the entire map
must be exhausted before someone can win. Human balance/play-feel review remains outstanding.

## Save/load and coverage limits

[Map round-trip comparison](map-roundtrip.json) found identical terrain, underlying terrain,
resources, space, fertility and movement measurements after loading the serialized sample map.
The final source's regenerated seed-7 map also matches those measurements from the played map.
No special growth flags or serialized caches are introduced by this generator.

**Nicowar simulation continuation is not verified equivalent.** Reloading the tick-10,000
checkpoint agreed through tick 10,166, then differed at tick 10,167. At tick 20,000, unit and
building counts differed as well. [The comparison](save-continuation.json), retained checkpoint,
replay and first differing records allow investigation. The generator is not invoked by that
load path, and simulation/AI source is untouched, but this observation has not been isolated
against a separate base build; do not call it a proven pre-existing bug or claim a passing
continuation test. Map serialization equivalence and AI execution continuity are separate results.

This initial package used macOS ARM64 only. The linked Linux AI pilot adds another platform and
more seeds: independently generated same-seed maps differed, while one game on a shared
serialized map matched per-tick checksums through its ending. That one match does not establish
general cross-platform equivalence. No human playtest or broader fairness tournament was
performed.
The new operations leave existing callers' resource and repair policies in place; the golden
comparison supplies the local evidence for unchanged existing generator output.
