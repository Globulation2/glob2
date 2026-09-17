# Neurotica overnight log

Running notes. Newest entries at the bottom. Every claim here should be
traceable to a command output, not an impression.

## Baselines to beat (full-corpus BC checkpoint, 6 games vs each of
## numbi/castor/warrush/nicowar, generated 128x128 maps)

| decode | record |
| --- | --- |
| inert field (decode bug, AI idle) | 4W / 24 |
| `--top-k` k=12 | 1W / 22 |
| `--threshold 0.05` | 0W / 22 |
| `--threshold 0.05 --type-caps` | pending |

Nothing has beaten doing nothing yet.

## Settled findings

**1. Deployment strength is governed by `novel_precision`, not P@1.**
The deployed field is idempotent on existing buildings (it has to be, or the
reconciler demolishes them), so the COPY half emits no orders by construction.
Every order comes from the NOVEL half, whose precision is 0.18. Checkpoints
were selected on P@5, which is dominated by the copy half -- i.e. selected on
the half of the output that cannot act.

**2. The model has learned the MARGINAL distribution over building types.**
Telemetry, same map/seed/opponent:

```
Neurotica  workers= 4  warriors= 0   swarm=48 inn=57 school=19     LOST 6690
Nicowar    workers=77  warriors=30   swarm=4 inn=7 hosp=3 race=2
                                     pool=2 barracks=2 school=2    alive 20000
Cortex     workers=30  warriors=25   swarm=6 inn=5 hosp=4 race=1
                                     barracks=1                    alive 20000
```

An uncertain cell decodes to whichever type is commonest in the corpus, so
thresholding turns "inn-ness everywhere" into 57 inns. It never builds
hospital/racetrack/pool/barracks/tower at all.

**3. A per-cell marginal field cannot express "one more inn".**
This is representational, not a tuning problem. Every decode tried today is a
different way of turning a marginal into a count, and all fail the same way:
uncapped -> 10x overbuild and starvation (4 workers); capped -> passive (1
swarm, 26 orders). Both ends of the dial fail because the count information is
not in the output at all.

## Method notes

* Single-game sweeps are worthless; glob2 variance swamps the effect. >=6 games
  per config, and say so when n is small.
* `pkill -f <pattern>` over ssh matches the ssh command's own line. Use
  `for p in $(pgrep -f ...); do [ "$p" != "$$" ] && kill "$p"; done`.
* Don't kill by broad pattern while an eval is running -- did that once and
  killed the eval's own server.

## Decode bugs found (all invisible to training metrics)

1. Per-cell argmax yields an EMPTY field (buildings are ~0.1% of cells).
2. Building plane has no DONT_CARE, so argmax on occupied cells reads as "want
   empty here" -> 874 created / 683 demolished in 8000 ticks.
3. eval.sh and record_replays.sh lacked `--top-k`, silently measuring the
   inert AI.
4. A "threshold" built as topk(n) over masked scores is still a fixed count.

---

## Night run starts (2026-09-16 ~21:00)

**Correction to finding 3 above.** The "capped decode is passive (1 swarm, 26
orders)" result was invalid: the per-type cap code named a local `sel`, which
shadowed the `selectors` object driving the server loop, so the server crashed
on its next poll and the rest of that game ran inert. The same crash voided the
`--threshold 0.05 --type-caps` eval that was in flight. Renamed to `of_type`.
Lesson: a server that "fails inert" hides its own crashes -- check the server
log, not just the game result.

**Change under test: a per-type count head.** The per-cell building head is a
marginal and cannot express "one more inn"; the count head predicts how many of
each of the 13 types the team should HOLD, from the pooled bottleneck (a global
judgement, like value), trained on log1p counts so the loss is not dominated by
swarm/inn. Labels are free: the BC label is one triple per building, so a
bincount over the type column is an exact per-type count. The decode then
bounds each type by the model's own predicted count (`--use-count`).

Also added `count_mae` to the eval metrics, in buildings rather than log space,
so it can be checked against "a teacher holds 4-7 inns".

