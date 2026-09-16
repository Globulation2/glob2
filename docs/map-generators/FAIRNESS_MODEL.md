# The fairness model

A generated map is fair when no colony starts with a better chance of winning than any other.
This is how that chance is estimated, where the numbers behind it come from, and how to
re-derive them when there are more games to learn from.

The score it replaces weighed six normalised factors with coefficients nobody measured —
0.22 for the walk to wheat, 0.25 for how well the ground regrows, and so on — and called the
worst colony's share of the best one "fairness". Nothing in that equation came from a game.

## What the model is

`MapGeneration::scoreStarts` measures each colony on the finished map: the walk to every
resource, what stock stands within 12, 24 and 48 walking steps, how much of it no rival
reaches sooner, buildable room, territory held outright and territory contested, ground
fertility, and the distance to the nearest and farthest rival. Those measurements are
unchanged; what they are *worth* is now fitted.

Every start gets a **fitness**

```
F_i = intercept + sum_k coefficient_k * transform_k(measurement_k(start_i))
```

and the chance that start *i* wins its map is the softmax over that map's colonies:

```
P_i = exp(F_i) / sum_j exp(F_j)
```

**Fairness** is one minus the Gini coefficient of those probabilities, normalised by the
`(n-1)/n` ceiling a raw Gini cannot exceed so that two colonies and eight are read off the
same scale:

```
fairness = 1 - Gini(P) * n / (n - 1)
```

It is 1 when every colony is exactly as likely to win as any other, and 0 when one colony
would take the map. That number is what the lobby keeps its best candidate roll by
(`GenerationService::bestSeed`), what the pre-game preview shows, and what
`canonical_quality.score` reports.

### What the fitness does and does not mean

Softmax is invariant to adding the same constant to every fitness on a map, so games can
only ever determine **differences** between colonies, never the absolute level. The scale is
determined — a coefficient twice as large really does separate colonies twice as sharply —
but the zero is a convention. The generated header anchors it so that the mean fitness over
the fitted starts is zero, which makes a fitness readable as "better or worse than a typical
start" and nothing more.

That matters for one intended use. Fairness, being a function of differences alone, is fully
determined by the games. An *absolute* reading — "this generator gives everyone good
positions" — rests on the anchoring convention and on the linear form holding away from the
data, and no game in the tournament tested it. Treat mean fitness as a comparison between
maps measured the same way, not as an absolute verdict.

## Where the coefficients come from

`tools/fairness_model.py` runs a tournament of its own and fits the model to it.

Each match draws a generator at random from every playable one, a colony count from two to
eight, a map size, and a value for each of that generator's controls (a quarter of maps keep
the generator's defaults; on the rest each control is re-rolled independently with
probability 0.7). Crucially **every slot plays the same AI** — one of nicowar, castor, numbi
or maxima, drawn per match — so what separates the players is their starting position and
not the opponents they drew. Candidate selection is off: the fit needs the raw spread of
starts a generator produces, including the lopsided rolls the lobby would reject.

Each game is one self-contained job: the engine generates the map inside the game process and
embeds the full map report in the result, so there are no artifacts to move between hosts and
no job that can be blocked by another.

The fit is a **conditional logit** (McFadden), maximising the Plackett-Luce likelihood of the
finishing order with the Breslow treatment for ties. Using the whole order rather than the
winner alone extracts far more from each game: a four-colony game distinguishes 24 orderings
instead of 4. Games stopped at the tick cap are ranked by the engine's own adjudication —
prestige, then population, then finished buildings — exactly as the existing fairness
tournament ranks them.

Selection is greedy forward selection on cross-validated McFadden R², with maps (not games)
as the cross-validation unit, so two games of the same map never straddle a fold. Candidate
transforms are `identity`, `log`, `sqrt` and `share` (a colony's cut of the map's total) for
extensive measurements, and `identity`, `log`, `sqrt` and `decay24` for distances. `log` is
the transform that makes a term scale-free: a difference of logs is a ratio between colonies,
so the same coefficient means the same thing on a 64x64 map and a 512x512 one.

## Running it

Build the client, register a bundle for the hosts, then:

