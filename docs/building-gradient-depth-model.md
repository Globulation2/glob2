# The building-field depth model

How far a building's walking field should be settled up front.

A building walking field (`Building::globalGradient`) is a lazy multi-source
search: it settles cost layers only as far as its readers ask. The scheduled
pipeline in development builds fields on workers instead, and a worker has to decide
how deep to go before anyone has read anything. If it goes too deep, worker CPU is
spent on layers nobody reads. If it stops too shallow, the owner settles the rest
lazily, as it does today. This model predicts that depth.

## The property that makes it safe

**The depth only moves CPU between threads. It never changes a result.** Every
reader resolves the cell it reads first (`BuildingGradientSearch::resolve`), and
cells beyond the prepared depth are settled synchronously from the same frozen
search, with values identical to a full build. A different table therefore changes
who pays for the search, never what any unit sees. **Refitting needs no
`SIM_REVISION` bump**, and replays, saves and network games are unaffected.

This is a design rule of the scheduled pipeline. Once that pipeline exists it
must be verified once, by comparing per-tick checksums from two different tables
on the same game.

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
| none (one global quantile) | 0.338 |
| + `previous`: log2 bucket of the field's previous settled depth / 80 | 0.542 |
| + `type`: building type | 0.590 |
| + `progress`: material-delivery quartile on construction sites | 0.619 |

The next candidate, `swim`, added 0.007, which is below the 0.01 threshold, so
selection stopped there. The key choice does not depend much on the budget: the
same three keys lead at every quantile in the screen.

The table is in `src/map/gradient/BuildingGradientDepthPolicy.h`: 361 integer rules,
most specific first, each holding one depth per operating point, plus one constexpr
lookup, `target(query, point)`. The summary it is generated from is in
`tools/gradient_depth_model.json`.

A field's own history is by far the best predictor: whatever kept a building's
units reading 30 tiles out last time usually does again. Building type separates
fields that are read near home (swarms, inns) from those read far away (flags,
barracks). Sites still waiting for materials draw fetchers from farther away than
sites that are nearly built.

## Where the numbers come from

There are two sources, 69 games and 322,917 field lifetimes in all:

- 64 games from the `gradient_depth` [tournament](tools/tournaments.md)
  (`sample_seed` 1, 18,048 ticks, AI order delay 8). Each game draws its map size
  first (64, 128 or 256 tiles a side; 64 is duel-only) and then a format, AIs and
  one of the 25 generators that generated at every size and colony count in a
  preflight.
- 5 busy Oazis games (11 Maxima, seeds 19, 23, 29, 31 and 37, 256x256), which
  stand in for the late, crowded games that cost the most.

