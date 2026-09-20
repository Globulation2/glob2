# Repair validation, 2026-09-20

Branch: `fix/neurotica-training-contract`. The 64 Neurotica commits were rebased
onto `origin/master` at `d37c0c353d9c2c68b4543286aa9b14e23ae7e526`.
The repair is committed on top of that rebase.

Local validation on macOS:

| Check | Result |
| --- | --- |
| Release engine and reconciler harness build | Passed |
| C++ codec/reconciler harness | 118 checks, 0 failures |
| Python regression suite | 14 tests passed |
| Actual teachers, 7 matchups × 1,500 ticks | Converted/recorded vs ordinary teacher per-tick checksums match for all seven; identical paired map hashes |
| Corpus from that regression | 1,129 valid order records |
| Missing configured policy server | Game rejected as invalid |
| Longer teacher games | Cabino/Cortex and Maxima/Nicowar completed 12,000 ticks each; 2,938 valid records, including construction, movement, radius, minimum level, deletion and area controls |
| Tiny BC on 8 actual recorded examples | 8/8 exact orders, including parameters and delays; loss 0.004854; holds, construction, staffing and swarm mix represented |
| Real socket → engine → trajectory → PPO | 200-tick sampled game, 24 decisions; behavior likelihood verification passed; 3 accepted optimizer steps, 4 backtracks; final sampled KL 0.02640 |
| Probability potential in longer corpus | Range −0.293 to +0.292; opening range −0.168 to +0.167 |

The BC result is a **memorization diagnostic**, using a small model and subsequent
fine-tuning on the same eight examples. It is not held-out accuracy or playing
strength. The PPO game used a randomly initialized policy to exercise the
pipeline; its nonwin is not an evaluation of a trained policy. The 200-tick
trajectory happened to have constant potential; the longer corpus demonstrates
that the scorer supplies changing potentials, including before tick 5,120.

The self-contained engine regression is `test_neurotica_engine.py`. Its manifest
and artifacts are retained in `.tmp/neurotica-review/engine-regression/` in this
workspace. Other logs, maps, teacher data, the tiny BC checkpoint and PPO
trajectories are under `.tmp/neurotica-review/`. A selected evidence bundle is
`.tmp/neurotica-review/validation-artifacts.zip` (maps/checksums, result metadata,
logs, the tiny checkpoint and its source corpus, and PPO trajectory/update).
The generated directory is intentionally not committed as repository test data.

Linux/CUDA, Windows, cross-platform checksum equivalence, loaded-game policy
continuity and full-training playing strength have not been verified here.
No training was started on `therig.local`. Use [README.md](README.md) for the
fresh-data, BC, evaluation and PPO sequence to run there later. The ordinary
engine save/network/replay formats were not changed; the external learning
protocol intentionally rejects the old field checkpoints.
