# Resource growth: reserved-core throughput and CPU profiles

## Conclusion

The requested throughput improvement is not demonstrated. On reserved cores, shared growth reliably regresses the dense and active-AI cases, and the disabled-growth control also regresses. Multi-material throughput is inconclusive. Shared execution helps against the delayed owner path, but that does not establish an improvement over legacy growth.

## Isolation and reproducibility

- Existing repository wrappers reserved physical cores 0–3 plus SMT siblings 16–19 in an exclusive, balanced cgroup-v2 partition. Engine affinity was 0–3; unrelated work retained the remaining CPUs. The governor wrapper selected performance on engine CPUs. All three campaigns restored cgroups, affinity and original schedutil governors successfully; audits are included.
- AMD Ryzen Threadripper 2950X, Linux x86-64, GCC 15.2 release. CPU frequency boundary samples were approximately 4 GHz. Reserved idle-SMT median activity was zero. This isolates CPU scheduling, not shared memory bandwidth, package power or all kernel activity.
- Native timing: four fixed starting saves, 1,024 ticks, one warm-up plus ten paired rotated/reversed measured repeats, legacy/owner/shared = 132 runs. Four executor slots, gradient workers 2, AI execution enabled, growth delay 8. Fixed scenarios and exact commands are in native metadata and measurement rows. Each binary ran from its corresponding source/data root.
- Baseline binary/revision d42d3e512; candidate production e93956015 with later test/docs-only changes through 14a5d8fe9. Binary hashes are retained. No production changes were made for this investigation.
- Hardware profiling used privileged perf attached only to launched benchmark descendants; the game ran as UID 1000. No host perf policy was changed. Initial unprivileged access was denied; the privileged probe succeeded.
- Primary profiles: three repeats per scenario and legacy/shared mode, 24 windows. Temporary Headless copies enabled and disabled perf using acknowledged FIFOs around the engine run. Both sampling and hardware counters excluded startup/load and post-run checksums. Release optimization was unchanged. The gate source/build commands are supplied; these hooks were not added to the branch. Profile durations are not throughput measurements.
- Sampling: 499 Hz cycles with DWARF call chains; cycles, instructions, task-clock, cache references/misses, context switches, migrations and page faults were collected without reported counter multiplexing. The 24 reports show no lost samples. PMU cache events are platform-specific; these are not direct memory-bandwidth measurements.
- Exploratory full-process 4,096-tick profiles preceded the gated profiles and are retained separately. Their symbols-only executables have identical .text hashes to the uninstrumented measured binaries. They are not substituted for matching-window profiles.
- All 88 native candidate runs matched final heavy checksums and growth counters across owner/shared. All 24 gated profiles matched their corresponding uninstrumented final checksums. These are final-state checks, not a new cross-platform or per-tick verification campaign.

## Uninstrumented throughput

Ticks/s columns are independent medians. Paired deltas are medians of same-repeat ratios, so they need not equal ratios of those columns. Intervals are deterministic bootstrap 95% intervals over ten pairs. CPU is all-thread simulation CPU, excluding process setup and final save.

| Scenario | Legacy ticks/s | Owner ticks/s | Shared ticks/s | Shared throughput vs legacy (95% CI) | Shared CPU/tick vs legacy (95% CI) |
|---|---:|---:|---:|---|---|
| dense | 2040.0 | 1658.1 | 1852.1 | -9.3% (-12.2 to -5.3%) | +10.6% (+5.7 to +14.6%) |
| multi | 161.2 | 147.1 | 166.5 | +1.0% (-2.7 to +9.5%) | +4.9% (+2.7 to +6.9%) |
| ai512 | 709.0 | 661.4 | 685.2 | -3.9% (-5.1 to -1.7%) | +4.3% (+2.3 to +7.5%) |
| disabled512 | 959.9 | 922.2 | 931.0 | -4.2% (-6.0 to -1.6%) | +5.4% (+2.4 to +8.3%) |

Dense uses a no-op JavaScript controller, whose observation path still scans visible tiles. Multi-material uses the same controller setup; ai512 uses active Nicowar/Warrush. Disabled512 is the pre-existing generic disabled-growth control. Old/new growth trajectories intentionally differ, so these measure the actual implemented engine behavior, not identical old/new ecological work.

## Matching-window hardware counters

Medians of three profiles, not acceptance timing. Ratios compare these medians and have no claimed confidence interval.

| Scenario | CPU ms old/new | Cycles B old/new | Instructions B old/new | Cache misses M old/new | Context switches old/new |
|---|---|---|---|---|---|
| dense | 1860 / 2082 | 7.40 / 8.29 | 11.52 / 11.41 | 45.1 / 53.1 | 289 / 222 |
| multi | 13356 / 15098 | 53.14 / 60.08 | 53.63 / 54.97 | 240.1 / 277.6 | 2607 / 2729 |
| ai512 | 4497 / 4735 | 17.90 / 18.85 | 26.06 / 26.04 | 152.9 / 166.0 | 844 / 727 |
| disabled512 | 3324 / 3444 | 13.23 / 13.71 | 22.04 / 21.96 | 97.2 / 107.2 | 674 / 625 |

