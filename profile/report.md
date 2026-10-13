# CPU gradient regression diagnosis

Linux x86_64 / AMD Ryzen 7 6800H / GCC13.3 release -O3. Other user workloads remained active. All comparisons used CPU automatic, GPU off, shadow verification off, fixed saved inputs and alternating revision order. This establishes local headless results; graphical, ARM, browser and other-platform performance remains unmeasured.

## Causes established by isolated experiments

1. Clearing preparation computed goal and open-cell conditions before invoking the generated predicate, which computed them again. Classifying once with the unreachable seed as a terrain placeholder preserves goal/blocker precedence and lazy terrain lookup.
2. CPU terrain lookup indexed a 36-byte accelerator record, extracted its mask and converted it to a seed in every cell. A narrow 16-bit lookup removed that repeated work. Production now compiles three CPU columns once with each immutable rule table; the 36-byte accelerator representation is unchanged. CPU columns add six host bytes per rule and participate in accounting, cloning and rollback.

## Callgrind evidence

These are simulated instruction counts, not native timings. Release objects were relinked without stripping symbols; optimized code was unchanged. Collection toggled within Game::syncStep, 512-square initial fixture, 128 ticks, one compute participant. Each profile ended with checksum 82182947. Exact commands are in process logs and relink-commands.json.

| Variant | Total simulation instructions | Periodic seeding self instructions | Material-cache self instructions |
|---|---:|---:|---:|
| control | 2,494,553,785 | 292,451,712 | 96,215,143 |
| after | 2,732,385,706 | 485,290,304 | 96,215,143 |
| clear-once | 2,579,162,248 | 338,051,134 | 96,215,143 |
| compact-lookup | 2,514,277,279 | 279,678,144 | 96,215,143 |

The two experimental changes removed about 92% of the added instructions. The material cache executed identical instruction counts. The remaining solver instruction increase was visible but was not changed or fully attributed in this fix. Callgrind/cache simulation does not qualify real hardware timing.

## Native diagnostic experiments

Eight alternating run pairs per case, 128-square and 512-square initial fixtures, counts 1/8; 256 warmup and 2048 measured ticks. Earlier revision pairs used the pre-merge control (2d5cf67e8) and merged implementation (3f4a275d6). Compact lookup in this experiment rebuilt a small per-field table; production instead caches the columns. Positive changes below mean more CPU.

| Baseline | Experiment | CPU change | Paired 95% CI |
|---|---|---:|---|
| control | original | +2.32% | [+0.64, +4.00]% |
| control | clear-once | +0.44% | [-0.33, +1.22]% |
| control | compact-lookup | -1.56% | [-2.92, -0.20]% |
| original | clear-once | -1.75% | [-2.83, -0.68]% |
| original | compact-lookup | -3.73% | [-5.05, -2.41]% |

## Final production comparison

Base: current master 3e2ba6c07. Candidate: the same base with the CPU preparation fix. Protocol and exact binary/source hashes: final-protocol.json and final-source-hashes.json. Four frozen fixtures and counts 1/2/4/8, eight alternating pairs per cell: 256 timed runs. All-thread process CPU excludes setup and 256 warmup ticks, includes final drain, and measures 2048 ticks. Confidence intervals use pair-level averages with equal fixture/count weights, not ticks as independent observations.

Current master gates per-field CPU diagnostics behind compute telemetry; defaults were left off. Those phase counters are unavailable in the final default-path comparison. Attribution above uses the earlier profiles and diagnostic cohort; final timing uses the independent process CPU clock.

| Metric | Change | Paired 95% CI |
|---|---:|---|
| cpu_ms_per_tick | -0.99% | [-1.84, -0.14]% |
| run_wall_seconds | -0.57% | [-1.68, +0.53]% |
| whole_process_cpu_seconds | -0.77% | [-1.68, +0.13]% |
| setup_cpu_seconds | +0.56% | [-0.32, +1.45]% |

| Fixture | Master CPU ms/tick | Fixed CPU ms/tick | Change | Paired 95% CI |
|---|---:|---:|---:|---|
| 128-early | 0.2510 | 0.2478 | -1.07% | [-1.77, -0.37]% |
| 128-established | 0.3015 | 0.3035 | +0.75% | [-0.56, +2.06]% |
| 256-early | 0.5640 | 0.5504 | -2.38% | [-3.48, -1.29]% |
| 512-early | 2.2740 | 2.2427 | -1.26% | [-2.66, +0.14]% |

The full matrix showed one small positive case: 128-established/count1, +1.91% (CI +0.05 to +3.78). It was selected for an independent exploratory confirmation, not omitted from the matrix. Sixteen new alternating pairs gave -1.36% (CI -2.52 to -0.20); the increase did not repeat. Protocol, all pair changes and logs are retained separately.

## Correctness and continuity

- 144 unit cases passed; 78 isolated engine cases passed; another 34 isolated building/pipeline/save continuation cases passed. XML/logs retain case inventories and assertions.
- 11 shader/rule generator contracts passed. Native memory probe reports six host bytes per rule for CPU seed columns; worksheet updated.
- 32 untimed per-tick trajectory comparisons matched across both final binaries and counts 1/2/4/8; saves and replays retained. All 256 timed runs and 32 independent confirmation runs matched final checksums, with GPU execution disabled.
- Direct continuation from the established saved game matched per tick between master and fixed binaries. Canonical FourSquares1 match record verified at counts 1 and 8. See continuity.json and golden output.
- Generated predicates, GPU wire layout, solver algorithms, save format, simulation version and publication scheduling are unchanged. No GPU preparation or GPU qualification is claimed.

