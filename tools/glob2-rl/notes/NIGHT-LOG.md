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

## Staffing rejected by the full eval (06:06)

```
ckpt_staff --staffing:  numbi 1W3L2cap | castor 0W4L2cap | warrush 0W5L1cap
                        nicowar 0W5L1cap                             = 1W/22
baseline (ckpt_cnt2, no staffing)                                    = 5W/22
```

The n=3 A/B said staffing helped -- higher early units in all three seeds and
one fewer loss. The 24-game eval says the opposite. That is the second time
tonight a 3-game signal pointed the wrong way, and it is the clearest possible
argument for the rule: **sweeps propose, only a >=24-game eval decides.**

Caveat before blaming staffing: two things changed against the 5W/22 baseline,
not one. `ckpt_staff` retrained the count head (3 epochs, count_mae 0.3545 vs
`ckpt_cnt2` 2 epochs, 0.3466) and the binary moved to NPS3. Running the proper
control -- `ckpt_staff` with `--staffing` OFF -- before attributing the
regression to staffing at all.

A plausible mechanism if staffing is the cause: the head predicts the staffing
a *teacher* building carries, and a teacher building sits in a 70-worker
economy. Applying those numbers to a 10-worker economy over-commits the entire
workforce to whichever buildings exist, which is the same distribution-shift
failure as the count budget -- a target learned on teacher states, applied to
states this agent actually reaches. Third instance tonight.

## Staffing attributed cleanly (06:25)

```
ckpt_staff, --staffing OFF:  numbi 3W2L1cap | warrush 1W5L | nicowar 1W4L1cap = 5W/22
ckpt_staff, --staffing ON:                                                      1W/22
```

The control reproduces the 5W/22 baseline exactly, so the retrained count head
and the NPS3 binary are exonerated and the staffing head itself causes the
regression. Worth noting the control is a *different checkpoint* from the
original 5W/22 (retrained count head, new binary) and landed on the same 5W --
which is a small independent check that the 5W number is not itself noise.

**Mechanism, as predicted before the control ran:** the head learns the
staffing a building carries in a teacher economy of ~70 workers. Applied
unscaled to a 10-worker economy it commits the entire workforce to whichever
buildings happen to exist, starving wheat delivery -- the same failure the
whole night has been about.

This is now the third confirmed instance of one pattern, and it deserves to be
the headline lesson of the night:

> **Every BC target is conditioned on teacher states. The agent is never in a
> teacher's state. Any target expressed in absolute quantities -- counts,
> staffing, building totals -- has to be rescaled to the agent's actual
> situation before it is applied.**

Copy-vs-novel was the same thing (labels assume the teacher's base already
exists). The count budget was the same thing (predictions assume the teacher's
trajectory). Staffing is the same thing in units of labour.

Testing `--staffing-budget 0.8`: scale the total staffing request to a fraction
of the units actually alive. Server-side only, no retrain.

## Staffing rejected outright (06:45)

```
staffing off                        5W/22   <- best config
staffing on, raw teacher numbers    1W/22
staffing on, normalised to labour   0W/22
```

Normalising made it worse, not better, so the distribution-shift mechanism I
proposed is NOT the whole story -- and I should not have presented it as
confidently as I did before testing it. Rescaling the magnitude does not
rescue it, which means the predicted staffing is wrong in kind, not merely in
scale.

Most likely reading: the engine default (`type->maxUnitWorking`, what the
reconciler already applies when the plane is DONT_CARE) is simply a better
policy at this economy size than anything the head predicts. The teachers'
staffing numbers encode decisions that only make sense inside their own
sequenced opening, so importing them piecewise imports nothing useful.

NPS3 stays -- the protocol gap was real and the plane should be reachable --
but `--staffing` remains off by default, and the staffing head is unused. This
is a negative result and it cost about two hours; the value is that the
capability now exists for RL to learn staffing directly, which is a better bet
than cloning it.

**Night's final configuration:** `ckpt_staff` (or `ckpt_cnt2`, identical
placement), `--top-k --placements 48 --use-count`, staffing off = **5W/22**,
with 3-4W vs numbi and a win each against warrush and nicowar.

---

# Morning summary

## Where it ended

Best configuration: `ckpt_staff/best.pt` (placement identical to `ckpt_full`),
served with `--top-k --placements 48 --use-count`, staffing off.

