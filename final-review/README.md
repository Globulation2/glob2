# PR1197 final review and short performance comparison

PR revision: **45710739e41679514bd3c4aa528c834bb518b1f4**. Master/base: **486a2230a4ddb257ba9dee34d3aace05f2260d6f** (fetched again before final checks; unchanged). The complete PR and cleanup received an independent subagent review: [findings and resolution](review.md). The production worktree is clean. The original private benchmark worktree and its edits were restored byte-for-byte after timing; neither release build used those edits.

## Native correctness

**308 targeted cases passed, zero failures/skips:** 146 hiring/gradient/service/save-safety, 41 replay/platform protocol/client, 18 turn/network, 103 unit customization/lifecycle/conversion/resource-fetch. `verification.json` contains clean 45710739e provenance from both actual test binaries; `native-commands.sh` records the commands. Build flags are fast-development `-O0 -g1` and are used only for correctness, never timing. The build command and compiler are in verification.json; full logs and JUnit reports are attached.

Two 2,048-tick seeded matches (Holiday Island 2/four teams and Oazis/eleven teams, seed 42, alternating Nicowar/Econo) and two 16-tick format 172 save continuations matched per-tick world/entity checksums across one/four compute participants. Every trace also matched the previously validated 403404a2b revision exactly, confirming the dead-state/audit cleanup preserved observed behavior. The old input saves, game/replay outputs, traces, test saves and exact commands are retained in native-artifacts.tar.gz and native-match-comparison.json. Native Game-only test saves load directly through harness loaders; the older CLI input saves include their GUI footer.

Requested OpenCL/Frozen8 games on Apple M3 matched both CPU trace files on each map. Both reported zero committed GPU fields (four/eight CPU fallbacks), so **GPU execution coverage remains unqualified**; see gpu-comparison.json. No cross-platform or actual rendered/manual-play verification was completed in this pass. Hosted checks were cheap-only, not full engine/platform verification. No full test-matrix claim is made.

## Release performance protocol

Apple M3 arm64, macOS 26.6.2, Apple clang 21.0.0. Stock master and PR sources were clean at build time. Both built in the same checkout/output path with identical dependencies and `PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j4` (`-O3`, ordinary compilation, no PCH/unity). See master-build.json/branch-build.json for revisions, binary SHA-256, compiler and identical linked-library paths/versions; compiler logs include the managed build-dd8d40b0b2e044b88374d32e dependency prefix. Frozen binaries use the stock built-in benchmark counters only: **no private source instrumentation**. Both retain the global four-building-admissions-per-tick policy; PR 1220 is separate.

**32 paired seeds 42–73 on each map: 128 measured games**, 4,096 ticks/game, four compute participants, GPU off, CPU backend, compute telemetry off, warmup 0 to include the early initialization window. Holiday Island 2 is 128²/four teams; Oazis is 256²/eleven teams. Players repeat Nicowar, Econo, Castor, Warrush up to the team's count. Each pair uses identical map bytes, AI assignments and seed. Runs are serial and master/PR order alternates within each map. All 128 games reached 4,096 ticks; no early end or measured game was dropped. Commands, environment, binary/map hashes, results, load averages, whole-process timing/RSS and contention status are in performance-runs.tar.gz. A clean four-game calibration pilot precedes the measured matrix and is excluded from averages.

The initial calibration pilot exposed macOS truncation of the `ps comm` column. That entire four-game pilot is retained under pilot-excluded-process-guard with its exclusion reason; **no 32-seed run had started**. The fixed guard reads executable names from full args, recursively excludes its own process subtree, and was verified against a real engine test and clang process (guard-verification.json). The measured 128-run matrix recorded zero contention/retries. All local builds/tests/GPU checks completed before the clean pilot and measured matrix. The guard does not detect every kind of OS/application activity.

Run `python3 artifacts/final-master-comparison/run.py --pilot`, then `python3 artifacts/final-master-comparison/run.py`, then `python3 artifacts/final-master-comparison/analyze.py` from a prepared benchmark checkout. The scripts use stock release binaries at artifacts/final-master-comparison/binaries/{master,branch} and the accompanying build JSON files. Extract the scripts/build JSON from this folder into that layout; build/freeze each listed revision before timing. Results use process CPU from stock high-resolution counters and whole-process user+system CPU from `/usr/bin/time -l`, which includes startup, final drain and exit but rounds to hundredths. The high-resolution simulation CPU is retained as supporting evidence. Per-game metrics use actual executed ticks. Means give each seed equal weight. 95% intervals use 10,000 paired seed bootstrap resamples and the ratio of arithmetic means (seed 1197); p99 values are **means of per-game p99**, not pooled tick percentiles.

## Results

| Map | Whole-process CPU µs/tick, master → PR | Change (paired 95% CI) | Simulation ticks/s, master → PR | Mean per-game p99 tick ms, master → PR |
|---|---:|---:|---:|---:|
| holiday | 164.0 → 164.1 | +0.05% (-2.27% to +1.55%) | 25968 → 26222 | 0.270 → 0.248 |
| oazis | 677.6 → 679.9 | +0.34% (-1.03% to +1.69%) | 5133 → 5007 | 1.768 → 1.838 |


Whole-process CPU/tick changes: +0.05% on Holiday (95% CI −2.27%..+1.55%) and +0.34% on Oazis (−1.03%..+1.69%). High-resolution simulation CPU/tick changes are effectively 0.00% and +0.46%, also with intervals crossing zero. This short run establishes no clear CPU, throughput or p99 change; the throughput/p99 intervals are wider (see summary.json). It is not proof of equal long-game performance or behavior on slower devices.

Mean peak RSS increased 70.96 → 71.63 MiB on Holiday and 149.09 → 151.47 MiB on Oazis (about +0.7 / +2.4 MiB). Recorded synchronous building-gradient builds averaged 58.13 → 0 and 113.44 → 0 per game. Background-job counts and other counters are in summary.json. Simulation trajectories can differ between revisions; these are whole-game results, not isolated-kernel attribution. The study covers only this short early-game window on one Mac; it does not establish frame pacing, rare-tail jitter, late-game behavior or cross-platform performance.

All evidence files have hashes in SHA256SUMS. Current code remains draft pending the explicitly omitted platform/device and hands-on coverage.