Dense executes about 1% fewer instructions but takes 12% more cycles and records 18% more cache misses. Disabled growth likewise executes slightly fewer instructions while cycles rise about 4% and cache misses about 10%. This supports reduced memory/cache efficiency, not merely more instructions from scheduling. It does not independently prove that the incarnation field causes the entire regression.

## CPU sample attribution

Median self-cycle percentages across three profiles. Groups do not overlap, but medians and report filtering need not sum to 100%. libc copying includes every caller, not just snapshot capture. Inlined copy instructions remain attributed to their containing snapshot functions. These are CPU samples, not blocked-time samples.

| Scenario / mode | Snapshot machinery | libc copying | Gradients | JS observation | Growth kernel/ecology/pipeline | Growth stats | Executor/locking/scheduling |
|---|---:|---:|---:|---:|---:|---:|---:|
| dense / legacy | 9.72% | 2.09% | 46.60% | 28.58% | 3.02% | 1.33% | 0.68% |
| dense / shared | 12.18% | 3.21% | 43.02% | 25.58% | 4.35% | 1.14% | 0.20% |
| multi / legacy | 15.26% | 8.60% | 42.09% | 14.36% | 3.42% | 0.93% | 0.27% |
| multi / shared | 16.27% | 8.08% | 38.89% | 14.04% | 6.10% | 0.79% | 0.00% |
| ai512 / legacy | 6.79% | 3.77% | 64.76% | 0.00% | 5.41% | 0.00% | 0.09% |
| ai512 / shared | 8.15% | 3.42% | 64.51% | 0.00% | 5.32% | 0.00% | 0.00% |
| disabled512 / legacy | 1.03% | 4.90% | 78.67% | 0.00% | 0.00% | 0.00% | 0.18% |
| disabled512 / shared | 1.41% | 4.80% | 78.74% | 0.00% | 0.00% | 0.00% | 0.06% |

## Interpretation and limits

1. Copying is efficient but not free, and this is not one large contiguous copy. Resource refresh operates on changed 16×16 chunks and issues row copies. The dense exploratory assembly annotation places 79% of samples within the hot refresh helper on its inline `rep movsq`; that helper itself accounts for 8.66% of all sampled cycles in that profile. The primary gated profiles likewise locate substantial cycles in refresh and libc copy routines. The earlier claim based only on elapsed capture time is now backed by CPU samples, while attribution is narrower than “all overhead is snapshots.”
2. The candidate resource cell is 16 bytes versus the old 12 bytes. Existing gradient and observation consumers read it even when growth is disabled. The disabled control costs more cycles with near-identical instruction counts, and its profile is approximately 79% gradient work in both binaries. This is consistent with a wider representation making existing scans less cache-efficient. A layout-only ablation is still needed to measure the incarnation field’s exact causal contribution.
3. In dense fields, snapshot machinery rises from about 9.7% to 12.2% of cycles, and libc copying from 2.1% to 3.2%. Those percentages also sit on a larger total-cycle budget. Extra compute/publication work contributes too; growth-specific functions and statistics are a small part of total engine work compared with gradients and JS observations.
4. Executor/locking/scheduling functions do not dominate CPU samples (under 1% in these groupings). Shared growth deadline/final-drain joins total median 0.14 ms dense, 0.20 ms multi, 0.18 ms AI, and zero disabled across 1,024 native ticks. The dense run uses approximately 3.9 CPU cores of the four reserved, supporting a CPU-throughput limit rather than idle waiting for growth. Sampling cannot rule out every off-CPU delay, and AI/gradient waits are not included in the growth-only join metric.
5. Multi-material workloads spend much more absolute time in copying and growth, but greater overlap can hide some CPU cost from the critical path. This explains why more CPU does not necessarily mean proportionally worse ticks/s. The native multi-material result remains inconclusive, despite shared execution outperforming delayed owner execution.
6. The best next controlled experiment is a layout-only version that keeps incarnation counters out of frequently scanned resource cells while preserving all pipeline semantics, followed by the same reserved-core throughput test. Dirty-copy granularity and snapshot-buffer reuse are additional candidates. No speedup is claimed for unimplemented changes.
7. This is one host and four workloads. Core reservation improves the evidence substantially but does not isolate memory bandwidth from other host work. Profiles are diagnostic; the separate uninstrumented paired campaign establishes the reported throughput results. Current master integration/golden refresh remains outstanding on the draft PR as previously documented.

## Evidence files

`native/measurements.jsonl.gz` includes every timing, command, CPU/RSS, per-CPU busy and governor/frequency snapshot. `native/metadata.json` retains input hashes and scenarios. `profile-summary.json` retains raw parsed counters and per-symbol percentages. `gated/` includes raw perf profiles (compressed), flat/cumulative reports, counters and matching engine results. The control FIFO build/runner scripts, profiler source copies and complete cgroup/governor audits are included. `checksum-audit.json` records the native/profile consistency checks.
