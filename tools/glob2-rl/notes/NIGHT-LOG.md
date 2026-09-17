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
