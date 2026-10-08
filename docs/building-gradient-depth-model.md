# The building-field depth model

How far a building's walking field should be settled up front.

A building walking field (`Building::globalGradient`) is a lazy multi-source
search: it settles cost layers only as far as its readers ask. The scheduled
building pipeline refreshes fields on workers instead, and a worker has to decide
how deep to go before anyone has read anything. If it goes too deep, worker CPU is
spent on layers nobody reads. If it stops too shallow, the owner settles the rest
lazily when units read it. This model predicts that depth.

## The property that makes it safe

**The depth only moves CPU between threads. It never changes a result.** Every
reader resolves the cell it reads first (`BuildingGradientSearch::resolve`), and
cells beyond the prepared depth are settled synchronously from the same frozen
search, with values identical to a full build. A different table therefore changes
who pays for the search, never what any unit sees. **Refitting needs no
`SIM_REVISION` bump**, and replays, saves and network games are unaffected.

This is a design rule of the scheduled pipeline, verified by comparing per-tick
checksums with `GLOB2_BUILDING_DEPTH=full`, `table` and `lazy` on the same game
(`test/check_gradient_pipeline.py`).

## What the model is

A quantile table. Each field lifetime is assigned to a cell by the selected keys.
The predicted depth is a quantile of the actual depth that cell's lifetimes
needed, clamped to [32, 65535] cost units (10 per land tile). The quantile is the
*operating point*. A cell with fewer than 40 training lifetimes backs off to the
shorter key prefix. The empty prefix always exists.

Keys were selected by owner saving at a field extra-CPU budget of 1.25. Below are
the selected keys in the order selection added them, with the held-out owner
saving at that budget after each one:

| Keys | Owner saving at 1.25x CPU |
| --- | ---: |
| none (one global quantile) | 0.444 |
| + `previous`: log2 bucket of the field's previous settled depth / 80 | 0.645 |
| + `units`: log2 bucket of the team's live unit count | 0.659 |
| + `type`: building type | 0.671 |

The next candidate, `is_site`, added 0.004, which is below the 0.01 threshold, so
selection stopped there.

The table is in `src/map/gradient/BuildingGradientDepthPolicy.h`: 336 integer rules,
most specific first, each holding one depth per operating point, plus one constexpr
lookup, `target(query, point)`. The summary it is generated from is in
`tools/gradient_depth_model.json`.

A field's own history is by far the best predictor: whatever kept a building's
units reading 30 tiles out last time usually does again. A larger team ranges
farther from its buildings. Building type separates fields read near home from
those read far away (flags, barracks).

## When to refit, and the refits so far