```sh
python3 tools/fairness_model.py run artifacts/fairness-model --hosts hosts.json \
    --bundle BUNDLE_DIR --games 6800
python3 tools/fairness_model.py status artifacts/fairness-model
python3 tools/fairness_model.py extend artifacts/fairness-model --hosts hosts.json \
    --bundle BUNDLE_DIR --games 4000      # another round, same directory
python3 tools/fairness_model.py play artifacts/fairness-model --hosts hosts.json
```

`hosts.json` is the ordinary [distributed tournament](../tournaments.md) host list. Rounds
live side by side under one directory and every round counts towards the fit; a round pinned
to an older worker package is still read, it just cannot be played further from here.

Then fit:

```sh
# Development: screen every measurement and transform, then select
python3 tools/fairness_model.py fit artifacts/fairness-model --mode explore

# Production: fit the screened list and regenerate the C++ header
python3 tools/fairness_model.py fit artifacts/fairness-model --mode final \
    --revision "$(git rev-parse --short HEAD)"
```

`explore` writes `analysis/explore.md` and `.json`: every measurement's single-term
cross-validated R², the selection trace, cluster-bootstrap coefficient intervals, the same
model refit on slices of the games, a calibration table and the fairness spread of the drawn
maps. `final` fits `FINAL_FEATURES` and writes
`src/map/generator/shared/FairnessModel.h` — the coefficients, the fitness function they
belong to, and per-term accessors for the lobby's breakdown screen. Nothing in C++ needs
editing when the fit changes, including when it selects different measurements.

`test/test_map_report.py` re-derives the fitness, win probabilities and fairness in Python
from the report's own raw measurements and its published coefficients, and requires the
engine to agree exactly. That is what keeps the two implementations from drifting.

One generator's maps depend on the coefficients. Lava shield ranks its own settlement
proposals by the map score (`chooseScoredSettlements`), so refitting changes which town
layout it picks on some seeds. After a refit, re-run
`build/src/MapGeneratorGoldenTest PROFILE --update --force` and commit the rows; `--force`
because a refit is new data for a shared score, not a new revision of that generator.

The Linux rows can only be regenerated on Linux (or copied from the `--print` output in a CI
log), so after a refit CI stays red for Lava shield until that is done.

## Testing a new measurement without another tournament

Every game stored the generator, every control value and the map seed it used, and generation
is deterministic, so the tournament's maps can be rebuilt exactly and measured again:

```sh
python3 tools/fairness_model.py remeasure artifacts/fairness-model --jobs 6
```

It rebuilds every played map with `--report diagnostics`, joins the measurements to the games
already played, and reports what each adds to the fitted model — about six minutes for 1,330
maps, no games. Four layers of measurement sit beside the model's own ColonyQuality fields:

| Layer | Prefix | Source | In the model? |
| --- | --- | --- | --- |
| Start diagnostics | `diag_` | `StartDiagnostics.h`, `--report diagnostics` | No |
| Movement | `move_` | the map report's walking, clearing and swimming sections | No |
| Generator telemetry | `tel_` | per-colony records the generator made while building | No |
| Derived composites | `d_` | arithmetic over ColonyQuality fields | Wheat decayed, yes |

The diagnostics and the movement, telemetry and derived layers are kept for diagnosing play —
why an AI does well or badly on a kind of terrain — rather than for scoring. Each was measured
against every other and against ColonyQuality over 13,200 colonies, and a measurement whose rank
correlation with one already kept reached about 0.9 was removed, keeping the most refined form:

- **Movement** keeps what clearing and swimming *add* over walking, the swimming distance to the
  nearest rival, rival centrality and fronts, and whole-map reachable wheat and wood. Walking
  reachability, territory and rival distances were exact copies of ColonyQuality fields (1.000),
  and clearing tracked walking at 0.91–0.99.
- **Diagnostics** keep regrowth capacity, trip-weighted harvest throughput for wheat, wood, stone
  and fruit, inn sites next to grain, second-swarm room, the forest's front and the build sites
  on it, contested wheat and choke width. Harvest frontage (a re-count of catchment deposit
  tiles), the field's front (identical to regrowth capacity), a second-swarm distance that never
  left its floor, and the raw forest-front tile count went.