```
vs numbi    3-4W  1-2L
vs castor      0W    4-6L
vs warrush   0-1W    4-5L
vs nicowar   0-1W    4-5L
                            = 5W / 22, reproduced twice on different checkpoints
```

Start of night: the AI was inert and the 4W/24 "baseline" was a do-nothing bug.
End of night: it plays, builds a teacher-like base, and has a winning record
against numbi. It is still swept by castor and warrush.

Self-play restarted from that checkpoint and is healthy -- `mean_return` around
-0.21 at iter 4, against the -0.9 that every previous run was pinned at, because
the agent no longer dismantles its own base.

## Bugs found and fixed (all invisible to training metrics)

1. Per-cell argmax decoded to an EMPTY field. The AI did nothing for entire
   games while BC metrics looked healthy.
2. Occupied cells set from argmax read as "want empty here" -> the reconciler
   demolished the base: 874 created / 683 demolished in 8000 ticks.
3. **The anchor bug** -- the big one. The field is anchored top-left, the
   observation marks whole footprints, and `observed_` is keyed by anchor, so
   re-asserting a footprint requested a NEW building per non-anchor cell.
   Swarms went 4 -> 28 -> 48 -> 80 in 1500 ticks with ZERO placements in the
   field. Present under every decode tried; the reason they all over-built.
4. `eval.sh` and `record_replays.sh` ran servers without the decode flag,
   silently measuring the inert AI.
5. A "threshold" built as `topk(n)` over masked scores is a fixed count.
6. A local named `sel` shadowed the server's selector object; the server
   crashed and, because Neurotica fails inert, games finished as a do-nothing
   AI and looked merely bad.
7. The socket folded DONT_CARE to 0, discarding the signal.
8. `have` divided covered cells by a guessed footprint size (4 swarms for 1).
9. The count budget deadlocked: a stunted base draws a low prediction, which
   forbids building, which keeps it stunted.
10. The corpus index cache had no schema version, so new label fields silently
    reused a stale index.

## What was added

* `DONT_CARE` on the building plane, with harness tests (72 checks, 0 failures).
* A per-type count head. Predicts per-type building counts accurately
  (inn 5.38 vs true 5.27) and fixed composition.
* NPS3: 4 bytes per cell, so the `workers` plane is reachable at all.
* `play.sh` / `record_replays.sh` / `watch.sh` for human inspection.

## What was rejected, with numbers

* Population gate (`--build-per-unit 5`): 2W/22.
* Staffing head, raw: 1W/22. Normalised to available labour: 0W/22.
* `--count-scale` > 1: unproven, likely noise (n=1 evidence).

## The lesson worth keeping

**Every BC target is conditioned on teacher states, and the agent is never in a
teacher's state.** Copy-vs-novel, the count budget, and staffing were all the
same failure. But note the limit of that lesson: rescaling staffing to the
agent's labour made it *worse*, so "rescale the magnitude" is not the general
fix. Some teacher quantities encode decisions that only make sense inside the
teacher's own sequenced opening, and importing them piecewise imports nothing.

## The open structural question, for Bradley

