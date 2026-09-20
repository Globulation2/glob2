> Historical field-controller handoff. The active order-based pipeline and restart instructions are now in [../README.md](../README.md); commands and checkpoints below describe the superseded experiment.

# AI Neurotica — handoff

Written 2026-09-20, when training was stopped and the rig cleaned. This is the
state of the work, the results, and the lessons. `NIGHT-LOG.md` is the running
diary; this file is the summary you would want if you picked the project up cold.

## What the project is

Train a deep network by RL to play Globulation 2, learning **all spatial
aspects** — building placement, flags, area painting — rather than acting as a
strategist on top of hand-written controllers.

The architecture is a **desired-state field plus a reconciler**. The network
emits, per map square, the state it wants to exist. An execution engine diffs
desired against observed and issues the orders that close the gap. Control is
level-triggered, so re-asserting a desire that already holds is a no-op and the
policy can be stateless.

Standing constraints for any future run:

- Maps are **always randomly generated from randomly chosen generators**, never
  built-in maps. 128x128 minimum.
- Teachers are **pooled**, not conditioned on.
- Determinism was deliberately deferred.

## Where the code lives

Branch `ai-neurotica`, renamed 2026-09-20 from `atlas-desired-state-m0`.

| Piece | Path |
| --- | --- |
| Policy network, heads, action sampling | `tools/glob2-rl/neurotica_net.py` |
| Observation encoding | `tools/glob2-rl/neurotica_obs.py` |
| Anchor/footprint decoding | `tools/glob2-rl/neurotica_decode.py` |
| Policy server (the one decode path) | `tools/glob2-rl/neurotica_serve.py` |
| PPO learner | `tools/glob2-rl/neurotica_ppo.py` |
| Self-play driver, PFSP matchmaking | `tools/glob2-rl/neurotica_selfplay.py` |
| Paired evaluation harness | `tools/glob2-rl/paired_eval.py` |
| GUI play / replay tooling | `tools/glob2-rl/play.sh`, `record_replays.sh`, `watch.sh` |
| Engine-side AI and socket | `src/ai/neurotica/` |
| Reconciler test harness | `test/NeuroticaReconcilerHarness.cpp` (75 checks) |
| Win-probability model, ported from PR #339 | `src/WinProbability.{h,cpp}`, `src/WinProbabilityModel.h` |

## The result, stated plainly

**No configuration ever became distinguishable from a do-nothing AI on wins.**

Every arm below is the same fixed 100-game paired manifest (opponent, generator,
map seed, game seed all held fixed across arms), so the comparison is paired and
the test is McNemar on per-game outcomes. `+n/-m` is games this arm wins that
inert loses / games inert wins that this arm loses.

```
arm                      W   L  cap    wins  score  McNemar-vs-inert
inert                   23  48   29   23.0%  37.5%
margin_T10              25  31   44   25.0%  47.0%  +7/-5  p=0.774
margin_T20              24  30   46   24.0%  47.0%  +5/-4  p=1.000
ppo075_sampled          14  77    9   14.0%  18.5%  +1/-10 p=0.012
ppo160_greedy           20  54   26   20.0%  33.0%  +4/-7  p=0.549
ppo160_sampled          19  64   17   19.0%  27.5%  +5/-9  p=0.424
ratio_sampled           14  78    8   14.0%  18.0%  +2/-11 p=0.022
ratio_unified           24  29   47   24.0%  47.5%  +5/-4  p=1.000
run2_049_sampled        20  57   23   20.0%  31.5%  +4/-7  p=0.549
run2_174_sampled        14  72   14   14.0%  21.0%  +0/-9  p=0.004
run3_058_sampled        20  57   23   20.0%  31.5%  +4/-7  p=0.549
staff_topk              24  56   20   24.0%  34.0%  +7/-6  p=1.000
wp621_sampled           20  30   50   20.0%  45.0%  +1/-4  p=0.375
```

Read it honestly:

- The best arms (25.0%, 24.0%) are **behaviour-cloned** policies, and they are
  statistically indistinguishable from inert (p = 0.77, p = 1.00).