- **Derived** keeps four distinct ideas: decayed wheat, wheat per nearby rival, wheat against the
  best-fed rival, and the scarcest input. Rank, gap and head-to-head forms of the rival
  comparison ran at 0.98 with each other; decayed wood and room, pressure and contested fraction
  re-derived existing fields at 0.95–0.997.

### What does not predict winning

Measured on 2,644 rebuilt games, cross-validated McFadden R² alone and gain on the model:

| Idea | Alone | Gain |
| --- | ---: | ---: |
| Wheat weighted by distance (adopted) | +0.0233 | +0.0031 |
| Regrowth capacity of the wheat field | +0.0075 | +0.0006 |
| Harvest throughput (trip-weighted gathering edges) | +0.0064 | +0.0002 |
| Forest front versus field front | +0.0045 | +0.0002 |
| Best of 43 whole-map movement measurements | +0.0055 | +0.0013 |
| An inn site next to grain | +0.0026 | +0.0007 |
| Best of 228 generator telemetry records | +0.0020 | — |
| Room for a second swarm; choke width to the nearest rival | ~0 | ~0 |
| Forest overgrowing the base | +0.0001 | 0 |

Boosted trees over every measurement above reach pairwise 0.595 on final placing, against
0.589 for a linear model over the same inputs: the limit is what is measured, not how it is
combined. A single shallow tree looked better only because it ties most colonies and a
concordance that skips ties grades it on the easy pairs.

### How much there is to find

Each map was played twice with different game seeds, so how often the same colony comes out
ahead in both measures how much the map itself decides:

| Colonies ahead on | Replicate agreement | Best model over all measurements |
| --- | ---: | ---: |
| Units at 5k ticks | 0.816 | 0.712 |
| Units at 10k ticks | 0.772 | 0.690 |
| Final placing | 0.652 | 0.595 |

The map largely decides the opening, and most of what it decides is still unmeasured. Training
the fitness on the early economy instead of the winner does not help predict winners.

### Causes, not correlations

`--perturb KIND:TEAM:RADIUS` edits one colony's start after generation (removing wheat, wood or
stone within a walking radius, without drawing a random number), so the same map can be played
from the same game seed with and without the edit. Runs are deterministic, so every difference
is the edit's. Over 80 maps and three seeds, twelve thousand ticks each:

| Edit to colony 0 | Its unit share at 5k | at 10k |
| --- | ---: | ---: |
| Remove wheat within 12 steps (median 23 deposits) | −21% | −21% |
| Remove wheat within 24 steps (median 48 deposits) | −24% | −28% |
| Remove wood within 24 steps (median 39 deposits) | +1% (not significant) | +1% |

Wheat near the door carries most of the opening; the next ring matters more as the game goes on,
which is what the distance-decayed term encodes. Wood near the start does not constrain the
opening at all, so the model's negative wood coefficient has no mechanism in it.

## How many candidate rolls to keep

The lobby generates `GenerationService::kSampledCandidates` rolls and keeps the fairest.
`bestSeed` records every attempt, so the best-of-k curve for any k can be read off one run of
32:

```sh
python3 tools/fairness_model.py sampling artifacts/fairness-sampling \
    --catalog catalog.json --seeds 24 --candidates 32
```

It reports, per generator and shape, the fairness the kept roll reaches at each k and the
cumulative generation time, and recommends the smallest k that buys 80% of the available gain
inside a 500 ms budget for the whole search.

## Limits

- **The AI is the measuring instrument.** A map that is fair for these four AIs can still be
  unfair for a person, and their habits — how they expand over water, how early they fight —
  shape what counts as a good start. The explore report refits per AI so that disagreement is
  visible rather than assumed away.
- **Most of a game is not the start.** A start position is one input to a long game between
  equal opponents; the rest is the play. A small McFadden R² is the honest ceiling here, not a
  failure of the fit, and it is the right number to compare models by.
- **Randomised controls are not the maps players see.** The tournament deliberately samples
  beyond what a generator produces at its defaults, so the model is fitted over a wider range
  of starts than a lobby game will ever offer. That is what gives the coefficients something
  to separate; it also means the drawn maps' fairness distribution is worse than a player's.
- **Fairness says nothing about quality.** A map where every colony is equally starving scores
  1. Roll selection maximises fairness alone, by decision; absolute quality is not in it.
