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
| `barracks_levels` | warrior training buildings and their sites, weighted from 1 (a level-1 site) to 6 (a finished level-3 building) | +0.0364 | 0.32 |
| `explorers` (sqrt) | living explorers | +0.205 | 0.27 |
| `attack` (share) | its share of all attack strength | +0.904 | 0.25 |
| `feeding` (sqrt) | units eating at an inn | +0.161 | 0.24 |
| `delivered_window` (share) | its share of resources delivered over the last 2,048 ticks | +0.851 | 0.21 |
| `swarm_filling_window` (sqrt) | worker-ticks spent filling swarms over the last 2,048 ticks | +0.0024 | 0.17 |
| `inn_walk_distance_window` (share) | its share of the distance walked to inns over the last 2,048 ticks | +0.268 | 0.08 |

Every coefficient is positive. Military investment (barracks, attack), scouting
and a fed, working economy each carry weight; no single term dominates. The
windowed terms describe what a colony has been doing recently rather than what
it owns, which the previous model, reading only current counts, could not see.

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

1,716 games, sampled every 512 ticks, giving 690,277 training samples, played at
SIM revision 155. The games are a random draw over all eight AIs, three formats
(1v1, 2v2, and free-for-all with 4 or 8 colonies), every playable map generator
and both 128x128 and 256x256 maps. They ran with `--win-probability 0` to a
180,000-tick cap, twice the default, so that most games reach a real result:
13% hit the cap and are labelled by adjudication.

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

Cross-validated, grouped by game: McFadden R² 0.384, top-1 accuracy 75.0%
against 41.2% for guessing. On the same games the previous six-term model scored
0.351 unchanged and 0.375 refitted.

### How the terms were chosen

Candidates were every `TeamStat` measurement plus about 190 aggregates of the
gameplay measurements, each also as 2,048- and 8,192-tick windows of its
cumulative counters, under the identity, share and square-root transforms. Greedy
forward selection (one term per family) produced a first set; stepwise refinement
then tried dropping each term, swapping it for any other candidate or transform,
and adding new ones, until no change improved cross-validated fit by 0.0005. The
refinement replaced worker share, which had come in with a negative sign, by
attack share, and total hit points by units eating.

A gradient-boosted model on every candidate reaches about 0.42, so there is
headroom, but it cannot be evaluated in integer arithmetic. A learning curve on a
fixed held-out set shows the seven-term model is flat from about 100 training
games, so more data would not improve it; the boosted model was still improving
at 1,373.

### It is calibrated, and that is what matters here

A winning condition needs the number to mean what it says, not merely to rank
correctly. Held out by game, predicted and observed win rates:

| Predicted | 0.010 | 0.035 | 0.063 | 0.096 | 0.131 | 0.179 | 0.250 | 0.361 | 0.508 | 0.687 | 0.858 | 0.968 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Observed | 0.007 | 0.022 | 0.039 | 0.066 | 0.119 | 0.173 | 0.266 | 0.375 | 0.533 | 0.714 | 0.864 | 0.966 |

The top bucket, where the threshold lives, reads 0.968 predicted against 0.966
observed. Mid-range predictions are slightly cautious and the lowest buckets
slightly overstate a losing side's chances; neither affects a call at 95%.

## Why the arithmetic is all integers

The winning condition runs inside the synchronised simulation and decides the
outcome. Two machines that disagree by one bit would end the same game on
different ticks, which desynchronises a network game and diverges a replay.
Floating point cannot promise agreement across platforms, because `exp` and `log`
come from the platform's library.

So there is no floating point in the decision path:

- Selection was restricted to transforms with exact integer forms — identity, a
  division (`share`) and a square root. `log1p` is excluded, and the emitter
  refuses to emit any transform it cannot do exactly rather than quietly falling
  back on an approximation.
- Coefficients are emitted twice — as a `double` for display and reporting, and
  as a fixed-point integer rounded **at generation time**, so the rounding is the
  fit's rather than a compiler's.
- The softmax uses a hand-rolled fixed-point exponential: the argument is reduced
  by whole multiples of ln 2 and the remainder evaluated as a fixed series, in
  integer add, multiply, shift and divide only. It agrees with `libm` to 6e-10.
- Scales are chosen so one 64-bit multiply per term cannot overflow given clamped
  inputs, which avoids 128-bit arithmetic and the portability question with it.

The features are values the simulation computes at fixed sample deadlines:
`TeamStat` (explorers, attack) and the published gameplay measurements (everything
else). `WinProbability::teamInputs` reads the latest retained periodic measurement
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
two digits from 00 to 99 (a certain win reads 99), evaluated live from the
current scene rather than the 512-tick samples. Only their own figure is shown,
so it says how the match is going without itemising any opponent.

The end-game metric picker includes **Win probability**, on a fixed 0–100% axis.
Its history is sampled every 512 ticks and includes a final partial interval.
Allies share one prediction. An early victory retains the probability that
triggered the rule, rather than showing a manufactured 100% after its losses are
assigned. Saved games preserve the trace; saves predating format 190 show only
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
| **95% (tournament default)** | **25.7%** | **1.7%** (18 of 1,090) |
| 97% | 18.8% | 1.6% (13 of 814) |
| 99% | 8.4% | 0.5% (2 of 376) |

Measured over the 1,716 games, cross-fitted so no game is scored by a model that
saw it, with three consecutive qualifying samples required. Errors sit below
what each threshold implies; the confirmation requirement removes most one-sample
swings.

## Limits

- The model was fitted on AI games only. Human play may distribute these
  quantities differently, and nothing here has been checked against it.
- It is fitted on the whole random draw, so it describes the average game across
  every generator and size rather than any particular map. Per-generator
  behaviour has not been broken out.
- It reads seven numbers. Position, terrain, who is adjacent to whom and what is
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
  terms are read from.
- [AI telemetry](../telemetry.md) — per-AI internals, likewise excluded.
- [Distributed tournaments](../../tools/tournaments.md) — where the calibration games come from.
- [Headless runs](../../development/headless-replays.md#probability-based-early-victory) — enable the rule with `--win-probability`.

Related: [AI documentation](../README.md).