- Three RL arms are **significantly worse** than doing nothing (p = 0.004,
  0.012, 0.022). Self-play made the policy worse than inaction, repeatedly.
- `wp621_sampled` — the win-probability-shaped run — ran at `--max-ticks 40000`
  while every other arm ran at 60000, because I launched it on the harness
  default. Its row is not directly comparable to the others. Recomputing inert
  truncated to 40000 ticks for a fair pairing gives inert 23W/42L/35cap, so the
  conclusion (20.0% vs 23.0% wins, not better) holds either way.

### The single most informative number

Cross-tabulating `wp621_sampled` against inert on the shared games:

```
 inert W   : wp621 {W: 19, L:  1, cap:  3}
 inert L   : wp621 {W:  0, L: 22, cap: 26}
 inert cap : wp621 {W:  1, L:  7, cap: 21}
```

**Nineteen of the policy's twenty wins are games the do-nothing AI also wins.**
Exactly one win comes from a game inert loses. The win column is almost entirely
opponents defeating themselves, not the policy doing anything. Any future
evaluation should report this cross-tab, not just a win rate: a win rate alone
cannot tell a competent agent from a passenger.

What the policy *did* learn is **survival**. It converts 26 of inert's 48 losses
into tick-capped games and cuts losses from 48 to 30. That is real, and it is
not nothing — but it is not winning.

### Trend over the final run

4,970 self-play episodes, broken out per opponent because PFSP shifts the
opponent mix as the policy changes and the aggregate is therefore confounded.
Buckets of 300 episodes, chronological:

```
castor   n= 275 ( 5.5%) W= 10 L= 81 cap= 184 |  0%  7%  5%  6%  0%  0%  0% 11%  5%  4%  7%  0%
nicowar  n= 260 ( 5.2%) W=  7 L=131 cap= 122 |  9%  0%  0%  0%  0%  0%  7%  6%  0%
numbi    n=2440 (49.1%) W=768 L=493 cap=1179 | 32% 31% 31% 31% 28% 35% 26% 31% 31% 31% 27% 41% 23% 35% 34% 38% 27%
warrush  n=1840 (37.0%) W=361 L=713 cap= 766 | 22% 21% 24% 19% 26% 25% 18% 22% 17% 12% 12% 24% 20% 25% 16% 17% 11%
cortex   n= 155 ( 3.1%) W=  0 L= 62 cap=  93 |  0%
```

numbi flat at ~31% across two days. warrush drifting **down**, 22% to 11-17%.
castor and nicowar never above noise. cortex never won once in 155 games.
Mix-preset shares stayed uniform start to finish (0.199/0.200/0.160/0.217/0.223
to 0.203/0.201/0.205/0.195/0.195) — the production-mix head never developed a
preference, which is what a 0.05 entropy coefficient applied to a head that only
acts on 10% of steps will do.

## Why it did not work — the honest reading

The reward is `loss -1 / cap 0 / win +1`, with potential-based shaping on the
mid-game win probability. That is an exact affine image of tournament points
`(W + cap/2)/n`: **stalling costs half price**. Combined with ~250 win-probability
readings per episode that reward *holding* a favourable board, the objective pays
well for turtling and we got a turtle. The agent is optimising what it was told
to optimise. This is a specification problem, not a bug.

**The unresolved design question, and it is the blocking one:** should the project
maximise **wins**, or **tournament points where a cap is a draw**? Everything
downstream — reward, shaping, eval metric — follows from that answer. It was put
to the maintainer and never settled. Settle it before writing another line of
training code.

Secondary suspects, all identified and none actioned:

- `gae()` treats truncation as termination (`next_v = 0.0` at `t = T-1`), so
  every tick-capped episode is bootstrapped as though the world ended.
- A timed-out glob2 is labelled a draw (`winner = -1` -> outcome 0.0) and is
  therefore rewarded above a loss, reinforcing the stall.
- gamma = 0.999 with lambda = 0.95 gives roughly a 20-step advantage horizon on
  ~1600-step episodes. Dropping gamma to ~0.99 was recommended twice.