The desired-state design is vindicated for placement: once the anchor bug was
fixed it went from unusable to beating numbi. What it has no representation of
is **sequence**. Teachers run an opening -- wheat first, swarm fed, then expand
-- and a stateless per-tick field predicting "what a team in this state holds"
re-derives a plausible snapshot every tick and never commits to a plan. The
economy ceiling (15 units at tick 19968 against a teacher's 107) is most likely
this, not another missing plane.

Two options, both his call:
* a small amount of policy state, enough to commit to an opening;
* condition the count head on game phase rather than current holdings, so early
  targets mean "what to build NEXT" rather than "what a team like me holds".
  Cheaper, keeps the policy stateless.

## Self-play was training a different agent than the eval measured (07:05)

```
iter   4:  mean_return -0.21   <- small-sample noise, not progress
iter 143:  mean_return -0.74     rollout win rate 0.037
```

Cause found: the eval decodes with `--use-count`, but the `--sample` path
skipped the budget entirely (deliberately -- PPO must be credited for exactly
the placements it sampled). So PPO was optimising a policy with no composition
control, while every measurement of quality used one. Training and deployment
were different agents.

Fixed by sampling FROM the budgeted candidate set rather than trimming after:
`NeuroticaNet.budget_mask` zeroes cells whose best type is already at its
predicted count, and both `act_placements` and `evaluate_placements` apply it.
The mask depends only on the observation, so it is reconstructible at training
time and the credit stays honest. Verified end to end: logp when acting and
logp when re-scoring are identical (-77.6325, `allclose` True).

This is the same class of mistake as the decode bugs, one level up -- the
agent being optimised has to be the agent being measured, and nothing in the
PPO logs would ever have shown the difference.

Self-play restarted from `ckpt_staff` with `--use-count` in the loop.

## Self-play with the aligned decode (07:50)

```
                        rollout win rate      mean_return
before (unaligned)      0.037 @ 360 games     -0.74
after  (budget-aware)   0.059 @ 270 games     -0.78
```

Win rate is up (~16 wins in 270 against ~13 in 360) but the returns band is
unchanged, and neither difference is large enough to call at this n. Not
evidence of learning yet.

PPO has moved: L2 drift from the BC init is **2.855** across 92 tensors, so
the gradient is reaching the weights -- this is not the silently-frozen policy
of the original action-space bug. The open question is whether the movement is
an improvement, which drift cannot answer.

Running the measurement that can: a 24-game eval of the live `ppo/policy.pt`
under the same decode as the 5W/22 baseline. Same opponents, same protocol.
If PPO is learning, it beats 5W/22; if it is drifting off a good BC init, it
scores worse, and that would be the clearest possible argument that the
sequence limitation -- not plumbing -- is now the binding constraint.

## First evidence self-play helps (08:15)

24-game eval of the live `ppo/policy.pt`, same decode and opponents as the
baseline:

```
                     numbi        castor      warrush     nicowar    total
BC baseline          3-4W 1-2L    0W 4-6L     0-1W 4-5L   0-1W 4-5L  5W/22
PPO @ ~125 iters     3W 1L 2cap   1W 3L 2cap  2W 4L       0W 5L 1cap 6W/22
```

6W against 5W is not significant by itself -- pooling the two independent BC
evals gives 10W/44 = 0.227, against PPO 6/22 = 0.273, and that gap needs far
more games to resolve. What is new is the *shape*: wins come from three of four
opponents instead of concentrating on numbi, including the **first win against
castor in any configuration tonight**, and two against warrush where the best
prior was one.

The important negative: PPO has NOT degraded the BC init. That was the main
risk of running RL on top of a cloned policy, and after ~125 iterations and an
L2 drift of 2.855 it has not happened.

So the sequence limitation is real but it is not yet the binding constraint --
I was ready to call it that and the data does not support doing so. The loop
deserves to run longer before any architectural change is proposed.

Plan: leave self-play running, re-eval every couple of hours. If PPO is
learning, the gap against 0.227 should widen with drift; if it plateaus at
parity, that is when the sequence argument becomes the live one.

## Self-play was starving for positive reward (09:15)

At iter 416, drift 5.286 (up from 2.855), the rollout win rate had gone the
wrong way:

```
270 games   win rate 0.059
915 games   win rate 0.037    -> ~2.8% over the last 645 games
mean_return flat at -0.88 across the whole run
```

**The league was `nicowar,cabino,cortex,maxima` -- the four strongest AIs.**
The agent lost ~97% of its games, so the advantage was negative almost
everywhere: PPO is pushed away from whatever it just did and no positive
direction is ever reinforced. Returns pinned at -0.88 is exactly what that
looks like. Shaping was on (0.1) and is far too weak to carry the signal alone.

This also explains why the 24-game eval showed 6W/22 while rollouts showed 3%:
the eval includes numbi, which the agent beats; the league did not include a
single opponent it could beat.

Fix: a spread league (`numbi,warrush,castor,cortex,nicowar`) plus PFSP-style
matchmaking -- weight each opponent by p*(1-p), maximal at a 50% win rate,
which is where a game carries the most information. Opponents with fewer than
8 games are sampled freely so the ladder fills in; a 0.05 floor keeps every
opponent occasionally sampled so a hopeless matchup can become live again as
the agent improves.

Verified on synthetic records (numbi 90%, warrush 50%, castor 0%, cortex 10%,
nicowar 0%):

```
warrush 53.4%   numbi 20.8%   cortex 20.6%   nicowar 2.9%   castor 2.3%
```

The rollout log now prints the per-opponent ladder so the curriculum is
visible rather than inferred.

**Lesson:** a win rate near 0 and a win rate near 1 are equally uninformative,
and I set up a league that guaranteed the former. The eval opponents and the
training opponents have to be chosen for different reasons -- the eval spans
the difficulty range to measure, the league concentrates where the gradient is.

## Learning curve so far (09:20)

```
                    numbi   castor  warrush  nicowar   total
BC baseline         3-4W      0W      0-1W     0-1W    5W/22   (pooled 10W/44)
PPO @125 iters      3W        1W      2W       0W      6W/22
PPO @416 iters      2W        0W      3W       1W      6W/22
```

Two useful readings.

**PPO has not degraded the BC init even while its rollout win rate collapsed.**
Rollouts fell to ~2.8% because the league was strong-only; the eval includes
opponents the agent can beat, and there it held 6W/22. So the rollout number
was measuring the curriculum, not the policy. Worth remembering: a training
metric computed against a fixed strong pool is not a measure of the agent.

**The composition is moving in the right direction.** warrush 0 -> 2 -> 3 and
nicowar 0 -> 0 -> 1, against numbi 3 -> 3 -> 2. A trend on a specific harder
opponent across two snapshots is more convincing than the flat total, which
hides it.

But the total has plateaued at 6W across 291 iterations of training, which is
consistent with the diagnosis above: with ~97% losses there was almost nothing
to learn from. The curriculum fix landed after this snapshot, so the next
snapshot is the first that will have been trained with winnable games in the
league.

## Curriculum fix, immediate effect (09:25)

First 15 games after the restart:

```
                 before      after
mean_return      -0.88       -0.12
rollout win rate  0.037       0.200
ladder           invisible   castor:2/7 cortex:0/4 nicowar:0/1 numbi:1/2 warrush:0/1
```

`mean_return` moved from -0.88 to -0.12 within ten iterations. That is not the
policy improving in ten iterations -- it is the reward signal finally being
informative. The previous 416 iterations were spent almost entirely on losses.

Unexpected: castor 2/7 in rollouts, when castor is 0W in every 24-game eval.
Rollouts sample placements (`--sample`) and run at policy-period 100, the eval
decodes greedily at period 25, so these are different policies in effect. Worth
remembering before reading rollout numbers as eval numbers -- they are not
comparable, which is part of how the strong-only league hid its own problem for
so long.

## What PPO actually learned (10:30) -- the clearest result of the night

Behavioural diff, identical map and game seed, vs warrush:

```
                      BC init (ckpt_staff)         PPO (snap3)
outcome               LOST at 11394 ticks          survived to 20000
workers                4                           14
foodCritical           4                            0
inns                   1                            3
schools                9                            0
flags                 31 (11 exp/13 war/7 clear)    4
defence towers         1                            4
total buildings      ~52                           16
units @5632            6                            7
units @13824          (dead)                       11
```

Every change is one a human would call correct:

* **Flags 31 -> 4.** The BC clone spammed exploration/war/clearing flags --
  they are cheap, frequent in the corpus, and nearly free to want, so the
  marginal field loved them. They do nothing for an economy. PPO cut them by
  87%.
* **Inns 1 -> 3, foodCritical 4 -> 0.** The BC clone starved. PPO learned to
  feed itself. This is the single change that most explains the survival.
* **Schools 9 -> 0.** Schools were the over-built type in every earlier
  telemetry read. PPO dropped them entirely.
* **Workers 4 -> 14**, from building 16 things instead of 52.
* **Defence towers 1 -> 4**, against a rusher.

So PPO is not making marginal adjustments -- it has reversed the specific
pathologies behaviour cloning produced, and it found them from game outcomes
alone. This is the first direct evidence that the RL half of the project does
what it was built to do.

It also vindicates Bradley's original call on pooling teachers rather than
conditioning: the diffuse BC prior is a poor player but an adequate *starting
point*, and the thing that fixes it is outcome, not more imitation.

**Caveat:** one game per arm. The composition differences are far too large to
be seed noise (52 buildings vs 16, 31 flags vs 4), but "survived vs lost" on
n=1 is not an outcome claim. The 24-game eval of this snapshot is the outcome
measurement and it is still running.

## Neurotica has never been able to build an army (10:30)

Eval of the first curriculum-trained snapshot:

```
vs numbi   2W 0L 4cap   <- stopped losing, started drawing
vs castor  0W 5L 1cap
vs warrush 1W 4L 1cap
vs nicowar 0W 5L 1cap        = 3W/22, with 7 caps
```

Wins fell 6 -> 3 while caps rose 5 -> 7, and losses to numbi went to zero. Read
alongside the behavioural diff, PPO has optimised for **not losing**: a lean
defensible base, four towers, no starvation, and no offence. A draw scores 0
against a loss at -1, so survival is exactly what the reward asks for when
winning is out of reach.

And winning by force has been out of reach the whole time:

> `Building/Lifecycle.cpp:93` -- a new swarm is created with `ratio[0]=1` and
> zero for every other unit type. WORKER=0, EXPLORER=1, WARRIOR=2
> (`UnitConsts.h`). `swarmRatio` was never in the wire protocol, so the network
> could not change it. **Neurotica has produced zero warriors in every
> telemetry read tonight, in every configuration, BC and PPO alike.** It cannot
> win by conquest -- only outlast.

That is the explanation for `warriors=0` appearing in every single telemetry
dump since the first one, which I noted repeatedly and never chased.

**NPS4** adds the mix: 7 bytes per cell, three worker/explorer/warrior weights,
sent only where a swarm is wanted. Head is 3 logits per cell, supervised by
cross-entropy against the normalised teacher mix, masked to cells where a swarm
actually stands. Eval now reports `warrior_share_pred` against
`warrior_share_true` so the head can be checked on the number that matters.

**Why this should transfer where the staffing head did not:** a ratio is
scale-free. Absolute staffing failed because teacher magnitudes assume a
teacher economy; 2:1:1 means the same thing in a 10-worker base and a 70-worker
one. If that reasoning is wrong the eval will say so, as it did for staffing.

Harness: 72 checks, 0 failures.

## The ratio head works, and the scale-free argument was still wrong (11:05)

Head learns the teacher mix accurately, placement untouched:

```
warrior_share_pred  0.3426      <- teachers put ~1/3 of production into warriors
warrior_share_true  0.3246
novel_precision     0.1846      <- identical to ckpt_full
p1                  0.5542
```

So teachers run about a third warriors and Neurotica has run 0% all night.

End to end, same map and seed vs warrush:

```
ratio off:  warriors=0   draw at 20000   workers=9
ratio on:   warriors=1   LOST at 8866    workers=5, foodCritical=3
```

First non-zero warrior count in the project -- and it lost a game it otherwise
drew. Diverting a third of unit production in a 5-worker economy is fatal, and
it only managed one warrior anyway.

**I predicted this would transfer because a ratio is scale-free. That was
wrong, and wrong in an interesting way.** The ratio is scale-free; the
*decision* to run it is not. A teacher spends 32% on warriors while holding 70
workers -- the affordability of that split is implicit context the ratio does
not carry. Cloning the proportion without the precondition imports a decision
that only makes sense at a scale the agent has not reached.

Fourth instance of one pattern tonight, and the sharpest statement of it:

> Teacher targets carry hidden preconditions. Counts assume a teacher's
> trajectory, staffing assumes a teacher's labour pool, ratios assume a teacher's
> economy can afford the split. Rescaling the number does not supply the
> precondition.

**Strategic consequence, and I think this is the right call:** stop adding
cloned heads. The one thing that has demonstrably worked is PPO discovering
these decisions from outcomes -- it cut flags 31 -> 4, tripled inns, dropped
schools, all unprompted. NPS4 now makes the planes *reachable*; the value of
that is that RL can learn to use them, not that BC can clone them. Every plane
cloned from teachers has been a negative result (staffing 1W/22, ratio
pending); every behaviour RL found has been an improvement.

## Correction: the ratio head is NOT a negative result (11:45)

24-game eval:

```
                  numbi   castor  warrush  nicowar   total
BC baseline       3-4W      0W      0-1W     0-1W    5W/22
PPO curriculum    2W        0W      1W       0W      3W/22
ratio head        1W        0W      2W       2W      5W/22
```

Parity with baseline, and **2 wins against nicowar -- the best result against
the strongest AI all night**. It trades easy wins (numbi 2W -> 1W) for hard
ones. I called it a negative result off a single game, and the single game was
wrong again. That is the third time tonight n=1 has pointed the wrong way, and
I had already written the rule twice.

## PPO cannot improve the heads it does not act with

Gradient from the PPO policy loss, trained checkpoint, real observations:

```
head_building  758.59
head_ratio       0.00
head_workers     0.00
```

Same class as the original latent bug: a head that shapes play but receives no
policy gradient, so it sits at its BC value forever. "Expose the plane and let
RL learn it" does not follow from exposing the plane -- the plane has to be
part of the ACTION.

(Also seen: one of four samples had 0 allowed cells under the budget, which
falls back to a uniform distribution and contributes no gradient. Rare, but it
means an over-tight budget silently costs learning signal as well as play.)

## The production mix is now an action

Five presets from all-worker to warrior-heavy, chosen globally per policy step
from the pooled bottleneck, sampled with its log-prob added to the placement
log-prob. Verified: sampled choices vary per state, act and evaluate agree
exactly (`allclose` True), and `head_mix` receives gradient 665.75 where it
previously received zero.

This is the direct consequence of the night's governing lesson. Cloning the
teacher mix imports a decision without its precondition and loses games;
*choosing* the mix from the current state is a decision the agent can only
learn from outcomes -- which is exactly how it learned to cut flags 31 -> 4
and triple its inns.

Self-play restarted from `ckpt_ratio` with the mix action live.

## Mix action: exploring, not yet converged (12:25)

After 166 iterations with the mix as an action, preset usage is still close to
uniform:

```
all-worker 15.3%  light-mil 12.9%  teacher 29.6%  heavy-mil 16.9%  explorer 25.3%
```

`mean_return` dipped to -0.56 from -0.17, which is expected rather than
alarming: 85% of steps now divert production away from workers, and we already
know that hurts a small economy. PPO has to discover that, and 166 iterations
of a 5-way categorical is not enough to.

**Instrumentation added, because the question was unanswerable.** The learner
deletes episodes once consumed, so only ~6 existed at any moment and
"does the military preset actually win" could not be asked. `ppo/episodes.csv`
now records one line per episode before deletion: iteration, opponent, outcome,
length, and the share of steps spent in each preset. Over a few hours that is a
real dataset for the economy-vs-army tradeoff.

**Mistake:** I restarted the learner with `--init ckpt_ratio` instead of
`--init ppo/policy.pt`, discarding ~166 iterations of progress. Only time lost,
but it was avoidable and worth writing down: when restarting a learner to pick
up a code change, initialise from the LIVE policy, not the original checkpoint.

Also worth recording: the rollout ladder reads numbi 48/177 while castor,
nicowar and warrush sit at 0 wins, yet the 24-game eval shows 2W against both
warrush and nicowar. Rollouts sample placements at policy-period 100; the eval
decodes greedily at period 25. They are different policies and their win rates
are not comparable -- a point that has now misled me twice.

---

# Independent review (13:30) -- and what it overturns

Bradley asked for an adversarial third-party review. A separate agent was
given the commit range and this log (not the conversation), read-only access,
and told to find where I was fooling myself. It found a great deal. The
findings below are its, verified by me where marked.

## 1. The headline is false: the inert AI scored 5W, not 4W  [verified]

The inert baseline row is `3W 2L | 1W 3L | 0W 4L | 1W 4L`. That sums to
**five** wins. I wrote 4W/24 and carried it through the whole night. The best
playing configuration is 5W/22. **The do-nothing AI and the best playing AI won
the same number of games.** Every "first configuration to clear the do-nothing
baseline" claim in this log rests on an addition error.

## 2. Self-play was degrading and I called it "exploring"  [verified]

`episodes.csv`, win rate by 200-episode bucket:

```
   0- 199  22.6%
 400- 599  18.0%
 800- 999  21.0%
1200-1399  16.0%
1600-1799  11.5%
2000-2199   6.9%
```

Against numbi alone: 75W/251 in the first 400 episodes vs 39W/250 in the last
400, Fisher p = 0.0002. The mix head IS moving (teacher-preset share 0.29 ->
0.12) -- PPO is learning, and what it is learning is worse play. This is the
clearest signal in the night's data and I read it backwards. Loop stopped;
policy and CSV archived under `~/neurotica/archive/`.

## 3. No cross-configuration conclusion in this log survives the statistics

Wilson 95% intervals on k/22: 0 -> [0, .15]; 1 -> [.01, .22]; 3 -> [.05, .33];
5 -> [.10, .43]; 6 -> [.13, .48]. Fisher, two-sided: 5 vs 1 p=0.185; 5 vs 3
p=0.70; 6 vs 5 p=1.0; 0 vs 1 p=1.0. Detecting 0.23 -> 0.35 at 80% power needs
~212 games per arm.

So: "population gate rejected", "staffing rejected", "normalising made it
worse, wrong in kind not scale", "PPO has not degraded the BC init", "PPO
optimised for not losing", "ratio head: best result vs nicowar", "first win
against castor" -- **none of these are distinguishable from noise.** And
`eval.sh` draws a fresh generator, map seed and game seed per game, so arms are
unpaired on top of that; it caps by wall-clock on the GPU self-play was using,
so cap counts are load-dependent; and it never records which server flags a
run used. I wrote a rule about n=1 and then ran every decision on n=22 as if
that were different. It is not, at these effect sizes.

## 4. "Training and deployment are aligned" is false in four ways

Eval calls `net(x)` and never samples `head_mix`, so the one head PPO can now
move for production is never exercised by eval. The per-type budget trims in
eval but only excludes at-cap cells in self-play (which then draws k with
replacement). The deadlock breaker exists only in eval; self-play falls to a
uniform over ALL cells, including covered ones -- re-admitting the anchor
hazard at a low rate. k=48/25 ticks vs k=8/100 ticks. I noticed period and
sample, wrote "aligned", and never checked the rest.

## 5. `budget_mask` is not observation-only  [the 07:05 claim is wrong]

It reads `out["count"]`, which reads the pooled trunk features PPO is moving.
So the mask at re-scoring differs from the mask that acted; a stored cell that
becomes disallowed gets prob ~0, logp ~ -16, ratio ~ 0, and silently drops out
of the gradient. `allclose True` held for identical weights, which is not the
PPO situation.

## 6. Anchor detection breaks when two same-type buildings touch

Up/left test within one type plane: a second adjacent inn is not an anchor,
gets DONT_CARE, is undercounted by the budget, is never restaffed, and for a
1x1 flag the reconciler treats it as an orphan and MOVES it. The reconciler's
own nearby-placement makes adjacency common.

## 7. Smaller but real

* `mix_all` guard drops the mix term for a whole batch if any episode lacks
  one -- inflating ratios ~5x for the ones that had it.
* The DONT_CARE harness test never places DONT_CARE at an anchor, which is the
  case the decoder actually produces; the "never demolishes" half is vacuous.
* `NeuroticaPolicySocket.h` still documents NPS2 / 3 bytes; `neurotica_ppo.py`
  docstring still says the action is the latent. `TYPE_CELLS` is dead.
* `--count-only` selects `best.pt` on P@5, which a frozen trunk cannot change,
  so `best.pt` is always epoch 1 -- the auxiliary heads I credited with "3
  epochs" got one. The served ratio checkpoint has count_mae 0.367, not the
  0.3545 I logged.
* Staffing is applied to NEWLY placed buildings too, overriding the engine's
  `maxUnitWorking` at creation -- an under-prediction directly slows
  construction. That is a plain alternative explanation for staffing "hurting".
* The engine changes ARE additive per CLAUDE.md (only reachable when Neurotica
  is selected; no save/replay format touched).

## 8. The "governing lesson" is narrative fitted to noise

Every instance was n=1 or p >= 0.185, and the revision from "rescale" to
"hidden preconditions" was made to fit 0/22 vs 1/22 (p=1.0) and is
unfalsifiable as stated. Alternatives consistent with the same data: sampling
noise; one-epoch heads with ~40% relative error; staffing overriding creation
defaults; the ratio re-sent every step so swarms thrash. The one part that IS
right: heads outside the action get no policy gradient.

## What stands

The anchor/footprint bug and its fix (mechanism, reproducible, harness-tested).
DONT_CARE passthrough. act/evaluate consistency under fixed weights. The data
loader indexing. The PFSP code. The finding that non-action heads get zero
gradient. The GUI/replay tooling. Everything else in this log that compares
one configuration to another is unestablished.

## Reviewer's recommended order, which I agree with

1. A paired eval: fixed (generator, map seed, game seed) triples shared by
   every arm, >=100 games per arm, tick cap not wall-clock, a GPU not shared
   with self-play, server flags logged into the result. Re-measure inert,
   `ckpt_staff`, and the archived PPO snapshots. Until this exists nothing has
   been shown to beat doing nothing.
2. One decode for eval and self-play; store the budget mask with the
   trajectory instead of recomputing it from a moving trunk; deadlock-breaker
   instead of uniform fallback; snapshot `policy.pt` every N iterations; make
   the mix choice sticky rather than re-drawn every 100 ticks.
3. Fix anchor detection (an anchor plane from the observation) before trusting
   any count-budget number.

The sequence/phase question is premature: tick planes are already in the
input, so "phase" is available to the network now. The binding constraints are
measurement and the training/deployment mismatch, not representation.

## Paired eval built (13:50)

`tools/glob2-rl/paired_eval.py`. Every arm plays the same manifest of
(opponent, generator, map seed, game seed) -- 25 per opponent, 100 games per
arm -- with a tick cap instead of a wall-clock cap, the arm's exact server
flags written into every result row, and Wilson intervals in the summary.

Even at 100 games per arm the resolution is coarse: 25/100 is [18%, 34%] and
35/100 is [26%, 45%], so a 10-point difference barely separates. Anything
smaller than that is not going to be decidable at this budget, and the night's
"results" were mostly claims of 5-15 point differences on 22 games.

First two arms running concurrently: `inert` (no policy server; the
do-nothing AI the night's headline was measured against) and `staff_topk`
(`ckpt_staff`, `--top-k --placements 48 --use-count`, the best BC decode).
This is the question the review said had to come first: does the best playing
configuration beat doing nothing at all, on the same 100 games?

---

# Afternoon: acting on the review (13:50-18:30)

## The paired eval settles the headline

100 shared games per arm (25 per opponent, same maps and seeds, 60000-tick
cap, flags recorded per row):

```
                       numbi        castor       warrush      nicowar      TOTAL
inert (do nothing)   61.9% [41,79]  22.2% [6,55]  35.0% [18,57]  4.8% [1,23]  32.4% [23,44]
best BC decode       68.4% [46,85]  16.7% [6,39]  31.8% [16,53]  4.8% [1,23]  30.0% [21,41]
```

**The playing AI does not beat doing nothing.** The intervals overlap almost
entirely. The do-nothing AI beats numbi 62% of the time on its own, which is
why every "winning record against numbi" in this log meant nothing.

## Fixes landed (commit 41f08b2d2 and after)

* `neurotica_decode.anchors`: level-aware footprint tiling with wrap and claim
  tracking. Self-test covers adjacent, stacked, wrapped and level-2 buildings.
* One decode path for eval and self-play; same budget, trim, deadlock
  handling, k=8 and period 25. The deadlock breaker is now gated on a small
  base -- ungated it handed the top type a slot every step (hospital=8 vs cap 4).
* The allowed mask and mix-decide flag are stored per step and loaded back, so
  PPO scores the distribution that acted. Verified exact agreement with stored
  context, no placements on covered cells even on the fallback path.
* Mix held for 10 steps; mix term in the log-prob only on decide steps.
* PPO drops episodes lacking action context; snapshots every 25 iterations.
* Harness 75/75 incl. DONT_CARE at an anchor. Header documents NPS4.
* `--count-only` selects on the aux metrics. The loop only kills its own server.

Two bugs caught by smoke tests before they cost anything: a mangled import
line, and the dataclass field order. Both would have shown up as a silent
inert AI or a dead learner.

## Running now

* `ratio_unified` eval arm: `ckpt_ratio`, unified decode, k=8, period 25.
* Self-play from `ckpt_ratio` on the fixed loop. Rollouts ~170 games/h (the
  25-tick period is 4x the inference of the old 100), snapshots every 25 iters,
  `episodes.csv` accumulating. iter 0 mean_return -0.16.

The bar is now explicit and low: **beat 32.4% on the paired manifest.**