Refit whenever something changes how far units read building fields: fetching
rules, hiring, AI placement, or the default AI order delay. Refitting moves only
CPU, so it needs no `SIM_REVISION` bump. It is the same campaign and the same four
commands each time (see [Running it](#running-it)).

| Fit | Trigger | Keys | p90 field CPU | p90 owner saving |
| --- | --- | --- | ---: | ---: |
| 1 | first fit, on round-trip resource fetching | previous, type, progress | 1.841 | 0.940 |
| **2** | **round-trip removal**: the engine fetches greedily | previous, units, type | 1.722 | 0.825 |

Without round trips, fields are read far shallower than they would settle in full:
eager costs 5.8x the lazy search instead of 2.4x. Site progress stopped mattering,
because construction sites no longer pull round-trip fetchers from afar. The team's
size took its place.

The fit-1 table, scored on the greedy lifetimes, would have cost 4.09x field CPU
at p90 for 0.944 saving. Fit 2 costs 1.72x for 0.825. Fit 1's keys refitted on the
greedy data score almost the same as fit 2's (p90: 1.729x for 0.826), so fit 2's
gain over the stale table comes from the refreshed quantiles, not from the new keys.

## Where the numbers come from

Fit 2 has two sources, 69 games and 311,444 field lifetimes in all, played on a
greedy-fetching build:

- 64 games from the `gradient_depth` [tournament](tools/tournaments.md)
  (`sample_seed` 1, the same draw as fit 1, 18,048 ticks, AI order delay 8). Each
  game draws its map size
  first (64, 128 or 256 tiles a side; 64 is duel-only) and then a format, AIs and
  one of the 25 generators that generated at every size and colony count in a
  preflight.
- 5 busy Oazis games (11 Maxima, seeds 19, 23, 29, 31 and 37, 256x256), which
  stand in for the late, crowded games that cost the most.

`--telemetry gradient-stats` writes one row per lifetime (see
[building field statistics](development/performance-telemetry.md#building-field-statistics)).
With the statistics on, scheduled fields are published with only their seeds
settled, so the depth a lifetime settled is the depth its readers needed, whatever
table is committed. Fit 2 was collected before the pipeline became the default,
from synchronous lazy fields.
The row records the depth that lifetime actually settled, its popped entries per
80-cost band, its resolve calls, and the O(1) owner inputs at the moment it began.
Locked fields and fields without a search are excluded.

Metrics, computed with whole games held out (5 folds):

- **owner saving** is the share of lazy search work a build to depth D moves off the
  owner: Σ popped(min(D, actual)) / Σ popped(actual);
- **extra CPU** is total search work relative to lazy: Σ popped(max(D, actual)) /
  Σ popped(actual). Settling past a complete field's end costs nothing. Beyond a
  partial field's reach, the mean profile of complete fields of the same route and
  map size estimates the cost;
- **hit rate** is the share of lifetimes with actual ≤ D, and **coverage** is the
  same weighted by resolve calls.

Key sets are compared by owner saving at an extra-CPU budget of 1.25. For each
candidate, the quantile is swept over 0.3–0.95 and the saving is interpolated to
the budget, so that a key cannot look better just by predicting deeper.

## Operating points

The budget that matters is whole-process CPU, not field-search CPU: the
scheduled pipeline must stay at or below 1.15x process CPU. Search work is a small
share of the process, so a deep operating point costs little.

**Measuring the share.** `gradient.building_resume`, the lazy search extensions,
was divided by `benchmark_run_cpu_ns`, with the flag off and `--benchmark-warmup 0`.
These fit-2 runs used a greedy build on an unpinned 8-core arm64 Mac, one run per
scenario at 18,048 ticks.

| Scenario | Process CPU | Search share | Search + initialization |
| --- | ---: | ---: | ---: |
| busy Oazis seed 19 | 33.5 s | **0.026** | 0.112 |
| mixed (generator 26, seed 19) | 3.6 s | 0.006 | 0.019 |
| small (64x64 duel, seed 1) | 1.6 s | 0.008 | 0.019 |

With round trips, measured on reserved x86_64 cores, the busy share was 0.064 to
0.067. Greedy fetching reads building fields far less, so the busy whole-game search
share is now 0.026, while initialization (`gradient.building`, 0.086) still costs
about as much as before.

Only the search scales with depth; initialization is paid once per rebuild whatever
the depth. Process CPU at operating point q is therefore estimated as 1 + share x
(field extra CPU - 1). The estimate uses the busy share for Oazis and for the
pooled figures, and the mixed/small share (0.007) for the tournament.

**The curve.** Held out by game (`evaluate --curve`), keys previous, units and type:

| Point | Scenario | Hit rate | Coverage | Field CPU | Owner saving | Process CPU (est.) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| p50 | all | 0.525 | 0.571 | 1.137 | 0.567 | 1.004 |
| p50 | busy Oazis | 0.520 | 0.398 | 1.132 | 0.543 | 1.003 |
| p70 | all | 0.714 | 0.762 | 1.270 | 0.686 | 1.007 |
| p70 | busy Oazis | 0.715 | 0.616 | 1.263 | 0.664 | 1.007 |
| **p80** | all | 0.807 | 0.851 | 1.398 | 0.747 | 1.010 |
| **p80** | busy Oazis | 0.811 | 0.737 | 1.388 | 0.725 | 1.010 |
| p85 | all | 0.854 | 0.888 | 1.515 | 0.781 | 1.013 |
| p85 | busy Oazis | 0.861 | 0.808 | 1.509 | 0.763 | 1.013 |
| **p90** | all | 0.898 | 0.919 | 1.722 | 0.825 | 1.019 |
| **p90** | busy Oazis | 0.904 | 0.859 | 1.724 | 0.811 | 1.019 |
| **p95** | all | 0.945 | 0.948 | 2.277 | 0.895 | 1.033 |
| **p95** | busy Oazis | 0.952 | 0.917 | 2.335 | 0.894 | 1.035 |
| eager | all | 1.000 | 1.000 | 5.766 | 1.000 | 1.124 |
| eager | busy Oazis | 1.000 | 1.000 | 5.761 | 1.000 | 1.124 |

The p60 rows and the tournament breakdown are in the `evaluate --curve` output.

Every point up to p95 stays at or below about 1.035x estimated process CPU. That
makes owner saving, not CPU, the binding constraint, and a point well above the
median is needed: p50 saves only 0.57 of the search.

**How the default was chosen.** The header carries three points, `p80`, `p90` and
`p95`, and the committed default (`DEFAULT_POINT`) is **`p80`**, chosen by measured
timing rather than by the estimates above. Each point was built with
`-DGLOB2_BUILDING_GRADIENT_DEPTH_POINT=<index>` (which swaps the point without
regenerating anything) and timed against `GLOB2_BUILDING_DEPTH=full` on late busy
Oazis checkpoints (11 Maxima), building gradient delay 8, four compute workers, on a
quiet x86_64 host. The table gives the drop in owner loop work relative to lazy
synchronous fields on three checkpoints, and process CPU relative to lazy:

| Depth | Loop-work drop | Process CPU |
| --- | --- | --- |
| **p80** | 41%, 53%, 45% | 1.01–1.06x |
| p90 | 40%, 49%, 49% | 1.04–1.09x |
| p95 | 38%, 46%, 44% | not recorded |
| full | 33%, 43%, 38% | not recorded |

`p80` saves as much owner time as `p90` within noise for less CPU, and the deeper
points save less. A refit or a pipeline change should repeat this
comparison and, if another point wins, regenerate with
`fit --operating-point Q --points ...` and record the measurement here.

### The default, by map size

`p80`, held out by game:

| Map | Lifetimes | Hit rate | Coverage | Field CPU | Owner saving |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64x64 | 23,595 | 0.872 | 0.899 | 1.544 | 0.956 |
| 128x128 | 98,348 | 0.796 | 0.899 | 1.133 | 0.843 |
| 256x256 | 189,501 | 0.805 | 0.791 | 1.457 | 0.719 |
| all | 311,444 | 0.807 | 0.851 | 1.398 | 0.747 |

## What does not predict depth

Screened on its own, at the same budget, against 0.444 for no key at all:
`previous` 0.645, `units` 0.488, `buildings` 0.480, `type` 0.457, `route` 0.447,
`progress` 0.445, `is_site` 0.444, `level` 0.444, `construction` 0.444, `swim`
0.443 and **`size` 0.441**.

**Map size does not earn a place,** in either fit. The size-normalised target (depth
per tile of width + height) scored 0.390 alone, worse than the raw depth. Larger
maps do need deeper fields on average, but within a game the spread between
buildings is far larger than the difference between map sizes, and `previous`
already carries whatever the size implies.

## Running it

```sh
# Data: a gradient_depth tournament (see docs/tools/tournaments.md), plus any
# directories of plain --run-game outputs that hold gradient-stats.csv.
python3 -m tools.tournaments.gradient_depth plan depth.json --bundle BUNDLE --output planned.json
python3 -m tools.tournaments submit planned.json RESULTS --bundle BUNDLE
python3 -m tools.tournaments run RESULTS --hosts hosts.json

python3 tools/gradient_depth_fit.py dataset RESULTS RUNS --output dataset.json.gz
# What each key buys alone and in forward selection, held out by game.
python3 tools/gradient_depth_fit.py screen dataset.json.gz --jobs 14
# Select keys, or name them, and regenerate the summary and the header with its
# operating points; --operating-point is the committed default.
python3 tools/gradient_depth_fit.py fit dataset.json.gz --jobs 14 \
  --keys previous units type --operating-point 0.8 --points 0.9 0.95
# The committed default's metrics, overall and per map size.
python3 tools/gradient_depth_fit.py evaluate dataset.json.gz
# Owner saving against field and estimated process CPU, per operating point.
python3 tools/gradient_depth_fit.py evaluate dataset.json.gz --curve [--share busy_oazis=0.026]
```

It needs only the Python standard library; `--jobs` parallelises the screen by
fork. `test/test_gradient_depth_fit.py` asserts three things: regenerating the
header from the committed summary is a no-op, the compiled C++ lookup agrees with
the fitter on random queries, and the fitter recovers keys it was given in
synthetic data.

## Limits

- It was fitted on AI games under AI order delay 8. Human players place and staff
  buildings differently, and nothing here has been checked against human play.
- The depth a lazy field settled is the depth that lazy readers needed. A scheduled
  field is read at the same places, but the extra-CPU and saving figures are
  estimates from the depth histograms, not measured thread time (the operating
  point itself was chosen by measured time). Beyond a partial field's reach the
  work is extrapolated from complete fields.
- Fit 2's search shares come from single unpinned runs on a Mac. They set only the
  process-CPU estimate, not the table.
- The tournament draw is 64 games across 25 generators, so per-generator behaviour
  is not resolved. Large 256x256 games, busy Oazis in particular, carry most of
  the lifetimes and most of the weight.
- `previous` needs the depth the field's last lifetime settled, which the scheduled
  pipeline keeps as an owner-only hint. A field built for the first time uses the
  `-1` (no history) row.
- The process-CPU column of the curve is an estimate: a measured search share times
  the histogram-based field ratio. The measured costs are in
  [Operating points](#operating-points).

## Related

- [Building field statistics](development/performance-telemetry.md#building-field-statistics)
  are the rows this is fitted on.
- [Distributed tournaments](tools/tournaments.md) describe the `gradient_depth`
  campaign.
- [The win probability model](win-probability-model.md) is fitted the same way:
  dataset, screen, fit, then a generated header.
