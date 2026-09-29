# Cortex placement and resource hiring: review evidence

This pass builds on PR #393. Source revisions are recorded in `revisions.json`; the baseline already contains the earlier Maxima placement coordinate optimization. The additional production change is three commits: Cortex resource-presence scan order, Cortex placement distance eligibility, and packed building-hiring candidates. No gradient or pathfinding implementation is changed.

## Discovery and independent experiments

`profiling/` contains flat reports, caller reports, sample-period aggregates and exact commands for five 2,000-tick game windows. A release executable was relinked without stripping symbols. perf sampled user-space cycles at 99 Hz with DWARF stacks; events started disabled and were enabled when the engine printed its game-start marker, excluding save loading. Both pooled sample-period shares and equal-game shares are retained. These are CPU sampling shares, not predicted wall-time gains.

- Cortex placement was 5.4% self time in the mixed 512×512 game. Late islands spent another 5.1% in rectangle distances. Caller stacks attributed 2.16% to placement's tile checks, versus 0.78% in ground reachability. The prototype precomputes the exact accepted region for the existing distance limit, keeping candidate order and policy unchanged.
- Boolean fruit/algae presence scans traversed columns of row-major map storage. Only these existence queries change order; their reachability routines do not.
- Hiring candidate gathering was 3.5% in the non-Maxima control. Packing accepted candidates avoids clearing and rescanning empty slots while preserving all eligibility predicates, rejection tallies and destination-purpose side effects.
- Maxima food-reach flood fills, Cortex reachability fields, and gradient routines remain substantial costs but were excluded. Shared tile predicates are unchanged; the placement caller performs less work. Smaller TeamStats/resource-growth costs were considered without introducing stateful caches or reordering random updates.

`screening/` retains the independent experiment results and variant patches. `row-scans` changes only presence scans; `proximity` includes row-scans plus the placement mask; `hiring` changes only candidate packing. The Cortex screens ran on therig, hiring screens on devlaptop; do not compare their absolute times across machines. Warmups are marked repeat -1. Another build overlapped part of the Cortex screening window, so small differences there are not conclusive. Longer control runs test hiring over 10,000 ticks to reduce short-window noise.

`scan-visits/` compares both query orders on the same live state and checks every answer; `scan-counters/` retains auxiliary hardware counters for the isolated change. Scan-order timing benefits vary by workload; the final table measures the combined candidate.

## Final correctness and performance

`performance.md` and `performance.json` summarize final-commit measurements, with all raw samples under `timing/`. Release builds retain one gradient worker and an eight-tick publication delay. Therig timing runs use two distinct physical cores (logical CPUs 8 and 10), one discarded warmup per variant, and three measured repetitions per variant with alternating pair order. The final timing runner waits while compiler processes are active and retries samples with detected compiler overlap; excluded attempts remain in the raw run directories. Simulation elapsed time includes worker drain and excludes loading; process wall/CPU measurements are also retained. The non-Maxima control advances 10,000 ticks; other timing cases advance 1,000. These small samples do not establish confidence intervals or a universal speedup.

`validation/` contains baseline/candidate results from native macOS ARM64, devlaptop Linux and therig Linux. Each of 14 states has 512 consecutive ticks with identical checksum bytes and AI order streams across all six executions. Inputs include eleven earlier large-game states, a historical version-115 save, and two fresh 12-player Cortex games at tick 20,000. Seeds and input hashes are in `scenarios.json` and `cases.json`. Player counts describe starting rosters; the new Cortex snapshots have 11 and nine surviving teams, with 1,348 and 546 units. Reference checksum files and order streams are retained once under Linux baseline directories; other runs retain their hashes rather than duplicating identical trace data. Replay headers are not an equivalence criterion because baseline headers can already vary independently of orders/state.

Geometry, fetch-apportionment and hiring-bucket regressions pass on Linux and macOS. Geometry checks include wrapped bounds, oversized extents, absent/dead/null-type buildings and empty colonies. The hiring test includes sparse units through the final array slot. Logs are included.

`continuation/` retains six pairs of 128-tick checkpoint reloads. Baseline/candidate reloads must match exactly. The original mixed-continents case has an existing uninterrupted-versus-reloaded divergence at tick 12,171 in both versions; the other five cases also match uninterrupted execution. No save fields or compatibility/version gates are changed. Windows and interactive play were not tested.

## Reproduce

Build the exact revisions in `revisions.json` using `scons release=1 server=0`, retaining separate executables and matching game data. Set `E` to this evidence directory and `ROOT` to the source checkout. Output directories must be new.

```sh
python3 "$E/run.py" timing --baseline /path/to/baseline --candidate /path/to/candidate \
  --root "$ROOT" --output /path/to/timing --cpus 8,10 --repeats 3
python3 "$E/run.py" verify --baseline /path/to/baseline --candidate /path/to/candidate \
  --root "$ROOT" --output /path/to/verification --jobs 3
python3 "$E/continuation.py" --root "$ROOT" --verification /path/to/verification \
  --baseline /path/to/baseline --candidate /path/to/candidate --output /path/to/continuation
scons -j6 release=1 server=0 cortex-geometry-test fetch-apportionment-test hiring-bucket-test
```

Run CortexGeometryHarness directly. Run the two hiring harnesses through `test/run-savegame-safety-tests.py --check-preferences`, passing the repository path as the extra argument. Native executable paths use `build/linux/client/release/src/` or `build/darwin/client/release/src/`. Omit CPU pinning on macOS.

Run `python3 check_evidence.py` to verify the bundled input/reference hashes and equivalence manifests. Run `python3 summarize.py` to regenerate the performance table. Historical command files retain original machine-specific paths as provenance; the reproduction scripts use bundled inputs instead.
