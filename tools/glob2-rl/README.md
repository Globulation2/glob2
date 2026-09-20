# Neurotica: order-based training

The active pipeline is `neurotica-orders-v1` (NPS6). It replaces the historical
future-occupancy controller. **Old checkpoints and `.aob`/`.atr` training data
must be regenerated.** The old trace/oracle tools remain diagnostic utilities;
they are not the input to the current BC trainer.

## What changed and why

* BC records the teacher's actual order before execution. It no longer asks a
  network to infer which actions produced an occupancy map 500 ticks later.
  Every issued order and a hold at most every 25 ticks becomes an example.
  Delays are derived from the next recorded decision, clipped to 1–25 ticks.
* One autoregressive distribution chooses operation, building type or entity,
  target, relevant parameters, and delay. BC, serving, PPO, and evaluation all
  call `NeuroticaNet.score_action`. GID pointers identify touching and wrapped
  buildings exactly. The live policy bypasses the old field reconciler.
* Controls include construction/repair, cancellation, demolition, staffing,
  swarm mix (including all-zero production), flag movement/radius/minimum level,
  priority, clearing resources, exchange masks, area additions/removals, and
  resource sharing within fixed two-team alliances. Diplomacy changing the
  match's alliances is deliberately outside this two-player training task.
* Engine construction can queue a site while units move away. Create coordinates
  are normalized as the engine normalizes them. Teacher creates blocked by hard
  obstacles become explicit holds, logged as `NEUROTICA_TEACHER_NO_EFFECT`;
  checksum tests verify that this does not change their games. Unknown order
  types and unsupported parameter ranges fail instead of disappearing silently.
* Opcode and delay BC weights do not shrink with the number of parameters.
  Area loss is normalized by cell count. Validation uses the identical loss
  and reports **full typed order accuracy**, including holds and delays, plus
  per-operation counts/accuracy. Whole map seeds, including both teams and all
  their frames, stay on one side of the train/validation split.
* The circular U-Net sees current and previous observations, exact own-building
  attributes, additional own-unit state, and less saturated distance/staffing
  features. Entity summaries and explicit elapsed time reach the opcode/value
  heads; spatial targets also have periodic coordinate features. Two frames
  are limited history, not a recurrent belief state or perfect strategic memory.
* All sampled controls contribute to the joint PPO likelihood. No top-k/count
  pruning, hidden deterministic mix decoder, or untrained control head decides
  what is actually sent. A single immutable policy collects each generation.
  Trajectories stay compressed in memory and are decoded on demand.
  PPO verifies every stored behavior likelihood before its first update,
  backtracks optimizer steps that exceed the sampled KL bound, checks final
  rollout-wide KL, and preserves the BC reference. Large area distributions
  can require much smaller optimizer steps; backtracks/final KL are logged.
* A configured policy failure invalidates the match. Infrastructure timeouts,
  incomplete trajectories, stale weights, and missing evaluation pairs cannot
  silently become draws or inert-policy results. Evaluation preserves the
  manifest, maps, seeds, configuration, checkpoint/binary hashes and results.

## Dense reward contract

The calibrated winning-probability scorer was connected in the old pipeline;
connecting a predictor alone did not repair the action/likelihood defects.
The current potential is `Phi = p(win) - 0.5`. Each transition gets

```
F_t = shaping * (gamma ** (elapsed_ticks / 25) * Phi_next - Phi_t)
reward_t = F_t + terminal_win
```

The potential at every terminal state is **zero**, including a tick cap. The
finite task is win=1, loss/draw/cap=0. Infrastructure timeout is not a cap and is
excluded. Default gamma=1 therefore optimizes winning by the cap, and shaping
sums to `-Phi_initial`, independent of the path taken. There is no terminal
bonus for sitting at a favorable predictor score. GAE discounts also account
for elapsed ticks. The helpers reject truncation without explicit bootstrap
values; the current runner collects complete finite games only.

The scorer is evaluated from the opening onward for shaping. This does not
change the engine's minimum tick for probability-based victory adjudication.
Opening predictions and predictions on new policy states are **heuristics**;
historical calibration does not establish calibration on this distribution.
Team statistics update periodically, so repeated per-decision potentials are
normal. A learned value function can use the denser differences for credit
assignment, but shaping cannot make an unrepresentable action learnable or
supply strategic information that the predictor does not contain.

