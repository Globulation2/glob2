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

    F_i(t) = intercept + sum_k (coefficient_k + slope_k * c(t)) * transform_k(state_k(competitor_i))

where t is the decision tick and c(t) = min(t, 180,000) / 180,000 runs from 0 at
the start of a game to 1 at the tournament cap the games ran to, and holds there.
Only `prestige_lead` has a slope; every other coefficient is constant (see
[the clock](#the-clock)). Its probability of winning is `softmax(F)` over the competitors. Allies are
one competitor, because they win or lose together.

The fitted terms, ordered by how much a typical gap between rivals on that term
moves the odds (coefficient times the within-contest standard deviation):

| Term | Reads | Coefficient | Typical gap's effect |
| --- | --- | ---: | ---: |
| `prestige_lead` (signed log1p) | its prestige less the most any surviving opponent holds | −0.103 at tick 0 → +0.297 by tick 180,000 | 0.68 with its late coefficient (1.10 where it differs) |
| `delivered_window` (share) | its share of resources delivered over the last 2,048 ticks | +1.499 | 0.30 |
| `feeding` (sqrt) | units eating at an inn | +0.203 | 0.24 |
| `school_levels` | schools and their sites, weighted from 1 (a level-1 site) to 6 (a finished level-3 school) | +0.0238 | 0.15 |
| `attack` (share) | its share of all attack strength | +0.599 | 0.12 |
| `prestige_needed` (log1p) | prestige it would still have to add to win by prestige | −1.252 | 0.10 (0.52 where it differs) |
| `swarm_filling_window` (sqrt) | worker-ticks spent filling swarms over the last 2,048 ticks | +0.0019 | 0.10 |
| `explorers` (sqrt) | living explorers | +0.0620 | 0.08 |
| `inn_walk_distance_window` (share) | its share of the distance walked to inns over the last 2,048 ticks | +0.191 | 0.05 |
| `barracks_levels` | warrior training buildings and their sites, weighted from 1 (a level-1 site) to 6 (a finished level-3 building) | +0.0042 | 0.03 |

A fed, working economy carries the most steady weight; late in a game a prestige
lead outweighs everything else, and military strength comes after the school
and prestige terms. The windowed terms describe what a colony has been
doing recently rather than what it owns.

Prestige comes only from finished level-3 schools (`school2` in the catalog), 50
each, so three terms describe it. `prestige_lead` is the steady advantage: the
leader's margin over the runner-up, or everyone else's deficit to the leader
(`WinProbability::prestigeLead`; an alliance takes its furthest-ahead member).
It is the only signed term, and it enters through an odd transform, so a lead
and an equal deficit mirror each other. `prestige_needed` is the end-game
distance: the larger of what the match's combined prestige still lacks to reach
the goal and what the colony needs to hold one school's prestige (50) more than
every surviving opponent (`WinProbability::prestigeNeeded`; an alliance takes its
closest member). Prestige arrives a whole school at a time, so passing the leader
takes a school, not a point. Both are 0 for every side when prestige cannot end
the game, so they cancel. `prestige_needed` differs between sides in only about
4% of samples, but where it does it is the strongest term; its job is
smoothness, so the model does not favour one colony right up to the moment
another wins by prestige. `school_levels` is the pipeline behind them: sites and
lower-level schools that have not paid out prestige yet.

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

1,029 games, sampled every 512 ticks, giving 518,019 training samples, played at
SIM revision 168. Only the four strongest AIs play: Maxima, Cortex, Nicowar and
Cabino. Numbi, Castor, Econo and Warrush are left out, because how weaker AIs
lose says little about how strong play is won. Half the games (514) are a random
draw of AIs, formats (1v1, 2v2, and free-for-all with 4 or 8 colonies), every
playable map generator and both 128x128 and 256x256 maps. The other half (515)
draw the same way except that every seat plays the same AI (`seat_sampling:
"mirror"` in the tournament planner), so differences between sides come from
the position and how the game went rather than from differing strategies. The
games ran with `--win-probability 0` to a 180,000-tick cap, twice the default,
so that most games reach a real result: 15% hit the cap and are labelled by
adjudication. 79% ended in conquest and 6% in a prestige victory; prestige also
decided 7% of games at the cap, where the winner held strictly the most of it,
so prestige settled about 13% of games in all (30% of eight-colony games).

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

Cross-validated, grouped by game: McFadden R² 0.295, top-1 accuracy 68.3%
against 40.2% for guessing. The same ten terms with constant coefficients score
0.288, and the previous eight-term model 0.266 with its shipped
coefficients and 0.268 refitted, so the two new terms add about 0.02 and the clock
on `prestige_lead` a little more. They help most where strategy is held equal: on
the mirror half, 0.238 against 0.206 for the refitted eight terms; on the
random half, 0.369 against 0.343. Mirror games are harder to call at all
(65% top-1 against 73%), which is what they are for: they keep the model
from learning which AI is playing instead of how well each side is doing.

### How the terms were chosen

Candidates were every `TeamStat` measurement plus about 190 aggregates of the
gameplay measurements, each also as 2,048- and 8,192-tick windows of its
cumulative counters, under the identity, share and square-root transforms. Greedy
forward selection (one term per family) produced a first set; stepwise refinement
then tried dropping each term, swapping it for any other candidate or transform,
and adding new ones, until no change improved cross-validated fit by 0.0005. The
refinement replaced worker share, which had come in with a negative sign, by
attack share, and total hit points by units eating.

The prestige and school terms were added afterwards by design rather than by
selection. `prestige_needed` came first, to remove the late swings in games won
by prestige; `prestige_lead` and `school_levels` followed, because a term that
only wakes up at the end of a game cannot see a colony building a steady
prestige advantage. The three interact, so their transforms were chosen together:
every combination of `prestige_needed` (left out, identity, sqrt, log1p, x^0.25),
`prestige_lead` (left out, identity, signed sqrt, signed log1p, signed x^0.25)
and `school_levels` (left out, identity, sqrt, share), 100 in all, was refitted
with the seven other terms and scored by cross-validated fit and by how much the
eventual winner's chance jumped at the end of games prestige decided. Concave
transforms of the lead beat a linear one clearly (a linear lead over-weights the
long margins of large free-for-alls); among log1p, sqrt and x^0.25 for
`prestige_needed`, log1p gave the smallest end-of-game jump in prestige
victories (0.134 against 0.144 and 0.160); `school_levels` is best linear. The
chosen three score within 0.0001 of the best combination: with them the
eventual winner's average chance over its last ten samples in prestige-decided
games rises from 0.77 (`prestige_needed` alone) to 0.81. With the lead in the
model, `prestige_needed`'s coefficient falls from about −2.5 to −1.7: the lead
carries the steady advantage and `prestige_needed` the final approach.

The earlier history of `prestige_needed`: Its transform was chosen among
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

### The clock

A coefficient may change through the game: β(t) = coefficient + slope × c(t), a
straight line in the decision tick, clamped at the 180,000-tick cap. The clock
is absolute ticks because a live game does not know how long it will run. Fitting
it keeps the model a conditional logit: each timed term gains a second column,
its value scaled by c(t).

Allowing a slope on every term fitted the data better (CV R² 0.290 against
0.284 at every fourth sample), but a bootstrap over games showed most slopes
were noise or shared between correlated terms, and the random and mirror halves
disagreed on them. Forward selection on game-grouped cross-validation, adding
one slope at a time while it gained at least 0.0005, kept only `prestige_lead`:
alone it reached 0.291, more than all ten slopes together, and no other slope
added more than 0.0002. Its slope is +0.399 (bootstrap 95% interval +0.27 to +0.55;
+0.45 on the random half and +0.36 on the mirror half). The coefficient starts
slightly negative, −0.103: an early prestige lead means a colony rushed a
level-3 school, which costs its economy. It crosses zero near tick 46,000
and reaches +0.297 by the cap, when a lead is usually the decisive one. The
ridge penalty, which shrinks slopes toward constant coefficients, made no
difference between 1e-4 and 1.

A time curve does not fix the model's late-game caution (below): that caution
is measured against the fraction of each game played, which a live model cannot
know, and a 1v1 at tick 40,000 is nearly over while an eight-colony game has
barely begun.

### It is calibrated, and that is what matters here

A winning condition needs the number to mean what it says, not merely to rank
correctly. Held out by game, predicted and observed win rates:

| Predicted | 0.023 | 0.076 | 0.141 | 0.247 | 0.348 | 0.451 | 0.543 | 0.648 | 0.748 | 0.852 | 0.927 | 0.979 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Observed | 0.018 | 0.051 | 0.136 | 0.277 | 0.360 | 0.462 | 0.564 | 0.696 | 0.772 | 0.856 | 0.921 | 0.965 |

The top bucket, where the threshold lives, reads 0.979 predicted against 0.965
observed. Mid-range predictions are slightly cautious and the low buckets
overstate a losing side's chances.

Calibration also depends on how far into the game a sample is (the sample's tick
divided by the game's length; expected calibration error, ECE, is the
sample-weighted gap between predicted and observed):

| Phase | 0–10% | 10–20% | 20–30% | 30–40% | 40–50% | 50–60% | 60–70% | 70–80% | 80–90% | 90–100% |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ECE | 1.4% | 2.0% | 1.9% | 1.8% | 2.4% | 2.8% | 3.1% | 3.5% | 3.6% | 3.8% |

Early, the rare confident predictions are slightly overconfident; from about
halfway the model is increasingly cautious (late in the game a predicted 65%
wins about 80% of the time, and a predicted 7% about 2%). One scale serves the
whole game, while the same gap in the inputs means more when there is less time
left to recover. For the winning condition the late error is on the safe side:
when it fires late, the real win rate is above the threshold.

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
  fitter read the same table. `prestige_lead` is clamped to ±2,400 and is the one
  signed measurement: its table holds the transform of the magnitude, and the
  engine restores the sign (`WinProbability::signedTableValue`), so only odd
  transforms can apply to it.
- A timed coefficient is `coefficient + slope × min(t, 180,000) / 180,000` in
  fixed point: one 64-bit multiply and one division truncated toward zero
  (`WinProbability::timeScaled`), mirrored in the fitter, before the term's own
  arithmetic. Each competitor's slot carries the decision tick. The emitter refuses any other transform it cannot
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
"log_mode": "gameplay"}`). Gameplay logs of eight-colony games reach about
200 MiB, against a 64 MiB default allowance: set `limits.log_bytes` to 512 MiB on
every job, and a results `artifact_budget_bytes` that fits about 20 MiB per game
(a thousand-game campaign needs about 60 GiB). Draw the mirror half with
`seat_sampling: "mirror"`. Then, from Python:

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
| **95% (tournament default)** | **17.5%** | **2.2% (16 of 730)** |
| 97% | 12.6% | 1.1% (6 of 565) |
| 99% | 5.8% | 0.7% (2 of 283) |

Measured over the 1,029 games, cross-fitted so no game is scored by a model that
saw it, with three consecutive qualifying samples required. Errors sit below
what each threshold implies; the confirmation requirement removes most one-sample
swings.

## Limits

- The model was fitted on AI games only. Human play may distribute these
  quantities differently, and nothing here has been checked against it.
- It is fitted on the whole random draw, so it describes the average game across
  every generator and size rather than any particular map. Per-generator
  behaviour has not been broken out.
- It reads ten numbers. Position, terrain, who is adjacent to whom and what is
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
