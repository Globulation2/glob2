# The win probability model

## On this page

- [What the model is](#what-the-model-is)
- [Where the coefficients come from](#where-the-coefficients-come-from)
- [Why the arithmetic is all integers](#why-the-arithmetic-is-all-integers)
- [Running it](#running-it)
- [The winning condition](#the-winning-condition)
- [Limits](#limits)
- [Related](#related)

## What the model is

Every competitor still standing gets a fitness

    F_i = intercept + sum_k coefficient_k * transform_k(state_k(competitor_i))

and its probability of winning is `softmax(F)` over the competitors. Allies are
one competitor, because they win or lose together.

The fitted terms, ordered by how much a typical gap between rivals on that term
moves the odds (coefficient times the within-contest standard deviation):

| Term | Reads | Coefficient | Typical gap's effect |
| --- | --- | ---: | ---: |
| `attack` (share) | its share of all attack strength | +0.907 | 0.27 |
| `barracks_levels` | warrior training buildings and their sites, weighted from 1 (a level-1 site) to 6 (a finished level-3 building) | +0.0244 | 0.23 |
| `delivered_window` (share) | its share of resources delivered over the last 2,048 ticks | +0.950 | 0.23 |
| `feeding` (sqrt) | units eating at an inn | +0.139 | 0.19 |
| `prestige_needed` (log1p) | prestige it would still have to add to win by prestige | −3.70 | 0.17 (1.15 where it differs) |
| `swarm_filling_window` (sqrt) | worker-ticks spent filling swarms over the last 2,048 ticks | +0.0025 | 0.16 |
| `explorers` (sqrt) | living explorers | +0.107 | 0.15 |
| `inn_walk_distance_window` (share) | its share of the distance walked to inns over the last 2,048 ticks | +0.186 | 0.06 |

Military investment (barracks, attack), scouting and a fed, working economy each
carry weight; no single term dominates. The windowed terms describe what a colony
has been doing recently rather than what it owns.

`prestige_needed` is the larger of two distances: what the match's combined
prestige still lacks to reach the prestige goal, and what the colony needs to pass
every other surviving colony (`WinProbability::prestigeNeeded`; an alliance takes
its closest member). Its coefficient is the only negative one, because the
distance shrinks as a colony closes in. It is the same for every side until
someone earns prestige, so it cancels in about 98% of samples; where it does
differ it is the strongest term. Its job is smoothness: without it the model can
favour one colony right up to the moment another wins by prestige.

### What the fitness does and does not mean

Softmax fixes the scale of `F` but not its zero, so the absolute level is a
convention and cancels between competitors. Only differences carry meaning. The
intercept is carried for reporting only.

The model is a *description of how these games went*, not a theory of the game.
It says that colonies with upgraded barracks, most of the attack strength and a
busy supply chain went on to win; it does not say that building barracks causes
victory. A term can be a
symptom rather than a cause.

## Where the coefficients come from

501 games, sampled every 512 ticks, giving 218,109 training samples, played at
SIM revision 159 with the prestige-needed statistic recorded. The games are a random draw over all eight AIs, three formats
(1v1, 2v2, and free-for-all with 4 or 8 colonies), every playable map generator
and both 128x128 and 256x256 maps. They ran with `--win-probability 0` to a
180,000-tick cap, twice the default, so that most games reach a real result:
18% hit the cap and are labelled by adjudication. Nine (1.8%) ended in a prestige
victory and 210 earned some prestige.

Each 512-tick sample is one contest: the surviving competitors are its entries,
their live state is the evidence, and the finishing order the game eventually
reached is the label. That is deliberately the same estimator the fairness model
uses (`tools/conditional_logit.py`), so there is one conditional logit in the
tree and two models on top of it.

Three choices matter:

- **Labels come from the end of the game, not the sample.** That is the whole
  point: what we want to predict at tick T is the result that only arrives later.
- **Games stopped at the tick cap are labelled by the same economic adjudication
  the ratings use.** Excluding them would drop the games that are hardest to
  call, which are exactly the ones this is for.
- **Cross-validation groups by game.** A game's hundreds of samples are near-identical
  views of one outcome; splitting them across folds would let the fit see its own
  test set and report an accuracy nobody could reproduce.

Cross-validated, grouped by game: McFadden R² 0.306, top-1 accuracy 72.2%
against 41.9% for guessing. The seven terms without `prestige_needed`, refitted on
the same games, score 0.306 and 72.2%: the new term is not meant to move the
average, and does not. The previous coefficients, fitted on 1,716 games at
revision 155, score 0.315 on these games. Refitting on fewer current games costs a
little overall fit; the full refit was preferred over keeping those coefficients
and adding one term, so the model is one consistent fit on the current simulation.

### How the terms were chosen

Candidates were every `TeamStat` measurement plus about 190 aggregates of the
gameplay measurements, each also as 2,048- and 8,192-tick windows of its
cumulative counters, under the identity, share and square-root transforms. Greedy
forward selection (one term per family) produced a first set; stepwise refinement
then tried dropping each term, swapping it for any other candidate or transform,
and adding new ones, until no change improved cross-validated fit by 0.0005. The
refinement replaced worker share, which had come in with a negative sign, by
attack share, and total hit points by units eating.

`prestige_needed` was added afterwards by design rather than by selection, to
remove the late swings in games won by prestige. Its transform was chosen among
identity, square root, log1p, x^0.25, x^0.75 and the square by how much each
removed those swings without costing overall fit. The concave transforms did
best: log1p cut the mean end-of-game jump in prestige-won games from 0.146 to
0.085 (fitted without those games), against 0.121 for identity and 0.135 for the
square, and the earlier campaign's games showed the same order. The two
prestige-won games it does not change were true coin flips, both sides one
school short of the goal. Wherever one side needs strictly less than every
other it went on to win every time in this campaign, so the coefficient is held
back by the fit's ridge penalty rather than by the data; a larger one would turn
each prestige award into a jump.

A gradient-boosted model on every candidate reaches about 0.42, so there is
headroom, but it cannot be evaluated in integer arithmetic. A learning curve on a
fixed held-out set shows the seven-term model is flat from about 100 training
games, so more data would not improve it; the boosted model was still improving
at 1,373.

### It is calibrated, and that is what matters here

A winning condition needs the number to mean what it says, not merely to rank
correctly. Held out by game, predicted and observed win rates:

| Predicted | 0.019 | 0.056 | 0.088 | 0.117 | 0.149 | 0.199 | 0.276 | 0.384 | 0.516 | 0.672 | 0.826 | 0.949 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Observed | 0.024 | 0.046 | 0.054 | 0.084 | 0.139 | 0.204 | 0.287 | 0.411 | 0.545 | 0.707 | 0.819 | 0.931 |

The top bucket, where the threshold lives, reads 0.949 predicted against 0.931
observed: slightly overconfident, unlike the previous fit (0.968 against 0.966).
The seven-term refit on the same games is the same, so this reflects the current
simulation rather than the new term. Mid-range predictions are slightly cautious
and the low buckets overstate a losing side's chances.

## Why the arithmetic is all integers

The winning condition runs inside the synchronised simulation and decides the
outcome. Two machines that disagree by one bit would end the same game on
different ticks, which desynchronises a network game and diverges a replay.
Floating point cannot promise agreement across platforms, because `exp` and `log`
come from the platform's library.

So there is no floating point in the decision path:

- Transforms have exact integer forms — identity, a division (`share`) and a
  square root. A measurement with a hard upper bound may use any transform
  through a lookup table: `prestige_needed` cannot exceed the largest prestige
  goal (150 × 16 teams = 2,400), so the emitter writes `log1p` of every integer
  up to it into the header in fixed point (`TABLE_SHIFT`), and the engine and the
  fitter read the same table. The emitter refuses any other transform it cannot
  do exactly rather than quietly falling back on an approximation.
- Coefficients are emitted twice — as a `double` for display and reporting, and
  as a fixed-point integer rounded **at generation time**, so the rounding is the
  fit's rather than a compiler's.
- The softmax uses a hand-rolled fixed-point exponential: the argument is reduced
  by whole multiples of ln 2 and the remainder evaluated as a fixed series, in
  integer add, multiply, shift and divide only. It agrees with `libm` to 6e-10.
- Scales are chosen so one 64-bit multiply per term cannot overflow given clamped
  inputs, which avoids 128-bit arithmetic and the portability question with it.

The features are values the simulation computes at fixed sample deadlines:
`TeamStat` (explorers, attack), live prestige (`prestige_needed`, read after
`prestigeSyncStep` on the decision tick and recorded with it) and the published
gameplay measurements (everything else). In a match without a prestige goal,
`prestige_needed` is 0 for every side and cancels. `WinProbability::teamInputs` reads the latest retained periodic measurement
sample and the one `ACTIVITY_WINDOW_SAMPLES` (4) samples earlier. A sample is
published after its source tick, so at a decision tick T the latest is the sample
for T − 512; the fitter joins that same sample to the decision. Readings are taken
from the measurement history, per team, before allies are summed: an eliminated ally then
contributes no recent activity rather than removing its whole history from the
alliance. The history is saved, so a loaded game reads the same windows; a save
whose coverage starts later counts the window from coverage start. Because a
winning condition acts on these values, the routine statistics checksum covers
them (`teamStatsCoreChecksum`), not only full verification. Per-AI telemetry is
still excluded.

The fitting tool extracts the same values from `GLOB2_TL`, `GLOB2_ECON` and
periodic `GLOB2_MEASURE` records, with the same window rule, so the training
features and the in-engine features are the same quantities.

`tools/win_probability_model.py` mirrors the same integer algorithm in Python, so
`test/test_win_probability_model.py` asserts the engine and the fitter agree
*exactly* rather than closely, against recorded engine output.

## Running it

Collect games with `--win-probability 0` and `team-timeline` telemetry with
gameplay capture (tournament `outputs` `{"telemetry": ["team-timeline"],
"log_mode": "gameplay"}`; long free-for-all logs need a larger
`limits.log_bytes`). Then, from Python:

```python
from tools import win_probability_model as W

cache = W.build_dataset([RESULTS_DIR], 'dataset.json.gz')   # per-sample dataset
data = W.contests(cache)
W.screen(data)                                              # each candidate alone
model = W.fit_model(data, W.FINAL_FEATURES)                 # tools.conditional_logit
model['games'], model['samples'] = len(cache['games']), sum(len(g['entries']) for g in data['games'])
W.emit_header(model, W.HEADER_PATH, provenance_lines)
W.summarise_savings(W.savings(cache, W.FINAL_FEATURES, threshold=0.95))
```

Needs numpy and scipy for the fit; the arithmetic and the emitter do not.

Regenerating the header must be a no-op when nothing has been refitted, and a
test asserts it.

## The winning condition

Off by default in the game UI. The structured tournament CLI (`game run`) and
standard Python experiment planners use 95% by default, with an explicit zero
opt-out. Loaded games retain their saved conditions. Probability statistics are
collected automatically in every game regardless of whether the rule is enabled;
collecting them does not give the model a say in the outcome.

Turned on from the custom game rules, the match ends as soon as the model is
sure enough. It is evaluated on the 512-tick boundary `TeamStats` already samples
at — the cadence it was fitted on — and never before tick 5,120, since the
opening samples can look lopsided for reasons that mean nothing. It is placed
last among the winning conditions, so an actual elimination or prestige win is
always the reason a game ended when one is available on the same tick.

Live spectators can open an additional, last statistics page showing each side's chance,
whether the rule is on or off. The separate page fits up to sixteen colonies. Allies share one figure, because the model
rates the alliance. Players do not see the panel during a normal game.
They do see their own side's chance in the statistics header, beside a trophy:
two digits from 00 to 99 (a certain win reads 99), the chance the simulation
recorded at its latest 512-tick sample. Both panels renormalise over the sides
still standing, so an elimination between samples is reflected at once. Only
their own figure is shown,
so it says how the match is going without itemising any opponent.

The end-game metric picker includes **Win probability**, on a fixed 0–100% axis.
Its history is sampled every 512 ticks and includes a final partial interval.
Allies share one prediction. The trace only ever holds the model's own
predictions: the final point, and any sample after prestige, conquest or a script
has decided the game, repeats the last periodic prediction rather than
re-evaluating with the losers removed, which would manufacture a sudden 100%. An
early victory retains the probability that triggered the rule. Saved games preserve the trace; saves predating format 190 show only
the newly observed portion after loading.

Headless results automatically include the trace and an exact integer
trapezoidal-area summary. Detailed `GLOB2_WINPROB` log records still require
`--telemetry team-timeline`; the automatic summary does not. The tournament
framework uses final probability for its default cap adjudication and reports
the adjusted trajectory score (`c=2` log time plus outcome credit) separately
from ratings. Probability-stop credits are labelled predicted; the raw model
trace and linear area remain unchanged. See
[whole-game scores](../../tools/tournaments.md#whole-game-probability-scores).
The calibration labels and measurements above use the historical economic
adjudication for capped games, not this default.

When validating or refitting the probability model itself, collect games with
`--win-probability 0` (tournament setting `win_probability_permille: 0`) and use
independent outcome labels. The fitting helpers reject probability-ended games
and `win_probability` adjudication, including in previously cached datasets:
training or measuring calibration against the model's own calls would make its
reported accuracy misleading. The extraction default remains the historical
`prestige` policy for tick-capped games; natural engine victories remain authoritative.

The threshold is the only dial:

| Threshold | Compute saved | Calls on the wrong side |
| --- | ---: | ---: |
| **95% (tournament default)** | **18.8%** | **3.8%** (10 of 265) |
| 97% | 12.8% | 2.2% (4 of 180) |
| 99% | 4.6% | 0% (0 of 77) |

Measured over the 501 games, cross-fitted so no game is scored by a model that
saw it, with three consecutive qualifying samples required. Errors sit below
what each threshold implies; the confirmation requirement removes most one-sample
swings.

## Limits

- The model was fitted on AI games only. Human play may distribute these
  quantities differently, and nothing here has been checked against it.
- It is fitted on the whole random draw, so it describes the average game across
  every generator and size rather than any particular map. Per-generator
  behaviour has not been broken out.
- It reads eight numbers. Position, terrain, who is adjacent to whom and what is
  about to happen are all invisible to it, which is part of why a close game
  stays close in its eyes.
- The windowed terms see 2,048 ticks of activity. A colony that has just lost its
  supply chain still shows its earlier deliveries for up to four samples.
- The error rates above are measured against the *adjudicated* winner for capped
  games, so they inherit whatever the economic adjudication gets wrong.
- Refitting changes when games end. That is a gameplay change for anyone using
  the rule, and it invalidates comparisons with tournament results gathered under
  the previous coefficients.

## Related

- [The fairness model](../../map-generators/fairness-model.md) — the same estimator, on
  starting positions.
- [Gameplay statistics](../gameplay-statistics.md) — the measurements five of the
  terms are read from, and the prestige-needed chart.
- [AI telemetry](../telemetry.md) — per-AI internals, likewise excluded.
- [Distributed tournaments](../../tools/tournaments.md) — where the calibration games come from.
- [Headless runs](../../development/headless-replays.md#probability-based-early-victory) — enable the rule with `--win-probability`.

Related: [AI documentation](../README.md).
