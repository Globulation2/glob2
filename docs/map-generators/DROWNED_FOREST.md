# Drowned Forest

Drowned Forest combines wet woodland islands, permanent sand routes and meadow
settlements found in the landscape. Players can follow the coastal route or put
workers into clearing a timber neck for a shorter trip to another usable meadow.
Normal wood growth can reclaim the cut. Sand routes and construction meadows stay
open through growth, so maintaining a shortcut is a choice rather than a survival
requirement. Shared shore meadows and elongated sandbar shoals offer forward bases;
swimming provides later alternatives.

The generator composes relaxed scattered sites, warped territories, lobed shores,
a near-tree crossing graph, wandering paths, contained farmland and scored start
selection. It uses ordinary terrain and resources, grants only the normal starting
swarm and workers, and never disables resource growth. No simulation or save-format
change is required.

## Controls and supported requests

| Control | Intended effect |
| --- | --- |
| Sandbar connections, 0–100 | Adds links beyond the connected network and required second island connections. More links offer more approaches. |
| Wooded neck thickness, 3–9 | Changes the depth of each five-tile-wide timber plug: 15, 25, 35 or 45 wood tiles. |
| Neutral clearing size, 12–24 | Enlarges forward meadows and junction shoals. Colony construction room is a separate guarantee. |
| Wheat amount | Increases seeded grain beyond the home/forward-base minimum, up to physical plot capacity. |
| Wood amount | Increases ambient forest coverage and planted timber. Structural timber shoulders remain at zero. |
| Stone amount | Increases deposits outside reserved building footprints and gathering faces; two starter deposits per home remain at zero. Fractional rounding distributes intermediate steps among meadows. |
| Algae amount | Increases initial water patches; each colony retains three reachable algae tiles at zero. |
| Fruit amount | Increases orchard seeds around neutral meadow edges. |

Resource controls cover 0–300%. Ambient forest probability is `amount / (amount +
25)`, so high wood settings continue changing coverage. This describes initial
coverage; subsequent normal growth changes it. Plot capacity can limit abundance,
and the finished-map search can select another landscape when a control changes
which starts meet the contract. Connection counts are discrete: small island graphs
may have no unused edge to add at an intermediate setting. Abundance controls
change initial resource coverage, not the crop growth rules.

Supported maps have square or 2:1 proportions and sides of 128–512 tiles:
128×128 supports one or two colonies, 128×256 and its transpose support up to four,
and maps with both sides at least 256 support up to eight. Workers cover the normal
one-to-eight range. Other requests are refused explicitly.

## Construction and validation

Colony sites must fit a meadow and separate grain and timber plots on existing
islands. Farm orientation follows the available ground. An adjoining pool supports
regrowth, and swarm placement faces the actual grain patch to shorten the opening
food haul. One row of sand corners contains each plot without thick paved rings.
The shared farmland helper retains its original two-row default for other callers.

Woodland islands carry irregular timber shoulders reaching their natural coasts.
Two sandy inlets almost meet across each designated plug; the coastal detour stays
open. This structural wood is required even at zero ambient forest. Starts and
forward meadows are chosen and connected using actual toroidal walking distances.
The search is deterministic and bounded; it never relaxes the validation contract
to accept a difficult request. On 128×128 maps it tries alternate sides for forward
meadows before discarding the landscape, with at most 512 landscape attempts;
Fully occupied 128×256, 256×128 and 256×256 maps use at most 256, and other requests use at most
64. Difficult compact or crowded requests can therefore take longer.

The finished-world checks require:

- Connected colonies and meadows on routes that remain open after growth.
- Six independent 4×4 home building footprints with circulation, two independent
  three-wide meadow exits, and renewable home crop and reachable algae supplies.
- A shared, food-bearing forward meadow with at least three building footprints
  within 70 walking steps, with bounded arrival differences between rival colonies.
- Contained crop components, with no growth connection into construction meadows.
- Closed timber plugs whose removal saves at least eight steps and 25% of the
  mouth-to-mouth walk. Every colony must also benefit on an actual trip to another
  usable meadow. Shortcut measurements use current resource-aware walking, rather
  than pretending that empty forest grass is already blocked.