Run: `~/neurotica/countrun.sh` -> `ckpt_count`, 12 epochs, ~39 min/epoch at
116 samp/s, GPU0. GPU1 left free for evals and playtests.

Box reallocated: self-play stopped for the night. It was optimising a policy
with the representational gap the count head addresses, so its episodes are of
little value until this lands; restart it from the better checkpoint.

## Count head, attempt 1: learned counts, destroyed placement

`ckpt_count` (12 epochs, count head attached to the trunk, count_weight 1.0):

```
                 count_mae   novel_precision   P@1
ckpt_full            --          0.185        0.554
ckpt_count         0.116         0.035        0.268
```

The count head works: **count_mae 0.116** means it predicts per-type building
counts to about a tenth of a building. But novel_precision fell 5x, and
novel_precision is the deployment metric.

Cause is loss scale, not the idea. The count loss sits around 0.1 while the
building loss is ~0.004, so at count_weight=1.0 the count term dominated the
shared encoder by ~25x and the trunk reorganised itself around counting.
Visible in the logs: final train loss 0.0044 here against 0.0006 for the
placement-only run.

**Fix, two parts:**
1. `out["count"] = self.head_count(pooled.detach())` -- the count head is
   auxiliary and must never reshape the trunk.
2. `--count-only --init <ckpt>` -- freeze everything but head_count and bolt a
   count head onto the checkpoint that already places best. This makes the
   trade structurally impossible rather than merely unlikely, and it is cheap:
   332 samp/s against 116 (no backward through the trunk), ~14 min/epoch.

Running as `ckpt_cnt2`. If count_mae stays near 0.1 with novel_precision
unchanged at 0.185, the composition hypothesis can finally be tested on its
own, with `--use-count`, against the standing 4W/24 do-nothing baseline.

**General lesson:** when adding an auxiliary head to a shared trunk, compare
the loss magnitudes before picking a weight. "Weight 1.0" is not neutral; it
is whatever ratio the two losses happen to have.

## The actual cause of the over-building (04:00)

`--use-count` did nothing: the game still finished swarm=52 inn=99 with the
count head predicting inn 5.38 against a true 5.27. The count head is fine --
per-type predictions are all close:

```
type      pred    true
swarm     2.56    2.69
inn       5.38    5.27
hosp      3.54    3.27
barr      1.68    1.54
```

Instrumenting the cap showed why the budget never bit:

```
CAP caps=[1,1,0,...] have=[ 4,0,...] allow=[0,1,0,...] keepN=3
CAP caps=[1,1,0,...] have=[28,0,...] allow=[0,1,0,...] keepN=0
CAP caps=[2,2,0,...] have=[48,0,...] allow=[0,2,0,...] keepN=0
CAP caps=[2,2,1,...] have=[80,0,...] allow=[0,2,1,...] keepN=0
```

`keepN=0` -- **no new placements at all** -- while the swarm count climbs 4 ->
28 -> 48 -> 80. The buildings were never placements, so no placement budget
could ever have bounded them.

**Cause.** The desired field is anchored top-left; the observation marks every
cell a building covers; and `observed_` in the reconciler is keyed by anchor
alone (`NeuroticaReconciler.cpp:86`, from `posX/posY`). So re-asserting the
observed type across a footprint asks for a NEW building at each non-anchor
cell -- a 2x2 becomes four requests -- and each new building covers cells of
its own. That is the exponential growth, and it has been present under every
decode tried, which is why every decode over-built.

Nothing in the per-cell metrics could see this: the model's output was fine,
and the corruption happened in the translation from field to orders.

**Fix, two halves:**
* Decoder emits the type at the ANCHOR cell only. A cell is an anchor if it is
  marked and the cells above and left are not -- `torch.roll` on both axes,
  which is exactly right on a toroidal map.
* Covered-but-not-anchor cells get `DONT_CARE` (255), newly honoured by the
  building plane: never built on, never demolished. Every other plane already
  had DONT_CARE; the building plane did not, which is why the earlier
  idempotence fix had to re-assert types and walked into this.

