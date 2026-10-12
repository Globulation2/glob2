# The building-field depth model

## On this page

- [The property that makes it safe](#the-property-that-makes-it-safe)
- [The model: a field's own past depths](#the-model-a-fields-own-past-depths)
- [Choosing the constants](#choosing-the-constants)
- [Where the numbers come from](#where-the-numbers-come-from)
- [Operating points](#operating-points)
- [When to refit](#when-to-refit)
- [Running it](#running-it)
- [Limits](#limits)
- [Related](#related)

## The property that makes it safe

**The depth only moves CPU between threads. It never changes a result.** Every
reader resolves the cell it reads first (`BuildingGradientSearch::resolve`), and
cells beyond the prepared depth are settled synchronously from the same frozen
search, with values identical to a full build. A different depth therefore
changes who pays for the search, never what any unit sees. **Refitting needs no
`SIM_REVISION` bump**, and replays, saves and network games are unaffected.

This is verified by comparing per-tick checksums of the same game under every
operating point, `GLOB2_BUILDING_DEPTH=full` and `lazy`
(`test/check_gradient_pipeline.py`, and the timing runs below).

## The model: a field's own past depths

When a stale field's refresh is staged, two depths of that same field are known
in O(1):

- **serving**: how deep readers of the field still serving required it, the
  `requiredCost()` of its search. It is the freshest sample of how far this
  field's readers reach.
- **previous**: the final reader-required depth of the lifetime it replaced,
  `Building::settledCostHint`.

Worker preparation does not contribute to either input. Each scalar query records
its required cost layer even when the worker already prepared the answer. Queries
for unreachable cells require exhaustion; seeded goals and blocked cells require
no propagation. Completing a field for serialization does not add reader demand.
Demand resets with each new search, so two lower-demand lifetimes allow the
prediction to shrink. Using prepared depth instead would feed speculative work
back into the next prediction and progressively overbuild active fields.

With `m` the deeper of the two, the depth is

```text
depth = offset + ((slope256 * m) >> 8)    clamped to [32, 65535]
depth = 32                               when neither is known
```

Each operating point is a name and two integers in the generated
`src/map/gradient/BuildingGradientDepthPolicy.h`, and
`Map::predictBuildingDepth` evaluates the expression above. Clearing and combat
fields, whose goals move, still settle fully; fields without a usable old value
are built synchronously and never reach the model.

Nothing in the model names a building type, a team or a map, so new buildings
need no refit. The fitted points settle a little beyond the deeper past depth:
the default (λ 0.1) prepares `166 + (292 * m >> 8)`, approximately
16.6 land tiles beyond 1.14 times the deeper past demand.

A field with neither depth prepares the minimum depth (32 cost units). In the
training games, that happened to 0.001% of scheduled refreshes; after a save is
loaded it happens to
every field once, because the past depths are not saved. Fitting a depth for
that case would send a burst of deep worker searches right after loading, for
fields about which nothing is known. Minimum preparation lets the readers decide,
and the field has a history from its next refresh.

## Choosing the constants

The objective is the trade the scheduled pipeline makes. For a depth D and a
lifetime that actually needed depth A:

- **owner saving** is the lazy search work a build to D moves off the owner:
  popped(min(D, A)) / popped(A);
- **extra CPU** is the total search work relative to lazy:
  popped(max(D, A)) / popped(A). Settling past a complete field's end costs
  nothing; beyond a partial field's reach, the mean profile of complete fields of
  the same map size estimates the cost.

Each operating point maximises owner saving minus λ times extra CPU, summed over
all training lifetimes. One λ for every field puts worker CPU where it moves the
most owner work: a small λ settles deeper, a large one stays close to the past
depth. The fitter searches integer `offset` and `slope256` on a grid and then
refines them; the depth with no history is the best single constant for those
lifetimes. Because the formula depends on a lifetime only through `m`, the fit
groups lifetimes by `m` on a 16-cost grid and scores depths on the same grid.

**Why a formula rather than a table.** Fit 2 was a quantile table keyed on the
previous depth, the team's unit count and the building type. Candidate inputs
were screened again on the first 354 games of the campaign below, as cells of a
table, held out by game, at 1.25x extra CPU:

| Inputs | Owner saving |
| --- | ---: |
| none (one global depth) | 0.497 |
| building type | 0.505 |
| type, level and construction site combined | 0.516 |
| serving depth (quarter-octave buckets) | 0.876 |
| serving and previous depth | 0.911 |
| + any of type, level, site, team units, workers, game time | at most +0.0005 |

A field's own history carries almost all the predictable signal. On all 1,472
games, the table over both depths was then compared with closed-form rules
fitted on the same objective:

| Rule | Saving at 1.2x CPU | 1.25x | 1.3x |
| --- | ---: | ---: | ---: |
| a + k · serving | 0.875 | 0.887 | 0.899 |
| **a + k · max(serving, previous)** | **0.914** | **0.926** | **0.934** |
| a + k · max(serving, w · previous) | 0.914 | 0.926 | 0.934 |
| a + k · max + c · min | 0.915 | 0.926 | 0.934 |
| k · max^b | 0.914 | 0.925 | 0.933 |
| bucket table (serving, previous) | 0.913 | 0.926 | 0.934 |

Taking the deeper of the two past depths is what matters: weighted sums of the
two lose about 3 points, and ignoring the previous depth about 4. Extra terms
gain nothing measurable, so the model is the two-constant rule.

## Where the numbers come from

Fit 3 is 1,472 games and 3,408,777 scheduled footprint lifetimes from a
`gradient_depth` [tournament](../../tools/tournaments.md) (`sample_seed` 3, 18,048
ticks, AI order delay 8, all eight AIs). Each game draws its map size first
(64, 128 or 256 tiles a side; 64 is duel-only), then a format and a generator
from those that generated at that size and colony count in a preflight: 25
generators at 64, 49 at 128 and 66 at 256. The campaign planned 2,400 games and
was stopped once the fit was stable; the remaining jobs were never run.

`--telemetry gradient-stats` writes one row per lifetime (see
[building field statistics](../../development/performance-telemetry.md#building-field-statistics)).
With the statistics on, scheduled fields are published with only their seeds
settled, so the depth a lifetime settled is the depth its readers needed. The
rows record the model's inputs as they stood when each lifetime's job was
staged (`staged`, `previous_hint`, `serving_settled`), which is exactly what the
engine reads; only staged footprint lifetimes are fitted.

Fit 2 keyed on the depth the previous lifetime finally settled, which the
engine never has when it stages a job: by then that lifetime is still serving,
and `settledCostHint` holds the one before it. On the first 354 games of this
campaign, fit 2's table as the engine runs it saved 0.781 at 1.22x extra CPU
(0.812 had its intended input been available).

## Operating points

Held out by game (5 folds), as `evaluate --curve` reports them:

| λ | Hit rate | Coverage | Extra CPU | Owner saving |
| ---: | ---: | ---: | ---: | ---: |
| 0.05 | 0.988 | 0.984 | 1.725 | 0.966 |
| 0.1 | 0.969 | 0.970 | 1.364 | 0.942 |
| 0.2 | 0.937 | 0.954 | 1.241 | 0.925 |
| 0.3 | 0.912 | 0.941 | 1.204 | 0.916 |
| 0.5 | 0.860 | 0.912 | 1.166 | 0.902 |
| 1.0 | 0.729 | 0.796 | 1.135 | 0.881 |
| eager | 1.000 | 1.000 | 2.814 | 1.000 |

Hit rate is the share of lifetimes settled at least as deep as they needed;
coverage weights it by resolve calls. A prediction just short of the needed
depth misses but still saves almost all of the work, which is why owner saving
stays high where the hit rate falls.

The header carries λ 0.1, 0.2, 0.3 and 0.5 (`l0100` to `l0500`); the default
is λ 0.1.
`GLOB2_BUILDING_DEPTH=<name>` selects one at run time for timing, alongside
`table` (the default point), `full` and `lazy`.

The default prioritizes keeping search off the simulation owner. On the original
lazy dataset, λ 0.1 offloads 94.15% of scheduled search work at 1.3641 times lazy
search work, compared with 90.21% at 1.1659 times for λ 0.5. These are histogram
estimates for building-field searches, not measured whole-game CPU ratios.

The coefficients were fitted on lazy lifetimes, so worker preparation did not
inflate their training inputs. Reader-demand tracking restores those intended
inputs in the running engine. Earlier runtime comparisons used prepared search
depth as history and could accumulate speculative depth across refreshes; they
cannot establish the tradeoff after removing that feedback. Changing λ does not
change gameplay, field values, publication deadlines or save/replay formats.

## When to refit

Refit whenever something changes how far units read building fields: fetching
rules, hiring, AI placement, the default AI order delay or the refresh
scheduling. Refitting moves only CPU, so it needs no `SIM_REVISION` bump.

| Fit | Trigger | Model | Default |
| --- | --- | --- | --- |
| 1 | first fit, round-trip fetching | quantile table: previous, type, progress | p80 |
| 2 | round-trip removal (greedy fetching) | quantile table: previous, units, type | p80 |
| **3** | **staged inputs, larger campaign; reader-demand history** | **a + k · max(serving, previous)** | **λ 0.1** |

## Running it

```sh
# Data: a gradient_depth tournament (see docs/tools/tournaments.md), plus any
# directories of plain game run outputs that hold gradient-stats.csv.
python3 -m tools.tournaments.gradient_depth plan depth.json --bundle BUNDLE --output planned.json
python3 -m tools.tournaments submit planned.json RESULTS --bundle BUNDLE
python3 -m tools.tournaments run RESULTS --hosts hosts.json

python3 tools/gradient_depth_fit.py dataset RESULTS RUNS --output dataset.npz --jobs 16
# Fit the operating points and regenerate the summary and the header.
python3 tools/gradient_depth_fit.py fit dataset.npz --operating-point 0.1 --points 0.2 0.3 0.5
# The default's metrics, overall and per map size; and the held-out curve.
python3 tools/gradient_depth_fit.py evaluate dataset.npz
python3 tools/gradient_depth_fit.py evaluate dataset.npz --curve [--share busy_oazis=0.026]
```

The dataset, fit and evaluate commands need numpy; on the fit-3 data each takes
about a minute or less (`dataset` with `--jobs 16`) on one machine. Regenerating
the header from the committed summary needs only the standard library.
`test/test_gradient_depth_fit.py` checks that doing so is a no-op and that the
C++ expression agrees with the fitter on random inputs; its fitting cases skip
without numpy.

## Limits

- It was fitted on AI games under AI order delay 8. Human players place and staff
  buildings differently, and nothing here has been checked against human play.
- The extra-CPU and saving figures come from the lifetimes' depth histograms,
  not measured thread time; beyond a partial field's reach the work is
  extrapolated from complete fields. The default favors owner offload; runtime
  measurements must include total CPU and synchronous fallback cost.
- The campaign has no busy 11-player games; large 256x256 games carry the most
  work. The timing checkpoints are busy Oazis games.
- A field whose readers suddenly reach much further than before (a burst of new
  workers, a new wall forcing a detour) is under-predicted for one lifetime; the
  owner settles the rest lazily, as it would without the model.

## Related

- [Building field statistics](../../development/performance-telemetry.md#building-field-statistics)
  are the rows this is fitted on.
- [Distributed tournaments](../../tools/tournaments.md) describe the `gradient_depth`
  campaign.

Related: [AI documentation](../README.md).