## Verification and evidence

Evidence is retained under `artifacts/drowned-forest/` in the development workspace.
The frozen binaries, generator sources, map requests, previews and game saves identify
which revision each experiment tested. These are local artifacts, not checked-in
release assets.

- `late-review/README.md`: controlled ordinary-worker clearing experiment, treatment
  and baseline saves, per-100-tick measurements and reproduction harness. A useful
  home-to-meadow route fell from 89 to 71 steps after harvesting; the unflagged
  baseline retained its timber and longer route.
- `late-review/REVIEW5.md` and `REVIEW7.md`: iterative visual and economy reviews,
  the food-haul diagnosis, late containment, expansion and actual swimming checks.
- `local-room-check/README.md`: independent 800-case comparison of the bounded
  building-room optimization against the full-map helper, including the reproduced
  blocked-source defect and its regression fix.
- `late-review/FINAL-REVIEW.md`: the final compact-map review and bounded-search
  rationale; `FINAL19.md` diagnoses the tournament’s late food crises.
- `translation-review.md`: agent review of all four new labels in 33 locales.
- `profile-before/`: sampling profiles and CPU benchmarks against Forts.
- `final/`: the optimization-build tournament and timing results.
- `verified/release/`: final source/binary, regression log, exact-output comparisons
  and profiling report.
- `verified/linux-native/`: complete final parameter studies and aggregate analysis.
- `verified/cross-platform/`: matching per-tick replay checksum sidecars from macOS
  ARM64 and Linux x86_64.
- `verified/play-map-equivalence.json`: both tournament maps remain unchanged.
- `README.md`: reproduction commands and notes for compressed evidence.

The eight 25,000-tick AI games cover both seeds with all four rotations of Nicowar,
Cortex, Cabino and Maxima. They show colony development and expansion; some late
colonies lost feeding infrastructure and starved despite accessible wheat. The
controlled harvesting experiment proves the shortcut mechanism, but autonomous AI
use of that strategy was not observed. Human pacing and enjoyment still need play
review.

The same recorded orders produced byte-identical per-tick simulation checksums on
macOS ARM64 and Linux x86_64 for 4096 ticks. Windows and longer cross-platform
continuations were not tested. Existing generator golden rows remained unchanged;
the new generator adds eight matching rows per tested platform.

Profiling identified repeated flood searches and building-room checks. Bounded
searches, exact local room checks with global fallback, lazy reverse searches and
removal of duplicate validation reduced the 512×512/eight-colony default case from
15.529 to 10.003 CPU seconds (36%). The 256×256/four-colony case fell from 1.318 to
1.097 seconds. The compact case rose from 0.429 to 0.502 seconds after reliability
repairs. These are seed-7 process measurements; difficult search tails can take
substantially longer, and Forts remains faster. Telemetry-on timings and all commands
are retained in the profiling artifacts.

The Linux study completed 793/793 control and extreme cases and 1,999/2,000 random
requests. The remaining request, seed 101727 at 128×256 with four colonies, exhausted
the former 64-attempt cap. Extending the dense-map tail to rectangular maps fixed it;
all earlier successful prefixes are unchanged. The failed row and successful retest
are retained separately. Four supplemental requests completed successful coverage of
all 336 supported size/colony/worker combinations. This is sampled reliability, not
an exhaustive test of every combination of controls.

Across eight paired seeds, minimum-to-maximum controls changed mean sandbar count
from 24.25 to 39, neck wood from 15 to 45 tiles, and mean meadow building area from
407 to 466 tiles (the latter combines home and neutral meadows). Wheat increased
522→1,589 tiles, wood 5,336→17,021, stone 8→112, algae 12→4,452 and fruit 0→160.5.
No adjacent control step was unchanged on all eight seeds. Connections 90→100 had
the same overall mean, with individual changes; discrete edges and layout retries
prevent a strict per-seed monotonicity promise.

A compact, committed evidence bundle is in [evidence/drowned-forest](evidence/drowned-forest/README.md).
Earlier local artifacts used provisional generator ID 58. Integration assigns ID 65
because upstream allocated 58 while development was in progress.