Harness: 72 checks, 0 failures, including a test that pins the hazard --
"repeating the type on a covered non-anchor cell asks for a further building".

**Asymmetry worth remembering:** at a non-anchor covered cell, a *type* asks
for a new building but a *0* demolishes nothing, because nothing is observed
there. So the two wrong answers fail in opposite directions, and only at the
anchor does 0 mean demolish.

## Composition fixed (04:30). The economy is the next wall.

Telemetry after the anchor fix + a working count budget, same seed, vs numbi:

```
Neurotica  swarm=4 inn=6 hosp=2 pool=3 barracks=2 school=9 tower=1 flags=14  workers= 4
Nicowar    swarm=4 inn=7 hosp=3 race=2 pool=2 barracks=2 school=2 flags=4    workers=77
```

Composition finally resembles a teacher -- school and flags are over-built,
everything else is in range. First time in this project that has been true.

**But workers=4.** It builds a reasonable base and has no economy. That is now
the whole gap.

Two further bugs fixed to get here:
* `have` divided covered cells by a guessed footprint size, reading 4 swarms
  when the team held 1. Count labels are per-building, so count anchors.
* The budget deadlocked: the count head predicts what a team in THIS state
  should hold, so a stunted base (out of distribution -- teachers are far
  bigger by the same tick) draws a low prediction, which forbids building,
  which keeps it stunted. Holding 1 swarm and 1 inn it asked for 1 swarm and
  0 inns, and built nothing for 20000 ticks. Now the highest-scoring type
  always keeps one slot so the state can walk back into distribution.

**Next, and it is structural:** the policy socket carries only 3 bytes per
cell -- building class, score, area bits. `NeuroticaPolicySocket.cpp:176`.
So `workers`, `workersFuture`, `swarmRatio`, `level`, `flagRadius`,
`priority` and `minLevelToFlag` are never set by the network at all; they sit
at DONT_CARE and the reconciler leaves staffing and swarm ratios alone.

The net therefore cannot staff a building or tune a swarm, which is very
likely why 4 workers. The desired-state schema has always had these planes;
the wire format and the model heads never caught up. That is the next piece
of work, and it needs a protocol bump plus heads for those planes.

Also fixed: the socket folded any class above 13 to 0, so DONT_CARE never
arrived. Harmless while `observed_` is anchor-keyed (0 on a non-anchor cell
demolishes nothing) but wrong, and it would bite the moment that changed.

## First result that beats doing nothing (04:35)

`ckpt_cnt2`, anchor decode + count budget, `--placements 48 --use-count`:

| decode | vs numbi | vs castor | vs warrush | vs nicowar | total |
| --- | --- | --- | --- | --- | --- |
| inert (bug) | 3W 2L | 1W 3L | 0W 4L | 1W 4L | 4W/24 |
| top-k k=12 | | | | | 1W/22 |
| threshold 0.05 | | | | | 0W/22 |
| **anchor + counts** | **4W 1L** | 0W 6L | 0W 5L | **1W 4L** | **5W/22** |

A winning record against numbi, and a win against nicowar while actually
playing. Still swept by castor and warrush. Small sample, but it is the first
configuration to clear the do-nothing baseline.

## The economy is a rate problem, not a collapse

Timeline, team 0:

```
tick  1536  units= 5  bld= 1
tick  5632  units= 9  bld= 2
tick  9728  units=12  bld= 2
tick 13824  units=14  bld=13
tick 19968  units=16  bld=14
```

Nothing collapses -- it grows far too slowly, and sits at 2 buildings until
tick 9728. The game is lost in the first third.

**Cause: the count budget is a follower.** `allow = predicted - held`, and the
prediction is made from the CURRENT state, so an agent already behind the
teacher distribution is only ever permitted to creep toward where it already
is. It can never catch up, by construction. This is the same shape as the
copy/novel trap and the marginal/count gap: a quantity that looks right in
validation (pred growth +2.27 vs true +1.88 per 1000 ticks, measured
in-distribution) behaves quite differently once the agent's own state has
drifted off the data manifold.

