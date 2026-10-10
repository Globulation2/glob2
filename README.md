# Reproducible development evidence for PR 1045

Read [report.md](report.md) for results and limitations. This snapshot contains one clean reserved-core screen, the rejected unreserved polling screen, native inventories, binary/source/dependency receipts, an exactness receipt, and the retained generated fixture. It is an evidence branch, separate from implementation history. No performance gate has passed.

## Contents

- `raw/reserved-poll0-screen/metadata.json`: frozen configuration and runner/analysis hashes.
- `raw/reserved-poll0-screen/measurements.jsonl`: all paired rounds, startup samples, failures and resource inventories.
- `raw/reserved-poll0-screen/summary.json`: scenario estimates and explicit unavailable aggregate.
- `raw/reserved-poll0-screen/candidate-cpu-comparison.json`: same-source GPU/CPU comparison.
- `raw/candidate-gpu-screen/`: earlier contaminated polling wave, retained but rejected.
- `raw/native-second-unit/` and `raw/native-second-engine/`: exact testcase inventories and JUnit.
- `raw/native-build/`: immutable binary/config/dependency identities, integration revision, and build commands.
- `raw/candidate-second-exactness/`: per-tick world/simulation sidecar equality receipt.
- `fixtures/initial.game.gz`: retained generated512²open seed91001 game, SHA-256 `397bec44126f4611773829fd9093bf8d66bbf1b0c7410c9302baf5945ee522f7`.
- `tools/`: exact runner and analysis sources used for the clean screen. Their hashes match its metadata.
- `raw/reservation/`: audited local CPU-partition helper and verification receipts.
- `SHA256SUMS.json`: evidence-file identities.

## Reproduction

Build PR CPU revision `13843165bd7e778650142fe884c7af37b40b56f1` and candidate `3f394a366cc2ba47eae32853178d0cf89dcc8ac6` in isolated checkouts with the same compiler, release flags, SDL/recording prefixes and assets. Use `scons -j8 release=1 server=0`; source/dependency/binary identities are recorded in the manifest and build receipts. New compiler/dependency combinations require new receipts and binary hashes; do not pretend they reproduce the archived binary identity.

Reconstruct a configuration from `raw/reserved-poll0-screen/metadata.json`'s `configuration` object, adjusting binary, fixture and receipt paths to the local machine while recording the changes. It uses8compute slots for every variant,8,192ticks,1,024warmup ticks, identical retained fixture, and explicit GPUordinal0. Forced GPU settings are:

```sh
GLOB2_GRADIENT_BACKEND=opencl GLOB2_GRADIENT_PLAN=frozen8 \
GLOB2_OPENCL_DEVICE=0 GLOB2_OPENCL_CHECK_INTERVAL=8 GLOB2_OPENCL_POLL_US=0
```

On the original machine, audited `cpu_partition.py setup` reserved physical cores/siblings8–15,24–31 as an exclusive cgroup partition. The helper's `run --` launcher enters that partition and drops back to the invoking user before execution. Review hardware topology and helper configuration before using it on another machine; CPU IDs are machine specific. Plain `taskset` does not reserve cores against unrelated processes.

Run the screen through the reserved launcher using one shared resource lock:

```sh
python3 tools/benchmark_gpu_offload.py config.json \
  --output new-screen --lock /path/to/shared-gpu-offload-resource.lock
```

The runner verifies binary/fixture hashes, cgroup state, identical affinity and actual GPU execution; it preserves every sample. Builds and timed campaigns share the lock. Retain a new frozen manifest and receipts before each new run. The archived unreserved results must never be used for qualification.

For correctness, implementation `test/verify_gpu_offload.py` runs CPU/GPU against the same fixture, requiring equality at every tick and of the simulation/entity sidecar. Correctness exports are separate from performance windows.

For final acceptance, the larger map/phase corpus, strongest CPU comparison, real rendering, compatibility/failure matrix, tuning-on overhead and independent held-out confirmation still need to pass. This screen cannot substitute for them.