- Training is ~87% numbi + warrush while evaluation weights four opponents
  equally, so the eval measures something the run barely practises.

## Lessons learned

These are the ones that cost real time. Most are about method, not about RL.

### Measurement

1. **Never conclude from a handful of games.** n=1 and n=22 conclusions were
   drawn at least four times, after the rule had already been written down. The
   fix that finally worked was structural: `paired_eval.py` with a fixed shared
   manifest, ~100 games, Wilson intervals, and McNemar on paired outcomes.
   Comparing arms as independent samples throws away the pairing and badly
   overstates uncertainty.
2. **Pick the metric before you look.** `score = (W + cap/2)/n` correlates 0.965
   with cap rate. It was measuring draws, and it made stalling look like
   progress for days. Wins vs the do-nothing baseline is the metric that means
   something.
3. **Always carry a do-nothing baseline, and always cross-tabulate against it.**
   Inert wins 23% of games on this manifest because the hand-written AIs lose to
   themselves that often. Without that reference, 20-25% looks like competence.
4. **Check the arithmetic in the baseline itself.** The whole "first to beat the
   do-nothing baseline" claim rested on reading `3W|1W|0W|1W` as 4 wins. It sums
   to 5.
5. **Hold every harness flag fixed across arms.** The last arm silently used the
   harness default `--max-ticks 40000` against everyone else's 60000, which
   changes the win/cap split directly. Log the flags with each row — the harness
   does this now, which is the only reason the error was caught.
6. **Report the aggregate broken down by opponent.** PFSP changes the opponent
   mix as the policy changes, so the aggregate win rate moves when nothing about
   the policy's strength has.
7. **Never read a minibatch-loop variable as an update average.** `pg`, `vloss`
   and `ent_mix` are assigned inside the inner loop, so the log shows the *last
   minibatch only*. With `--minibatch 12` and a head that acts on 10% of steps,
   `ent_mix` printed exactly 0.0 on 428 of 795 iterations and looked like
   entropy collapse. It was not; the stored mixes were near-uniform throughout.

### Bugs that invalidated work, and what they have in common

8. **The placement margin, not `1 - P(none)`.** The per-cell softmax is over 14
   classes — "if something is here, what is it" — and is *not* a distribution
   over *where*. Weighting cells by `1 - P(none)` gave median 0.9988 with 84% of
   legal cells above 0.99; the resulting draw had entropy 9.7014 against
   `ln(n_legal) = 9.7031`. **Uniform to four decimals.** Every sampled number
   before 2026-09-18 measured a uniform random placer. The informative quantity
   is `logit[best type] - logit[none]` compared across cells. Fixing it moved
   16.7% to 46.9% with no training at all.
9. **Anchor versus footprint.** The desired field is anchored top-left; the
   observation marks every covered cell; the reconciler keys `observed_` on the
   anchor alone. Repeating a type on a covered non-anchor cell therefore
   requests a *new* building. This produced swarms of 4 -> 28 -> 48 -> 80
   buildings with zero placements in the field. `neurotica_decode.py` now does
   level-aware anchor detection (the naive `roll`-based rule missed the second of
   two touching same-type buildings) and the harness pins the hazard.
10. **One decode path.** Evaluation and self-play diverged in four ways after I
    had declared them aligned — budget, per-type trim, deadlock breaker, and
    disallowed-draw masking. Training and deployment must run literally the same
    function, not two functions believed to agree.
11. **A crash became a silent do-nothing.** A local named `sel` shadowed the
    `selectors` object; the server died; Neurotica fails *inert*, so 96 games
    completed and recorded plausible numbers as a do-nothing AI. **Make the
    failure mode loud** — an AI that silently degrades to passivity will happily
    produce a full results table.
12. **Skipping a "trivial" dependency skipped the build.** Omitting a two-line
    `const` change in `GameHeader.h` meant `WinProbability.o` never compiled,
    while scons exited 0.