Testing `--count-scale`: multiply the predicted counts before using them as a
budget, keeping the composition the head predicts while allowing faster
approach to it. Sweeping 1.0 / 1.5 / 2.5 / 4.0 on a fixed seed, reading units
and buildings at tick 5632 as the early-game measure.

## Growth scale: loosening the budget makes it worse (04:45)

```
scale=1.0  draw   tick5632: units=9 bld=4   final workers=14
scale=1.5  draw   tick5632: units=6 bld=2   final workers=18
scale=2.5  LOSS   tick5632: units=5 bld=1   final workers= 4
scale=4.0  LOSS                             final workers= 4
```

So the budget was never too tight. Loosening it reproduces the overextension
pathology, and the hypothesis that slow growth came from a too-conservative
budget is wrong. Keep scale 1.0.

## Teacher control curves, same map (units/buildings)

```
tick     nicowar   numbi    cortex   Neurotica
1536      8 / 2     8 / 1    7 / 1     5 / 1
5632     30 / 6    21 / 1   15 / 6     9 / 4
9728     51 /10    29 / 1   18 / 8    12 / 2
19968   107 /21     --      22 /11    16 /14
```

**Numbi wins games holding ONE building.** It puts every early worker on wheat
and lets the starting swarm pump units. Neurotica holds MORE buildings than
numbi at tick 5632 and less than half the population.

Buildings per unit at 5632: nicowar 0.20, numbi 0.05, cortex 0.40,
**Neurotica 0.44**. It is over-built *relative to population*, and a swarm
only produces when wheat reaches it (`src/building/TypeSteps.cpp:27`), so
workers spent on construction are workers not feeding the swarm. The economy
never bootstraps.

This reframes the earlier "composition" finding: composition is now right, but
composition was never the whole story -- *timing against population* is. The
count head predicts what a team in a given state holds, and has no notion that
those buildings must be staffed by units that do not exist yet.

Testing `--build-per-unit`: require N units per building held before allowing
another. Teacher-implied N is about 5 (nicowar) and far higher early.

**Underlying structural gap, unchanged:** the policy socket carries 3 bytes per
cell, so `workers`, `workersFuture`, `swarmRatio` and `priority` are never set
by the network. Labour allocation -- which is what the early game IS -- is not
expressible by the model at all. The population gate is a heuristic standing in
for a head that should exist.

## Methodology failure of my own (04:50)

The `--build-per-unit` sweep returned this:

```
bpu=0  LOSS  tick5632: units=10 bld=1
bpu=3  draw  tick5632: units=10 bld=1   tick19968: units=8 bld=1
bpu=5  draw  tick5632: units=10 bld=1   tick19968: units=7 bld=1
bpu=8  draw  tick5632: units=10 bld=1   tick19968: units=8 bld=1
```

`bpu=0` disables the gate, so it should reproduce the earlier scale=1.0 run --
which gave units=9 bld=4 at 5632, 14 buildings by the end, and a draw. It did
not. The server code is identical on that path; I checked.

The explanation is that **the sweep scripts never set `GLOB2_TEST_SEED`**, so
only the map was fixed and every game had a different game seed. These runs
differ by variance, not by configuration.

I wrote "single-game sweeps are worthless here, use >=6 games per config" in
this very file at the start of the night, and then ran three more single-game
sweeps and drew conclusions from two of them. The `--count-scale` conclusion
("loosening makes it worse") rests on the same weak evidence and should be
treated as unproven, not as the settled finding I recorded above.

Rules going forward, for real this time:
* Fix `GLOB2_TEST_SEED` in any diagnostic meant to be compared across configs.
* Never change a default based on n=1. Sweeps propose; only a >=24-game eval
  decides.
* A single game is still useful for *mechanism* (does it crash, does it build
  the wrong types, does it demolish) -- just never for *outcome*.

Running the proper eval for `--build-per-unit 5` against the 5W/22 baseline.

## Population gate rejected (04:55)

24-game eval, `--build-per-unit 5`:

```
vs numbi 1W 3L 2cap | castor 0W 4L 2cap | warrush 1W 5L | nicowar 0W 6L   = 2W/22
```

