For the tuned revision-4 opening and subsequent AI games, see [initial playtest results](PLAYTEST.md).
For the revision-5 full-range generation check and repair, see [bulk generation results](BULK_GENERATION.md).
The evidence below records revision 1.

# Rice Terraces verification — revision 1

[Initial seed 7](seed-7.png) · [After 40,000 Nicowar ticks](after-40000-ticks.png) ·
[Download evidence](https://github.com/Globulation2/glob2/blob/evidence/rice-terraces/docs/map-generators/evidence/rice-terraces/verification.zip) (on the evidence branch, kept out of the main history)

The archive contains native maps, request/report JSON, both AI games' initial and
final saves and replays, study configurations and accepted result records, telemetry
summaries, test logs, source/build identity, and a SHA-256 manifest. These are final
results after explicitly sequencing the two layout RNG draws for compiler portability.
The source identity records the tested runtime source; subsequent documentation,
golden additions and test-only whitespace cleanup do not change the executable.

## Results

- Linux x86_64 and macOS arm64 release builds: **256 golden cases each, zero
  failures**. All existing rows are unchanged. All eight new generator rows agree
  across platforms (seven generated worlds and one invalid request).
- Seed 7, default 256×256/four colonies: native `.map` bytes match across platforms,
  SHA-256 `acbc856a222538055da2ab3363668c827e11955dbfeda293b403dee311e15115`.
- Rice Terraces sweep: **36 successful generations**, zero failing combinations.
  Tested 128²/two colonies; 256²/two, three, four, six and eight colonies;
  512²/four and twelve colonies. 128²/four and 256²/twelve are explicitly invalid.
- Defaults/toolkit contracts pass, including corner rasterization, wrapped and
  translated farms, tower coverage selection, two/three/four stairs, scarcity,
  crowding, rectangles, vacant hills and **12,000 resource-growth calls**.
- Extra seed-37 probes pass: 512²/twelve colonies plus four vacant hills at 300%
  wheat/wood and narrow bands; 512²/two colonies with radius 100 and no towers or
  river; 256×128 and 128×256/four colonies.
- Translation structure: zero errors; five translation regression tests pass.

The durable tournament workflow ran 28 requests against immutable build
`714e216c5fe2ff0e4925d0d854267e0b67a599e33ed7c4eb377800e839455085`:

| Study | Generated | Explicitly invalid | Lowest static fairness |
| --- | ---: | ---: | ---: |
| Training seeds 1–4 | 4 | 0 | 0.9594 |
| Held-out seeds 20001–20004, three stair variants | 12 | 0 | 0.9030 |
| Crowding/geometry | 6 | 4 | 0.9664 |
| Zero ambient resources | 2 | 0 | 0.9630 |

No reports were missing. The only fallback was bounded crop-budget saturation on
six crowded maps: the requested deposits exceeded eligible terrace space. No
starting-crop repair fallback occurred. Across generated study maps, the smallest
building-room count was 418 overlapping 4×4 origins, and the largest initial wheat
and wood walking distances were 20 and 22. These are static opening measurements,
not independent building capacity or proof of competitive balance.

The telemetry equivalence test generated all 96 cases with zero semantic failures;
collection preserved serialized worlds and restored RNG state. Rice Terraces
seeds 1/2/3 took respectively 180.752/184.840/176.976 ms without collection and
181.864/235.764/180.814 ms with collection, with 73 records each. These single
wall-clock samples ran under concurrent load; the second sample is noisy and does
not establish stable overhead. The source adds no telemetry-specific RNG draws or
map scans.

## AI smoke games

Both used map seed 7, game seed 19, four identical AIs, and the default generator.
All teams remained alive at the tick cap; neither game resolved a winner.

| AI | Ticks | Units by team | Buildings by team | Critically hungry units |
| --- | ---: | --- | --- | --- |
| Nicowar | 40,000 | 119, 165, 112, 45 | 17, 17, 20, 19 | 3, 10, 12, 6 |
| Numbi | 20,000 | 19, 13, 14, 14 | 13, 12, 11, 12 | 0, 0, 0, 0 |

Previews show expanded terrace crops with clear summits and stairs. Nicowar's
uneven populations and hunger warrant human playtesting. These games establish
that the AIs can build and sustain colonies, not that valley contests, attack
pacing or summit tower strength are balanced. Swimming bypasses water barriers;
no engine elevation or gate mechanic is added.

## Reproduction

Build and generation commands are in [the design document](../../RICE_TERRACES.md).
Run the harnesses directly to reproduce the retained logs:

```sh
build/src/MapGeneratorDefaultsTest rice-contracts
build/src/MapGeneratorGoldenTest rice-golden --require-rows
build/src/MapGeneratorGoldenTest rice-sweep --sweep 21/32
build/src/MapGeneratorGoldenTest rice-telemetry --telemetry
build/src/glob2 --run-game --map-file seed-7.map --game-seed 19 --player nicowar --player nicowar --player nicowar --player nicowar --ticks 40000 --save initial --save every:20000 --save final --replay true --telemetry team-timeline --output-dir nicowar
```

The archive retains the four stress configurations, plans and accepted records;
use `tools.tournaments.generator_stress` plan/submit/doctor/run/reanalyze as
specified in the map-design skill's distributed telemetry reference. `analyze.py`
rebuilds the compact analysis from accepted records when extracted under
`artifacts/rice-terraces/final-linux`.

No simulation, save-format, replay acceptance or network version rules changed.
Windows generation and cross-platform per-tick simulation checksums were not
verified. Cross-platform claims here concern map generation only. Human gameplay
review remains outstanding.
