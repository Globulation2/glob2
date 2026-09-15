# Lava shield bulk generation and tuning, 2026-09-15

## Result and scope

The final revision-1 Lava shield executable completed **504 of 504 legal,
single-seed requests**. The paired original executable completed 496 of those
same 504 requests; its eight failures were small renewable starter fields in
otherwise legal scored towns. The final client rescued all eight and introduced
no new generation failure. This is a finite seed/settings study, not a proof that
every possible seed works.

The [main plan](evidence/lava-shield/plan.json) contains 424 jobs across 16
size/shape/density combinations, including 128-square maps, both 2:1 rectangle
orientations, and 512-square twelve-colony layouts. Two seed samples cover each
baseline, three cover each geometry extreme, and fixed random draws combine
controls. The [disjoint held-out plan](evidence/lava-shield/heldout-plan.json)
adds 80 jobs: every intermediate registered colony count from one through twelve
at its legal size, and 24 fresh dense extreme seeds. Their seeds and complete
settings are preserved in the plans; candidate selection is disabled to measure
the generator's unselected single-seed distribution. Both plans avoid requests
the documented size/density envelope rejects up front.

The matrix includes **every registered value** of tongues (3–9), long tongues
(25/50/75%), branching (0–3), rim width (6/8/10/12), workers (1–8), and each
of the five resource amounts (0–300% in 25% increments). The smaller legal maps
retain their combined tongue/rim bounds; a rejected request is not treated as a
map-generation failure. The [final plan](evidence/lava-shield/final-plan.json)
repeats the exact 504 requests on the rebuilt executable. The
[per-job final CSV](evidence/lava-shield/final-jobs.csv) retains every seed,
dimension, colony count, control, outcome, quality, construction fallback and
report status. The [summary](evidence/lava-shield/final-summary.json) groups
outcomes by shape and variant; the [paired comparison](evidence/lava-shield/paired-comparison.json)
records the eight rescues and any formerly successful world that changed.

### Small and rectangular maps

| Tested shape/density | Final completed |
| --- | ---: |
| 128×128, one or two colonies | 50/50 |
| 128×256 and 256×128, two colonies | 44/44 |
| 256×256, one through eight colonies | 110/110 |
| 256×512 and 512×256, two/four/eight colonies | 132/132 |
| 512×512, one through twelve colonies | 168/168 |

This matters because square mid-size defaults alone cannot expose a stretched
coast, cramped high-density fields, or the opposite axis of toroidal wrapping.
The 128-square preview remains a small island with a visible crater; it does
not promise the same expansion room or battle pace as a 512-square world.

## Why the starter fallback changed

All eight original failures reached the `lava scored settlements` stage.
Four scored town proposals per request rejected a starter wheat or wood field
below its floor. The first patch seed had been chosen by highest fertility, but
that tile could be an isolated grass pocket alongside sand or stone. The original
main cohort was 420/424 (99.1%) and held-out cohort 76/80 (95.0%); the latter
included two of 24 dense high-control seeds. Tightening the registered range
would have discarded useful compact and dense variants without addressing that
seed-selection problem.

The reusable Planting operation `seedForPatchCapacity` ranks eligible candidate
tiles by their 5×5 legal frontage, then a caller preference such as fertility.
Lava shield invokes it only if the first crop field falls below half its 40-wheat
or 32-wood target. Up to three secondary compact patches may fill the same
16-tile seed catchment and 18-tile growth limit. Existing deposits, town grass,
roads, stone and the crater remain protected; the acceptance floors remain
20 wheat and 16 wood. There are no extra RNG draws. An already sufficient
proposal's resource path stays the same. The final cohort recorded the
`lava-shield.starter.secondary` fallback on 38/504 maps, and the narrow legal
approach fallback on 48/504. Both records are bounded telemetry events, not
measurements added by scanning the final grid during ordinary generation.

