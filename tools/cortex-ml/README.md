# Cortex training tools

Behavior-cloning tooling for optional Cortex worker-tuning policies. Start with
[AI development](../../docs/ai/development.md). The maintained inference contract is
[the quantized format](../cortex-ml-infer/format.md), with feature extraction and
policy gates in `src/ai/cortex/`. Training uses seeded NumPy operations.

The default native controller does not require a trained model. Optional runtime
selection is controlled by `GLOB2_CORTEX_POLICY` and `GLOB2_CORTEX_NET`; see
`CortexPolicy.cpp` for the corresponding decision-policy mode.

For trace compatibility, reward definitions and the current 19-candidate runtime
versus 18-class training boundary, read [the training reference](training.md).

## Files

- `dataset.py` — trace-CSV loader. Reads a dir / glob / file of `*.team*.csv`,
  builds the 16-feature matrix `X` in the contract's exact column order and label
  `y = desired - 1` (action 1..20 → class 0..19). Reads cap/clamp constants live
  from `src/ai/cortex/CortexConstants.h` (never hardcoded). Excludes wheat-starved
  rows (`harvestableWheatNearby ∈ [0, CORTEX_SWARM_WHEAT_STARVED_TILES)`) from BC
  by default — those are governed by a hard C++ clamp (cap 1) that bypasses the
  net, so the net should learn buffer control, not memorise that rule. Also
  provides `TraceFile.transitions()`: the per-file gid-join into `(s_t, a_t,
  s_{t+1})` tuples for offline reinforcement learning (BC does not use it). Joins never
  cross files — gid is unique only within one game+team file.
- `mlp.py` — numpy MLP `16 → Dense(32) → ReLU → Dense(32) → ReLU → Dense(20)`,
  manual forward + backprop, softmax cross-entropy for TRAINING only (inference is
  argmax). Deterministic He init. Per-feature μ/σ standardization computed on the
  training set and FOLDED into the exported layer-0 weights so the exported net
  consumes RAW features (`normalization_folded: true`). `export_f32_json` writes
  the `cortex-mlp-f32-v1` format (`W` shape `[out][in]`, `y = W·x + b`).
- `train_bc.py` — CLI trainer + eval + export.

## Run

```bash
python3 tools/cortex-ml/train_bc.py --data artifacts/cortex-corpus --out artifacts/cortex-weights.json --epochs 200
```

`--data` is a directory (globbed for `*.team*.csv`), a glob, or a single file.
Keep corpora, exported candidates and metrics under ignored `artifacts/`.

Flags: `--epochs` (default 200), `--lr`, `--batch-size`, `--val-frac`,
`--include-wheat-starved` (keep the hard-clamp rows in training, for revisiting).

## Metrics reported

All eval uses the contract's eval-time masking: valid actions are
`[CORTEX_SWARM_WORKER_MIN .. swarmWorkerCap]`, where `swarmWorkerCap = LATE` if
`maxBuildLevel >= CAP_LIFT && freeWorkers > 0` else `base`; argmax over unmasked
logits, ties → lowest class index.

- **(a) masked top-1 accuracy** — fraction where masked-argmax = `desired`.
- **(b) exact-match-with-hand-rule (BC parity)** — how often the net's
  masked-argmax equals the hand rule's `desired`. On BC this equals (a) because
  the BC target *is* `desired`; the two diverge post-RL, so both are reported.
- **(c) confusion summary** — per-true-action count, accuracy, and the most
  common wrong prediction.

`train_bc.py` also runs a round-trip self-check: re-loads the exported JSON and
verifies its f32 forward pass reproduces the in-memory net's masked argmax on
sampled rows.