against the 5W/22 baseline. The gate hurts; default stays 0. Worth noting it
produced the first win against warrush, but the total is clearly worse and one
win is not a signal at this sample size.

Best config remains: `ckpt_cnt2`, `--top-k --placements 48 --use-count` = 5W/22.

## Closing the structural gap: staffing (NPS3)

The network has never been able to allocate labour. The policy socket carried
3 bytes per cell -- class, score, areas -- so `workers` (Building::
maxUnitWorking) sat at DONT_CARE forever and every building was created at its
type maximum. With ~10 workers and 14 buildings each demanding max staff, the
labour spreads across schools and flags instead of concentrating on the swarm,
and a swarm only produces when wheat reaches it. That is the measured economy
failure.

Scoped to the single plane that matters rather than all seven:

* **Data**: labels now carry `workers` per building. The trace has recorded it
  since ATR2; the loader was dropping it.
* **Head**: `head_workers`, one channel, predicted in units, supervised only at
  building anchors via a mask -- an unmasked loss would pull every empty cell
  toward 0 and drown the signal.
* **Protocol**: NPS3, 4 bytes per cell. The magic bump matters: a mismatched
  binary now fails loudly instead of misparsing, which is the failure mode that
  cost hours earlier tonight.
* **Decode**: staffing byte is DONT_CARE wherever no building is wanted, so the
  reconciler leaves existing staffing alone rather than reading a predicted 0
  as "unstaff this".
* **Training**: `--count-only` with the trunk frozen, weight 0.02. Deliberately
  small -- staffing is in units (tens) against a building loss of ~0.004, and
  the count head at weight 1.0 already demonstrated what an auxiliary head does
  to a shared trunk.

Harness still 72 checks, 0 failures.

## Staffing head trained (05:15)

`ckpt_staff` = `ckpt_full` trunk, frozen, plus count and staffing heads:

```
worker_mae        1.82   (staffing is typically 1-8 units, so within ~2)
count_mae         0.35
novel_precision   0.1846  <- identical to ckpt_full
p1                0.5542  <- identical to ckpt_full
```

The freeze did its job: placement is bit-for-bit the checkpoint that scored
5W/22, with two new heads bolted on. That is the property the joint count run
lacked, and it makes every comparison from here an A/B of the heads alone.

`--staffing` is opt-in and off by default: with it off the reply is DONT_CARE
everywhere, which is exactly pre-NPS3 behaviour. So the control and the
treatment are the same checkpoint and the same binary, differing in one flag.

Running 3 fixed seeds per arm -- `GLOB2_TEST_SEED` set this time.

## Staffing A/B (05:20), fixed seeds, one checkpoint, one binary, one flag

```
              units@5632   units@19968   outcomes
staffing off    6, 8, 4      11, --, 5    1 loss, 2 draws
staffing on     8, 9, 9      13, 15, 5    0 losses, 3 draws
```

Early units are higher in all three seeds and there is one fewer loss. n=3, so
this proposes rather than decides -- full eval running.

But note the ceiling has barely moved: best case 15 units at tick 19968 against
a teacher's 107. Staffing was necessary (the network genuinely could not
allocate labour before) and is clearly not sufficient. The economy gap is
roughly 7x and a staffing head closed maybe a tenth of it.

Honest reading: the remaining gap is probably not one more missing plane. The
teachers run a *sequenced opening* -- wheat first, swarm fed, then expand -- and
a stateless per-tick field predicting "what a team in this state holds" has no
representation of sequence at all. Every tick it re-derives a plausible
snapshot; it never commits to a plan across ticks. That is the deepest
structural limitation found tonight and it is inherent to the design, not a
bug in it.

Worth putting to Bradley rather than deciding unilaterally: the desired-state
field is his design and it is a good one for placement -- it went from unusable
to beating numbi 4W-1L once the anchor bug was fixed. But openings may need
either (a) a small amount of policy state, or (b) the count head conditioned on
game phase rather than current holdings, so early targets reflect "what to
build NEXT", not "what a team like me has".
