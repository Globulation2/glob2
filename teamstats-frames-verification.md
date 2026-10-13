# Complete measurement frame verification

Final source: `7588574389595d6fd51efeac293e55afdfc68ac5` (clean); source tree SHA256 `6581db4a54a7c058a43057d9a00c0eb23662eb04450e693d9ddc1237e38f41c9`. Base: `codex/mcp-test-portability` / `b1f1affb1ca39682c428003250ddac574a6608ed`. Fetched master: `277510840054b1416f2defd7d86fe1431dd0ddc0`; merge-tree preview is conflict-free. Master was fetched before final clean verification; unrelated gradient/rendering changes were not merged or rebased. This validates the feature branch on its recorded base, not execution of the newer master merge tree.

Workers construct complete measurements, including exact cumulative labour and retained periodic diagnostics. Publication swaps the frame pointer, installs consecutive raw-ring rows, and installs worker-finalized aggregates / appends periodic histories when due. Fixed deadlines, pause/read idempotence, K=0 behavior, event collection separation and omitted unpublished final samples remain. One ordered batch job computes the cheap team rows; AI uses separate executor lanes. Replaced frames return through existing batch storage, with shared retention preventing overwrite. No new pool. Old raw calculation remains an independent reference and cold migration path.

Save 158 stores a complete-frame flag; formats 155–157 are completed in source order without early publication. Older pending raw aggregates still retain their exact smoothing mode. Minimum save 58; replay floor 158; net protocol 70; SIM_REVISION 53; regenerated match record verifies.

## Clean final validation

- Native: 128 selected cases passed, no failures/skips (`final-native.xml`), covering executor, snapshots, read boundaries, frame pipeline, shared worker lifecycle, match/replay boundaries, 32-phase save/load/resave/text/corruption and golden record.
- Release: 78 passed, one display-only screenshot case skipped, no failures (`final-release.xml`), including all pipeline/save/AI cases. Full older 84/88 and format152 fixtures pass. Frame tests compare every measurement through tick519 for K=0/1/2/8 and zero/two workers, retain immutable prior frames, and exercise format157 deltas with nonzero baseline labour through load/resave/continued submission.
- `checksums.txt`: 32 advancing configurations ×520 ticks =16,640 full checksum records; all16 zero/two-worker pairs match. Profiling on/off and repeated-trial final checksums match within each binary/configuration. Full pending results are hashed; worker readiness is excluded.
- `sim-revision.log` verifies the gate/golden record. The initial broader run's stale version assertion (157/69) was repaired to158/70; final clean run repeats that full case successfully. Broader initial engine/MCP coverage and the resolved failure are retained in the archive.

Native macOS ARM64, Apple LLVM21, C++20. Debug: dev_fast=1, -O0 -g1; release: release=1, dev_fast=0, linker=default, -O3 (ordinary compiled game objects, focused engine-test link). Exact commands, build logs, link argv, compiler/provenance and test selection are archived. Runtime dependencies are the existing pinned SDL prefix plus Homebrew libraries; no dependency changes in this follow-up. GUI/GPU screenshots, Linux/Windows/browser/mobile per-tick execution, sanitizer and release packaging checks, hosted CI and maintainer gameplay review are not verified. The PR stays draft.

## Paired release profiling

Five paired trials, seed1234567, 2048 advancing ticks, profiling on/off, SDL dummy drivers, `GLOB2_PERF_DISABLE=1`. `profile.py` is the exact workload driver; `analyze.py` recomputes medians and validates checksums. The static world has two teams and256 total units; K=8 with two workers. Capture is the full shared consumer capture, not statistics-only cost. Publication excludes ticket joining/snapshot release/batch retirement, includes both teams and the amortized32/512 boundaries. Components are medians of separately timed regions and do not sum exactly to the median total. The remaining owner path includes ordinary completed joins and boundary/FIFO work; it is not individually attributed.

| Cost (µs/tick, both teams) | Before | Complete frames |
| --- | ---: | ---: |
| Shared snapshot capture | 8.363 | 5.130 |
| Batch/window setup | 0.156 | 0.096 |
| Submission | 1.574 | 1.714 |
| Publication | 1.566 | 0.061 |
| Deadline wait | 0.098 | 0.226 |
| Worker calculation (sum) | 10.888 | 4.606 |
| Owner total, profiled | 13.120 | 7.753 |


Without profiling: static owner 9.170 → 10.327 µs/tick. Published frame installation removed the bulk merge; total CPU work is not eliminated. Snapshot peak leased memory is 867,530 bytes, matching the baseline. This metric excludes frame/ring storage.

Representative advancing worlds:256×256 map, two/four teams with128 starting units per team, stocked inn/swarm, fixed wheat/wood/stone, controlled or alternating Numbi/Castor AI, normal validated orders, AI order delay8. Workload code is `src/team/stats/TeamStatsPipelineTest.cpp`; saved rule delays0/1/2/8 and zero/two workers are all profiled. Values below use K=8 and two workers; all units µs/tick.

| AI teams | Publication before / after | Worker before / after | Owner, no profiling before / after |
| --- | ---: | ---: | ---: |
| 2 | 2.006 / 0.219 | 10.415 / 17.680 | 241.798 / 271.310 |
| 4 | 4.389 / 0.288 | 22.564 / 28.106 | 356.488 / 387.293 |


The final host was measurably busier than earlier clean trials: static shared capture (unchanged code) varied substantially between paired binaries. These final wall-clock medians are not a reliable causal speedup estimate; earlier clean-source tables under serial-batch-before-copy-cleanup/ show publication near0.05µs and the original near0.87µs. The removed unused copy is a code-level reduction, not a separately quantified instruction-count result. No overall advancing-match speedup is established. Capture timings and whole-match totals include shared scheduling/cache/host effects and must not be attributed solely to this change. For each matching configuration, the AI order counts agree across binaries. Header/save-version changes alter across-version checksums; cross-worker equality is checked within each binary, not by comparing incompatible checksum versions. Short K=1/2 still exposes host/scheduler deadline waits (all configurations and costs in `medians.json`). Publication clock overhead is included; normal execution reads no clocks.

The first per-team ordered-lane implementation achieved ~0.057µs publication but ~2.4µs submission versus~1.3µs before. Those early three quiet paired trials are retained under `per-team-lanes/`; later partial/interrupted trials are not evidence for an improvement. A single ordered batch restored the executor's one-group fast path and removed the per-team snapshot-lifetime atomic. The frame storage is reused via the existing spare batch; no separate allocation pool was added. No updated Callgrind attribution is claimed for this follow-up.

Baseline code is commit `4b15aed8d` (the former0.87µs publication implementation), built just before committing; its logged source digest is exactly reproduced from that tree and its worktree representation by `baseline-source-identity.txt`. New clean binaries embed the final commit above. Binary SHA256s are in `binary-sha256.json`; executables are not included. Earlier trials are retained separately, but current tables/traces use final clean source/provenance. The final worker path also avoids moving the unused inline measurement arrays into pending rows and skips non-aggregate stat/eligibility transfers; those fields are omitted from serialization when no aggregate is due. Pre-cleanup measurements are retained under serial-batch-before-copy-cleanup/. Reproduction uses the existing compiler/dependencies, release targets in `build-release.py`, linked argv in `release-link-args.json`, and `profile.py`; ordinary debug commands use `PYTHONPATH="$PWD" python3 tools/dev_build.py engine-tests unit-tests` and `python3 test/run_tests.py` as recorded in the drivers.