Use `neurotica_diagnostics.py ROLLOUT_DIR --out reward-audit.json` to inspect
potential changes, terminal telescoping, action counts and game-weighted
calibration bins. These bins count capped games as nonwins; compare separate
opening/later results before interpreting the scorer as a probability.

## Local validation

Python requires NumPy and PyTorch; install the appropriate PyTorch build for
the host/device. No rig job is launched by these commands.

```sh
scons -j4 release=1 server=0 neurotica-reconciler-test build/src/glob2
./build/src/NeuroticaReconcilerHarness
python -m unittest discover -s tools/glob2-rl -p test_neurotica.py -v
python tools/glob2-rl/test_neurotica_engine.py \
  --root "$PWD" --binary "$PWD/build/src/glob2" --out /tmp/neurotica-engine-check
```

The engine test retains paired maps and per-tick checksum sidecars, exercises
all seven current teachers, validates their corpus, and verifies rejection of
an absent policy server. Run it on each affected platform and compare the
sidecars for identical seeds. Mac-only success does not establish Linux or
Windows equivalence.

## Restart sequence on therig.local (later)

Build this rebased branch on the rig and use a fresh environment/output tree.
Choose explicit generator IDs from `build/src/glob2 --list-map-generators`, one
per line in `generators.txt`; retain this file with the run. Commands below
assume a shell in the repository and the intended Python environment activated.

```sh
python tools/glob2-rl/neurotica_selfplay.py --collect \
  --root "$PWD" --binary "$PWD/build/src/glob2" \
  --generators generators.txt --out runs/teachers --games 140 --parallel 8

# Diagnostic gate: deliberately overfit a few actual orders first.
python tools/glob2-rl/neurotica_bc.py \
  --corpus runs/teachers/generation-00000/games --out runs/bc-tiny \
  --overfit 8 --epochs 200 --width 8 --lr .003 --device cuda

# Separate full training, with held-out map seeds.
python tools/glob2-rl/neurotica_bc.py \
  --corpus runs/teachers/generation-00000/games --out runs/bc --device cuda

python tools/glob2-rl/paired_eval.py manifest \
  --generators generators.txt --out runs/eval-manifest.json
python tools/glob2-rl/paired_eval.py run --manifest runs/eval-manifest.json \
  --arm inert --out runs/eval-inert --root "$PWD" --binary "$PWD/build/src/glob2"
python tools/glob2-rl/paired_eval.py run --manifest runs/eval-manifest.json \
  --arm bc --checkpoint runs/bc/best.pt --out runs/eval-bc \
  --root "$PWD" --binary "$PWD/build/src/glob2" --device cuda
python tools/glob2-rl/paired_eval.py summary \
  --baseline runs/eval-inert --candidate runs/eval-bc

# Only after BC/control gates pass: synchronous PPO generations.
python tools/glob2-rl/neurotica_selfplay.py --init runs/bc/best.pt \
  --bc-corpus runs/teachers/generation-00000/games \
  --root "$PWD" --binary "$PWD/build/src/glob2" --device cuda \
  --generators generators.txt --out runs/ppo --generations 100 --parallel 8
```

Do not interpret tiny-set memorization as generalization, or a completed PPO
update as evidence of stronger play. Require typed non-hold accuracy and inspect
replays, then compare fixed-manifest outcomes against inert and BC checkpoints.
Use the same sampling mode, cap and manifest across policy comparisons. The
summary reports paired gained/lost wins and exact McNemar probability; inspect
the outcome cross-tab as well. The default opponent mix includes Numbi, Warrush,
Castor, Nicowar, Cortex, Maxima and Cabino, without adaptive selection weighting.

A restart uses `--init` from a retained checkpoint and a **new** output directory;
there is no hot reload or implicit resume. BC's `--init` likewise restores
weights with a fresh optimizer. Runtime policy history/delay is session-local;
a loaded game reconnects with fresh policy history. No engine save-format,
network-version or replay-acceptance gates were changed. Old ordinary AIs use
the same simulation path; old learned field weights cannot run through NPS6.