`--telemetry gradient-stats` writes one row per lifetime (see
[building field statistics](development/performance-telemetry.md#building-field-statistics)).
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

**Measuring the share.** `gradient.building_resume`, the lazy search extensions
including those nested in round trips, was divided by `benchmark_run_cpu_ns`. The
runs used the flag off, `--benchmark-warmup 0`, and `run_with_benchmark_cpuset.py`
with cores 0-7 reserved.

| Scenario | Ticks | Process CPU | Search share | Search + initialization |
| --- | ---: | ---: | ---: | ---: |
| busy window (Oazis checkpoint, ticks 16000-18048) | 2,048 | 18.7 s | **0.067** | 0.126 |
| busy Oazis seed 19, whole game | 18,048 | 77.5 s | 0.064 | 0.121 |
| mixed (generator 26, seed 19) | 18,048 | 6.7 s | 0.015 | 0.025 |
| small (64x64 duel, seed 1) | 18,048 | 2.7 s | 0.015 | 0.026 |

Only the search scales with depth; initialization (`gradient.building`) is paid once
per rebuild whatever the depth. Process CPU at operating point q is therefore
estimated as 1 + share x (field extra CPU - 1). The estimate uses the busy-window
share for Oazis and for the pooled figures, and the mixed/small share for the
tournament.

**The curve.** Held out by game (`evaluate --curve`), keys previous, type and
progress:

| Point | Scenario | Hit rate | Coverage | Field CPU | Owner saving | Process CPU (est.) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| p50 | all | 0.518 | 0.470 | 1.242 | 0.608 | 1.016 |
| p50 | busy Oazis | 0.477 | 0.448 | 1.210 | 0.602 | 1.014 |
| p70 | all | 0.712 | 0.791 | 1.410 | 0.787 | 1.028 |
| p70 | busy Oazis | 0.682 | 0.724 | 1.373 | 0.793 | 1.025 |
| **p80** | all | 0.806 | 0.866 | 1.547 | 0.865 | 1.037 |
| **p80** | busy Oazis | 0.794 | 0.825 | 1.509 | 0.881 | 1.034 |
| p85 | all | 0.854 | 0.894 | 1.634 | 0.898 | 1.042 |
| p85 | busy Oazis | 0.854 | 0.870 | 1.592 | 0.915 | 1.040 |
| **p90** | all | 0.900 | 0.923 | 1.841 | 0.940 | 1.056 |
| **p90** | busy Oazis | 0.912 | 0.905 | 1.807 | 0.960 | 1.054 |
| **p95** | all | 0.949 | 0.951 | 2.033 | 0.974 | 1.069 |
| **p95** | busy Oazis | 0.969 | 0.960 | 2.012 | 0.993 | 1.068 |
| eager | all | 1.000 | 1.000 | 2.405 | 1.000 | 1.094 |
| eager | busy Oazis | 1.000 | 1.000 | 2.427 | 1.000 | 1.096 |

The p60 rows and the tournament breakdown are in the `evaluate --curve` output. On
the tournament alone, p90 saves 0.908 at 1.895x field CPU.

Every point up to eager stays well under 1.15x estimated process CPU. That makes
owner saving, not CPU, the binding constraint. It also means a point well above the
median is needed to leave a margin over the scheduled pipeline's 60% owner-drop
acceptance bar: p50 saves only 0.61 of the search.

**How the default is chosen.**
- The header carries three points, `p80`, `p90` and `p95`. They span 0.88 to 0.99
  owner saving on busy Oazis at an estimated 1.03x to 1.07x process CPU.
- The committed default is `p90` (`DEFAULT_POINT`), a provisional middle choice.
- The scheduled pipeline's timing runs pick the winner among the three by measured
  owner wall and process CPU. Building with
  `-DGLOB2_BUILDING_GRADIENT_DEPTH_POINT=<index>` swaps the point without
  regenerating anything. Whatever wins becomes the default via
  `fit --operating-point Q --points ...`, and this section records the measurement
  that chose it.

### The default, by map size

`p90`, held out by game:

| Map | Lifetimes | Hit rate | Coverage | Field CPU | Owner saving |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64x64 | 24,996 | 0.992 | 0.999 | 1.501 | 0.998 |
| 128x128 | 99,667 | 0.947 | 0.976 | 1.569 | 0.985 |
| 256x256 | 198,254 | 0.865 | 0.884 | 1.882 | 0.933 |
| all | 322,917 | 0.900 | 0.923 | 1.842 | 0.940 |

The provisional pipeline rule, `max(32, previous * 5/4)`, scored on all lifetimes
rather than held out, saves 0.547 at 1.281x field CPU. Eager saves everything at
2.405x.

## What does not predict depth

Screened on its own, at the same budget, against 0.338 for no key at all:
`previous` 0.542, `type` 0.473, `progress` 0.381, `construction` 0.357, `is_site`
0.355, `buildings` 0.350, `units` 0.344, **`size` 0.341**, `level` 0.340, `route`
0.336 and `swim` 0.335.

**Map size does not earn a place.** The size-normalised target (depth per tile of
width + height) scored 0.312 alone, worse than the raw depth. Larger maps do need
deeper fields on average, but within a game the spread between buildings is far
larger than the difference between map sizes, and `previous` already carries
whatever the size implies. Team unit and building counts are likewise absorbed by
the field's own history.

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
  --keys previous type progress --operating-point 0.9 --points 0.8 0.95
# The committed default's metrics, overall and per map size.
python3 tools/gradient_depth_fit.py evaluate dataset.json.gz
# Owner saving against field and estimated process CPU, per operating point.
python3 tools/gradient_depth_fit.py evaluate dataset.json.gz --curve [--share busy_oazis=0.067]
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
  estimates from the depth histograms, not measured thread time. Beyond a partial
  field's reach the work is extrapolated from complete fields.
- The tournament draw is 64 games across 25 generators, so per-generator behaviour
  is not resolved. Large 256x256 games, busy Oazis in particular, carry most of
  the lifetimes and most of the weight.
- `previous` needs the depth the field's last lifetime settled, which the scheduled
  pipeline keeps as an owner-only hint. A field built for the first time uses the
  `-1` (no history) row.
- The process-CPU column is an estimate: a measured search share times the
  histogram-based field ratio. Each operating point's real cost is measured by the
  scheduled pipeline's timing runs.

## Related

- [Building field statistics](development/performance-telemetry.md#building-field-statistics)
  are the rows this is fitted on.
- [Distributed tournaments](tools/tournaments.md) describe the `gradient_depth`
  campaign.
- [The win probability model](win-probability-model.md) is fitted the same way:
  dataset, screen, fit, then a generated header.