The paired comparison found **30 formerly successful requests with changed
world measurements**. A rescued proposal can outscore the old chosen proposal;
these are revised starts/resources, not a change to an existing map type or
core game rule. Representative macOS and Linux generator golden rows retained
their expected hashes on the final implementation. Reviewers should still play
the map to judge travel congestion, pacing and the contested crater rim; static
reachability does not measure those experiences.

## Final-world and telemetry observations

All 504 accepted final reports had exactly two water components: ocean and
inland crater lake. They reported zero unreachable directed walking pairs
between colonies. The minimum finished starting-colony fairness ratio was
0.7979 versus the generator floor of 0.65, and the minimum overlapping 4×4
building-site count was 164 versus its floor of 48. The minimum normalized
wheat and wood access values were 0.3333 and 0.5313; these are
`StartQuality` scores, not crop tile counts. Every accepted map supplied
version-2 final-map and version-1 generation-telemetry records, with zero
dropped records or invalid values. The [raw minimum map report](evidence/lava-shield/final-minimum-report.json),
[dense extreme](evidence/lava-shield/final-dense-high-report.json), and
[rescued seed](evidence/lava-shield/final-rescued-starter-report.json)
show the full metrics and internal decisions behind selected rows.
The [rectangle's complete native report](evidence/lava-shield/maps/rectangle-512x256.json.gz)
is attached in compressed form beside its playable map.
The shared framework's complete [typed event rows](evidence/lava-shield/final-telemetry-records.csv.gz)
and [numeric final-map metric rows](evidence/lava-shield/final-map-metrics.csv.gz)
are attached as compressed CSVs. `gzip -dc FILE.csv.gz > FILE.csv` makes either
cohort-wide table readable without rerunning analysis.

The final run used immutable Linux x86-64 bundle
`0d103f2221fcfacb65527502a1bcfc73025a93775137eef85698781ba0ceaaf0`,
GCC 15.2, binary SHA-256
`eea2783b4aced6474b56109916ec2d110a28a7105f55b9e142294d6145a8888e`.
Its exact [source identity](evidence/lava-shield/fallback-source-identity.json)
and the accepted study plans are retained. The compatible workers were
`therig.local` and `pharaoh-dev-{1,2,3}.local` (glibc 2.43). Before removing
`devlaptop.local`, 34 nonaccepted process attempts failed during startup because
that host has glibc 2.39. None entered map generation; all affected logical jobs
were accepted later on compatible hosts. The actual
[startup error and host versions](evidence/lava-shield/worker-startup.json)
are attached so infrastructure errors cannot be mistaken for generator results.
The original baseline bundle was Linux x86-64
`687e1efa6c12e9ef2bba17f472af4eb204ff9a3a35406716dfd42bd44371584e`.

## Reviewable maps and checks

The [four native screenshots](LAVA_SHIELD.md#review-previews) match the final
macOS client and have the same requests as the
[compressed playable maps and JSON reports](evidence/lava-shield/maps/).
The [checksums](evidence/lava-shield/preview-checksums.json) identify each
attached file. For example, `gzip -dc small-128.map.gz > small-128.map` produces
the previewed map for the game or editor. These screenshots and map files back
the visual claim; the earlier [AI playtest summary](evidence/lava-shield/playtest/README.md)
examined colony growth and late crops on the prior executable. Human judgment
about whether the rim prize and beach detours feel enjoyable remains part of
review.

The final code passed `lava-shield-contracts-final`, the supported 21/31-seed
suite, and all 256 platform-specific generator golden rows on both macOS arm64
and Linux x86-64. The translation audit had zero errors across all 33 language
catalogs, and its five regression tests passed. The custom labels `Lava shield`,
`Lava tongues`, `Long tongues` and `Crater rim width` have nonempty localized
values in every catalog; other labels use existing shared translations. The
bulk study is reproducible from the attached exact manifests and the shared
`tools.tournaments` framework; the native CLI uses tile counts, while framework
`width` and `height` request values are power-of-two exponents.
