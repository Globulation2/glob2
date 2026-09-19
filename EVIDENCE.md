# Who Ate the Map? — implementation evidence

Generator `who-ate-the-map`, numeric ID 60, revision 1. Optional novelty geography;
existing generators and simulation rules are unchanged. Source and build identity:
[final manifest](final/manifest.json). See the tracked
[design and controls](https://github.com/Globulation2/glob2/blob/map/who-ate-the-map/docs/map-generators/WHO_ATE_THE_MAP.md).

## Picture and play

[Final gallery](final/gallery.png) shows all three Appetite settings at 128 and 256.
[Large detached-start example](final/512x512-a2-s11.png) retains an inhabited island.
[Late 256 preview](final/late256.png) shows terrain and crop growth after 40,000 ticks.
The map uses rounded jaw cuts with overlapping tooth impressions; ponds, woodland,
fertile shores and unequal approaches determine the interior play. No home terrain
is stamped, no free pools are given, and no no-growth flags are used.

Independent map-review rounds revised the teeth, rejected cramped opening sites,
and diagnosed a detached-crescent pool trap. The final capacity budget leaves small
fragments as scenery while retaining large inhabitable fragments. Review 3/4 records
show the first opening failure and successful remedy; review 5 contains the exact
save-record diagnosis of a pool built on the wrong island. Review 6 checks the remedy.
These are agent reviews for this author, not independent maintainer approval.

Twelve mixed games, capped at 25,000 ticks, rotate Nicowar, Cortex, Cabino and Maxima through all
four seats on three 128 maps: Appetite 2 / seed 1, Appetite 1 / seed 7, Appetite 2 / seed 7, game
seed 1. Ten reached the tick cap; two ended naturally at 21,858 and 24,354 ticks.
Across 48 colony-games: mean peak 83.7 units, final 60.5 units, 9.7 buildings,
145.5 wood and 779.9 wheat harvested; 20.6 worker combat deaths versus 2.2 worker starvation
deaths. No colony had a peak below 20 units or an unexplained low-wood survival stall.
Harvest, training and death counters use the last telemetry sample at or before
termination; final units/buildings come from the result snapshot. These are economy
observations, not proof of equal starts or human fun.
[Per-colony data](play-v6/economy-summary.json), exact requests and compressed logs
are in `play-v6/`.

The corrected 256 / Appetite 2 / seed 7 four-Nicowar 40,000-tick regression produced
75–137 worker births and 360–548 wood harvested per colony. Team 0, previously stranded,
now harvested 490 wood and recorded 147 worker-training visits, with zero worker
starvation. One rival suffered heavy combat losses. See
[regression economy](play-v9/economy-summary.json), [request](play-v9/request.json)
and the final save/log in `play-v9/256-a2-s7-nicowar`.

The 512×512 / Appetite 2 / seed 11 four-Nicowar game also completed its
40,000-tick cap. The detached colony ends with 269 units, 30 completed buildings
and 15 swimming workers. Its two completed level-2 pools are at (58,253) and
(65,255), on the same western island as its original swarm at (57,232).
At the last complete counter sample (38,400 ticks), it had 99 worker and 146 warrior
swimming gains, zero worker/warrior starvation, and melee combat against units and
buildings. Explorers did suffer 82 starvation deaths; this is not a claim that every
unit type avoided starvation. The final save therefore resolves the earlier misplaced-pool mechanism
in this representative run. [Final review](review6/review-summary.md),
[pool records](review6/team0-buildings.json), [late preview](review6/final.png).
No remaining blocker was found within the agent review's scope.

Play uses frozen binaries: v6 mixed games, v9 long games. Their five map inputs are
byte-identical to the final generator, established by exact serialization at the
same path: [v6→v9](equivalence/play-v6-v9.json),
[v9→v10](equivalence/play-v9-v10.json), [v10→final](equivalence/play-v10-final.json).
The old 256 map intentionally differs at v9 because its cramped start was moved.
Earlier exploratory games and disk-interrupted runs are not counted above.

## Parameter study

[Study summary](parameter-summary-v10.json), [telemetry summary](telemetry-summary.json).
`study-v10/run.py` contains the complete reproducible driver; each mode has an exact
request manifest, binary SHA256, per-case JSONL and compressed engine reports.

- 2,000 randomized valid requests: all succeeded. Dimensions span all seven supported
 square/rectangular shapes; colony counts and workers 1–8 vary within the envelope,
 all five resource amounts vary over 0–300 in 25-point steps, and all Appetite levels
 occur. Random sampler seed 719205 uses independent map seeds 100000–99999999.
- 126 extremes: all succeeded, covering seven shapes, minimum/maximum colonies,
 three Appetite levels, all-resource 0/100/300, eight workers, seed 71.
- 816 one-control-at-a-time cases: all succeeded. Sizes 128/256/512, seeds 1–4,
 four colonies/four workers; three Appetite values and all 13 values of each resource
 control. Each control's mean intended statistic is increasing at every step at
 every size. This is measured aggregate monotonicity, not a promise about every seed
 after access repairs.
- 192 envelope cases: 44 supported requests succeeded, 148 unsupported requests refused;
 zero unexpected results.

Mean results at 256×256, four colonies, seeds 1–4:

| Control |0|100|300|
|---|---:|---:|---:|
|Wheat tiles|400|2,068.5|5,405.5|
|Wood tiles|260|5,265.5|14,776.8|
|Stone tiles|13.2|101|280|
|Algae tiles|0|406|1,138.8|
|Fruit tiles|0|70.2|210.8|

Appetite 0/1/2 removes 9.0%/19.6%/57.6% of the pre-bite island at 256 and 512;
at 128 the means are 10.4%/22.6%/57.6%. Starter wheat/wood/quarries intentionally
survive 0% ambient amounts. Wood allocation caps at 58% of eligible ambient tiles.

All three bite profiles occurred about 2,000 times each. 1,909 random maps used one
occupied land component; 91 used two. 1,990 succeeded on the first start-placement
trial; one needed the 20-tile spacing fallback. Both previously failing seeds 71 and
63577209 are regression fixtures. The study predates only the output-preserving
performance changes; 42 shape/seed/settings comparisons and all five gameplay maps
were byte-identical afterward.

## Verification and profiling

- Local full defaults/framework/contracts suite passed:
 [log](defaults-final.log). Final targeted contracts additionally cover coastline
 preservation, telemetry on/off repeatability, deliberate crop/coast corruption,
 support/refusal boundaries, crowded seeds and detached-start regressions:
 [log](final/contracts.log).
- macOS ARM: 464 golden rows compared, zero failures
 ([log](final/golden-check.log)); Linux x86-64: 464 rows, zero failures
 ([log](linux/golden-optimized.log)). All eight new generator rows match between
 platforms. Final Linux capacity optimization also passes targeted contracts
 ([log](linux/contracts-final.log)). Existing golden values are unchanged.
- Translation subagent reviewed all 33 catalogs and corrected Italian and Korean.
 [Structural check](final/translations.log): zero missing/untranslated entries or
 structural errors. This is not native-speaker certification of every language.
- Profiler found repeated per-tile trigonometry and unnecessary island service-space
 analysis where no detached island could possibly qualify. Hoisted sin/cos per bite
 and skipped that impossible branch. [Before sample](performance/before-sample.txt),
 [source snapshots](final/source.cpp), [42 exact map comparisons](equivalence/performance.json).
- Paired 32-request fixture runs (seed 771), reverse order on the second pair:
 CPU (user + system) mean 3.035 s before versus 2.685 s after, an 11.5% reduction.
 Wall time is noisy under concurrent machine load. [Commands and results](performance/benchmark.py),
 [raw timings](performance/benchmark.json).
- 512×512/eight colonies/default Appetite, seeds 1/7/11: mean generation+save CPU
0.477 s before, 0.437 s after; comparable Continents 1.093 s. Including the detailed JSON
 report: 0.710 s before, 0.683 s after, Continents 1.293 s.
 [Benchmark script](performance/largest.py), [timings](performance/largest.json).
- Telemetry toggling on nine 512×512/eight-colony cases preserved serialized bytes,
 outcomes and RNG state; zero semantic failures. Alternating off/on totals were
7.163 s/7.087 s wall time; those noisy timings show no useful measurable overhead.
 [Narrowed existing golden harness](performance/TelemetryCheck.cpp),
 [build script](performance/build-telemetry.py), [log](performance/telemetry.log).

This work has AI play evidence and engine previews, not a human playthrough.
Windows execution and per-tick cross-platform simulation replay comparison were
not performed. The addition changes neither save format nor simulation code;
saved-map generation fingerprints match on the two tested platforms. This archive retains the evidence available for PR review.


## Archive scope and integration

This is a selected evidence archive. Per-case engine reports, most full game logs and old executables remain local; the request manifests, per-case result tables, summaries, representative maps and the final 512 save are retained here. Historical records use provisional ID 60; integration assigns ID 66 without changing the terrain algorithm. Paths in reproduction scripts reflect the original workspace.
