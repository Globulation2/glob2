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
The predicted depth is the median (quantile 0.5) of the actual depth that cell's
lifetimes needed, clamped to [32, 65535] cost units (10 per land tile). A cell with
fewer than 40 training lifetimes backs off to the shorter key prefix. The empty
prefix always exists.

The selected keys, in the order selection added them, with the held-out owner
saving at the CPU budget after each one:

| Keys | Owner saving at 1.25x CPU |
| --- | ---: |
| none (one global median) | 0.338 |
| + `previous`: log2 bucket of the field's previous settled depth / 80 | 0.542 |
| + `type`: building type | 0.590 |
| + `progress`: material-delivery quartile on construction sites | 0.619 |

The next candidate, `swim`, added 0.007, which is below the 0.01 threshold, so
selection stopped there. The table is in
`src/map/gradient/BuildingGradientDepthPolicy.h` (361 integer rules, most specific
first, plus a constexpr lookup). The summary it is generated from is in
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

### Evaluation

The selected model, held out by game:

| Map | Lifetimes | Hit rate | Coverage | Extra CPU | Owner saving |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64x64 | 24,996 | 0.699 | 0.757 | 1.285 | 0.901 |
| 128x128 | 99,667 | 0.549 | 0.487 | 1.203 | 0.708 |
| 256x256 | 198,254 | 0.479 | 0.431 | 1.246 | 0.591 |
| all | 322,917 | 0.518 | 0.470 | 1.242 | 0.608 |

Other rules on the same lifetimes, scored on all of them rather than held out:

| Rule | Extra CPU | Owner saving |
| --- | ---: | ---: |
| this table | 1.240 | 0.610 |
| `max(32, previous * 5/4)`, the scheduled pipeline's provisional rule | 1.281 | 0.547 |
| full depth (eager) | 2.405 | 1.000 |

On busy Oazis alone, the table gives 1.278 extra CPU and 0.602 saving, against
1.341 and 0.539 for the provisional rule.

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
# Select, fit, and regenerate the summary and BuildingGradientDepthPolicy.h.
python3 tools/gradient_depth_fit.py fit dataset.json.gz --jobs 14
# The committed model's metrics, overall and per map size.
python3 tools/gradient_depth_fit.py evaluate dataset.json.gz
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
- The quantile and budget are choices: a higher quantile moves more work off the
  owner at more total CPU. `fit --budget` and `fit --keys ... --quantile` expose them.

## Related

- [Building field statistics](development/performance-telemetry.md#building-field-statistics)
  are the rows this is fitted on.
- [Distributed tournaments](tools/tournaments.md) describe the `gradient_depth`
  campaign.
- [The win probability model](win-probability-model.md) is fitted the same way:
  dataset, screen, fit, then a generated header.