The thread through 8-12: **every one of them produced plausible-looking output.**
None announced itself. The defence is not care, it is instrumentation — assert
the invariant, test the decode against a known case, and make the degraded path
crash rather than coast.

### Operational

13. **`pkill -f` matches your own shell.** It killed my SSH session at least five
    times (exit 255), and once killed a running evaluation's own policy server so
    its games silently ran inert. Run restarts from **script files** so the
    invoking command line cannot match the pattern, or anchor the regex with `^`.
14. **One socket per purpose.** A stray server bound `/tmp/neurotica_sp.sock` and
    stole it from the running loop; self-play sat dead for four hours.
15. **Don't interrupt scons.** Repeated 120-second timeouts left a partial object
    tree, and the resulting link failure looked exactly like a broken change.
16. **`timeout` does not exist on macOS** — `exit=127` is not a build failure.
17. **Check what `--init` points at.** Initialising from `ckpt_ratio` instead of
    `ppo/policy.pt` discarded ~166 iterations.

### Things that did work

18. **Potential-based shaping is safe.** `gamma*Phi(s') - Phi(s)` is policy-invariant
    and telescopes to `Phi(terminal) - Phi(start)` *exactly at gamma=1* — verified
    path-independent: steady climb, late surge, and dip-then-win all gave
    `+1.04999995` against an expected `+1.05`. An earlier "MISMATCH" was my test
    expecting exact telescoping at gamma<1.
19. **Rollout compression.** 3.19 GB to 26 MB, **122x**, byte-exact.
20. **Spawning an independent adversarial reviewer.** Two reviews, two critical
    findings that each invalidated days of work, both invisible from inside my
    own accumulated context. Give the reviewer the commit range and the notes —
    not the conversation — and make it read-only.

## Rig state at shutdown

`therig.local`, 2x RTX 2070 Super (sm_75: fp16 tensor cores, **no bf16** —
`torch.cuda.is_bf16_supported()` returns True and lies), 32 cores.

All processes stopped: learner, policy server, self-play driver, evaluation
harness, and stray `glob2` games. Both GPUs idle at 0%. Working data, logs,
rollouts, snapshots and evaluation results removed.

## What was deleted, and what that costs you

At shutdown `~/neurotica` (36 GB) was removed from therig at the maintainer's
request. Only the weights were kept, outside the tree:
`~/neurotica-final-policy.pt` and `~/neurotica-best-snapshot-621.pt`.

Gone and **not recoverable**: the rollouts, the PPO snapshot series, the 28 GB
BC corpus, `ppo/episodes.csv`, `eval/results.csv`, the recorded replays, and
`eval/manifest.json`.

Two consequences worth knowing before you trust anything above:

- **Every number in this file is a summary of deleted data.** The tables were
  computed before the delete and transcribed here; nobody can re-derive them
  from artifacts. Treat them as a record of what was observed, not as evidence
  a reviewer can independently check.
- **The paired manifest cannot be reproduced exactly.** `paired_eval.py make`
  is deterministic given `--seed 20260917`, but it draws generators from
  `$ROOT/generators.txt`, which lived only on the rig and is not in this
  repository. The generator registry has also gained entries since the list was
  built (it had 46), so regenerating it today yields a different list and
  therefore a different manifest. A future run must build a fresh manifest,
  **re-measure the inert baseline on it**, and compare only within that
  manifest. The 23.0% inert figure above is not portable to a new manifest.

If this project restarts, commit `generators.txt` alongside the manifest so the
evaluation is reproducible from the repository alone.

## If you restart this

Do these before resuming training, in this order:

1. **Settle wins-versus-points with the maintainer.** Nothing else matters until
   the objective is decided.
2. Fix truncation bootstrapping in `gae()`; stop labelling a timed-out game as a
   draw that scores above a loss.
3. Drop gamma to ~0.99.
4. Make the eval opponent mix match the training mix, or make the training mix
   match the eval.
5. Re-establish the inert baseline first, then treat any arm that does not beat
   it on **wins**, by McNemar on the shared manifest, as a failure regardless of
   how good the score looks.
