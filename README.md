# Snapshot resource growth: PR #897 evidence

Final implementation: `e93956015` on `codex/snapshot-resource-growth`. Original matched performance baseline: `d42d3e512` (immediate growth). Final integration reviewed master `591e40ecb`; its abort-session fix was cherry-picked as `e93956015`. Intervening rendering changes are unrelated and were not rebased into this branch. The merge tree is conflict-free.

[Performance and ecology tables](performance.md) contain the measured results and uncertainty. [Build provenance](build-provenance.json), [environment](environment.txt), and [exact commands](commands.txt) identify the inputs and tools. This was a shared, busy Linux x86-64 host with GCC 15 release builds; no CPU isolation was used.

## Findings

The split has a real cost: the immediate snapshot/compute/mutate component path took roughly **1.7–6.6×** the retained legacy algorithm across enabled-growth fixtures. Shared execution recovered part of the delayed owner-only cost, but the engine measurements do **not establish a consistent end-to-end speedup**. The full matrix and refreshed final default campaign are both retained; small effects need a quiet-machine rerun. Shared execution remains the explicitly requested default.

The 20-seed ecology study covers both reserve-preserving and deposit-depleting harvesting. With a reserve, mean final food falls 3.8%, deposits 1.4%, and harvest 1.6%. Under depletion, mean final food falls 16.8% and replenishment 41.0%, while harvest falls 1.2%. These are controlled 512-tick uniform-crop fixtures, not a full-match balance conclusion. Timing, snapshot-only decisions and the new RNG consumption deliberately alter game trajectories.

## Validation

- Full checksum matrix: **168 runs passed** across delays 1/3/8, executor sizes 1/2/4/8, owner/shared execution, three map sizes, idle/active-AI scenarios and a disabled-growth control.
- Final integration: **14 further runs passed** at delay 8/thread count 4, and all world traces match the pre-integration feature revision byte-for-byte.
- Final focused native checks: **39 passed, 1 skipped**, covering growth, shared-worker lifecycle, JavaScript version/session boundaries, saves and the simulation golden record.
- Broad pre-integration compatibility inventory: **1,432 cases passed, 61 skipped, 3 failed**. All three failures reproduced against the preserved baseline. Master's subsequent abort-session fix resolves the fatal-JavaScript teardown case in the final focused run. The remaining baseline failures are JavaScript conversion continuation and 16-bit image channel normalization; their logs are retained.
- Final component/ecology suite: **2 cases passed**, producing 924 component samples (including warm-ups) and 80 ecology samples (20 seeds × 2 harvesting policies × 2 algorithms).
- CI selector tests: **21 passed**. Simulation revision/golden consistency and whitespace checks passed. GCC 13 and Clang 18 syntax checks passed for the kernel and new test/benchmark sources; these are not runtime platform verification.

Save format 144 retains the save compatibility floor of 58. Replay floor 144, protocol 62 and simulation revision 29 distinguish the changed simulation. Tests exercise older saves, pending-batch continuation, paused/stalled ticks, deposit replacement, seeds in flight, capacities, occupancy and out-of-order completion.

The baseline failure probes were built from a source archive. Their embedded TestMain Git metadata incorrectly discovers the parent candidate checkout because the archive lacks `.git`; this is not their actual source provenance. [Baseline source hashes](baseline-probe-sources.json) were compared directly with `git show d42d3e512:<path>`. Only the archive's test registry was locally narrowed; its simulation sources were unchanged.

## Performance evidence layout

- `engine-timing/`: full 2,156-run matrix (seven scenarios × 28 variants × eleven rounds), measured before the abort-path integration. Its metadata records exact executable hashes.
- `engine-default-final/`: 231-run refresh on the final executable (seven scenarios × three variants × eleven rounds), delay 8/thread count 4.
- `verification/` and `verification-integration/`: correctness runs, kept separate from timing; archives contain per-tick checksums and logs.
- `component.json` / `component-summary.json`: final four-way component measurements for 128², 256², 512² maps and sparse, dense, saturated, harvested, blocked, multi-material and disabled scenarios.
- `ecology.json` / `ecology-summary.json`: final 20-seed ecology results, including removals, seeding, replenishment and harvest.
- `component-first.json` / `ecology-first.json`: earlier measurements retained for transparency, not substituted for the final-source component results.
- `tests/`: native results, baseline failure probes, build/syntax logs and compatibility checks.

Compressed measurement rows retain each command, wall/CPU/RSS, load average, full AI/snapshot/gradient metrics, resource counters, compute/queue/join/publication times and pending-buffer peaks. Snapshot capture/copy figures are shared engine totals; their old/new deltas are observational and also reflect trajectory changes. The original binary supplies a tick histogram, not exact percentiles. Growth join time includes final draining as well as deadline waits. The legacy component control runs the old algorithm in the candidate binary; full-engine legacy timings use the preserved old executable.

The full-engine fixtures are generated starting games with a no-op JavaScript controller or active Nicowar/Warrush controllers. Controlled dense/blocked/multi-material scenarios are covered by the component benchmark, rather than an exhaustive full-engine Cartesian product. Each engine run lasts 256 ticks. No claim is made about long-match performance or every map generator.

## Reproduction

Check out the implementation and build using the recorded dependencies/options. Fetch this evidence branch into a separate directory, then, from the implementation checkout:

```sh
python3 /path/to/evidence/prepare_manifest.py > artifacts/resource-growth-review-manifest.json
LD_LIBRARY_PATH=/path/to/SDL/prefix/lib python3 test/benchmark_resource_growth.py \
  /path/to/candidate-glob2 artifacts/resource-growth-review-manifest.json \
  --baseline /path/to/baseline-glob2 --output artifacts/resource-growth-retest
```

Run `--verify` separately. The manifest builder resolves the supplied initial saves to absolute paths while preserving their SHA-256 checks. `commands.txt` gives the component and focused-test commands. `summarize.py` regenerates the tables when called from the implementation checkout with the evidence directory as its argument.

## Play and remaining coverage

`fixtures/ai256/initial.game.gz` plus `playable/legacy/final.game.gz` and `playable/delayed/final.game.gz` provide matched playable before/after examples after 256 ticks. Use the baseline executable for the legacy trajectory and the candidate for delayed growth. The final saves were generated before the abort-path integration; the final integration checks establish unchanged normal simulation traces.

Actual Windows/macOS/ARM/WebAssembly determinism and a threadless-platform build were not available here. Owner execution and a one-slot executor were tested, but they do not replace threadless platform coverage. Display tests and maintainer play assessment remain outstanding. The PR stays draft; this evidence does not constitute maintainer acceptance or a complete platform qualification.
