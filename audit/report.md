# CPU overhead audit

Scope: Linux x86_64 / Ryzen 7 6800H / Ubuntu 24.04.5 / GCC 13.3 release -O3. No GPU preparation or new GPU feature was added. CPU automatic, GPU off, shadow verification off; defaults omit optional compute telemetry. Other user workloads were left running. Hardware/graphical/ARM/browser performance coverage is not established.

The frozen pre-project revision 8862b0930 already includes OpenCL and its ownership/coordinator infrastructure. This audit measures the incremental runtime-backend project cost; it is not a comparison against a GPU-free executable. Master was frozen at 3e2ba6c07. Candidate is f96aba387 plus candidate.patch, whose exact sources and binaries are hashed.

## Release comparisons

Four unchanged fixtures: 128² initial, 128² at tick 4096, 256² initial and 512² initial; identical orders/seeds and counts 1/2/4/8. Eight alternating triples per case: 384 timed runs. Each run excludes setup and 256 warmup ticks from its 2048-tick all-thread process CPU window, includes final drain, and records startup-inclusive user+system CPU separately. CPU affinity 0,2,4,6,8,10,12,14. Pair-level cohort averages give every fixture/count equal weight; Student-t paired 95% CI uses runs as units, never ticks. Protocol, raw commands/results, checksum traces, saves and replays are in closeout/.

| Comparison | CPU/tick change | Paired 95% CI | Startup-inclusive CPU | Setup alone |
|---|---:|---|---:|---:|
| before → master | +1.13% | [-0.25, +2.51]% | +1.00% | +0.95% |
| before → fixed | -1.25% | [-1.95, -0.55]% | -0.97% | +0.88% |
| master → fixed | -2.18% | [-3.19, -1.18]% | -1.85% | +0.17% |

Positive means more CPU. Full intervals for startup, setup, wall time and p95 ticks remain in closeout/three-summary.json. Candidate setup is +0.88% versus original (95% CI +0.29 to +1.47%); do not call startup free. Startup-inclusive CPU is -0.97% (CI -1.48 to -0.47%). Candidate wall time versus original is inconclusive (-0.63%, CI -1.54 to +0.28%). Per-case intervals are wider than the cohort interval; 128-early/count8 had the largest positive CPU point estimate (+2.83%, CI -4.53 to +10.20%). A separate sixteen-pair exploratory confirmation gave -0.62% versus original (CI -5.39 to +4.14%): the positive point estimate did not repeat, but this case remains inconclusive. It is retained in confirmation-final/ and is not pooled into the main result.

## Solver instruction attribution

Whole-game Callgrind collection inside Game::syncStep: 512² initial fixture, 128 ticks, one participant. All final checksums are 82182947. Original periodic solver self instructions: 1,785,571,564; project before CPU fixes: 1,815,066,127; dispatch fix: 1,785,571,214. Immediate solver self instructions match original exactly (76,220,894). Final candidate total simulation instructions are 2,496,743,593 versus original 2,494,555,959: approximately +0.088% remains outside the solved hot-kernel increase. This is an instruction comparison, not a CPU timing percentage.

The isolated kernel experiment excludes telemetry/job preparation: runtime scalar/SIMD layer dispatch raised instructions from 598,148,855 to 636,882,153. Resolving configuration access alone did not remove that increase (638,094,631). Fixed SSE selection gave 598,665,473; hoisting dispatch out of the layer loop gave 600,225,734. Every variant produced checksum 10969593557797945448. This identifies changed compiler output from inlined layer dispatch as the dominant cause, rather than telemetry or the handful of configuration reads themselves. Assembly, instruction-address profiles and raw receipts are retained. The full-field fix makes one scalar/SIMD choice before the sweep; resumed searches retain one resolved CPU scope per resume.

## GPU-off lifecycle and individual costs

Job/handle/session sizes remain 672/392/40 bytes in every build. Snapshot leases, pooled outputs, completion controls, allocation ownership and the inert device service already existed in the original OpenCL integration. GPU-off jobs bypass owned accelerator inputs, reservations and device coordination. All measured runs report zero coordinator threads and zero tracked device storage. Reported accelerator host bookkeeping is 87,840 bytes originally and 87,824 currently; status inspection creates this inherited registry. It is not new resident work.

At one participant, copied bytes and snapshot allocation counts match across builds. At higher counts they can vary with completion timing and pool reuse; do not treat one receipt as invariant. Final paired workload averages show no new systematic retention pattern: count4 allocations 446.1→445.9 and copied bytes 1116.4→1113.8 MB; count8 allocations 445.4→445.5 and copied bytes 1077.9→1073.5 MB. Snapshot accounting runs when headless results are reported, outside the measured tick CPU window. Startup-inclusive totals include that reporting work.

Component probes measure projection/release, job/output allocation, pool reuse, atomic control clearing, host reservation/release, configuration selection, disabled maintenance and memory accounting individually. They are hot-cache synthetic operation costs, not additive whole-game attribution; clocks/volatile observation and allocator reuse impose practical limits. Both single-threaded and multithreaded shared_ptr modes, eight alternating process pairs, exact build commands and paired intervals are in rule-final/ and lifecycle-probe.cpp. The plan-choice probe consumes its returned metadata to prevent optimizing away configuration work. Actual GPU-off reservations and ownership transfers remain absent; an isolated reserve/release timing does not imply those calls occur per CPU tick.

## Rule compilation and persistent cost

The journal copied maximum-sized prepared variants on every insertion and rebuilt unchanged profiles. It now records only changed prepared metadata, copies its active alternative, and rebuilds only when a distinct cost profile is added. Rejection rollback tests exercise both the 8→256 transition and the 256 alternative, checking prepared arrays and unpublished parallel tables.

In the final multithreaded probe, mixed-table compilation is 333.09→129.92 µs, -60.95% (paired CI -62.05 to -59.85%). Built-in construction still costs 9.23→11.76 µs, +27.54% (CI +23.20 to +31.87%): about 2.53 µs per table. Cloning is inconclusive. Native rule compilation overhead remains explicit despite improved mixed-rule insertion.

Incremental retained storage: 36 GPU wire bytes + 6 CPU seed bytes per rule capacity, 96 bytes of vector metadata per CellRuleTable on this ABI, plus 4 GPU preparation bytes per resource capacity and 24 vector metadata bytes per ResourceRegistry. At full capacities that is 2.625 MiB per rule table and 64 KiB per resource table, before allocator overhead. This storage is created even with GPU off. Rule-table ownership is shared; count capacities/owners, not cells. Job/handle/session sizes do not grow.

## Correctness, compatibility and boundaries

146 unit cases passed. The broad engine pass had 107 successful cases and one new fixture error; after correcting its API/range inputs, that case passed in isolation. The subsequent 42-case rule/terrain/preparation engine run passed after the rule optimization, including both rollback alternatives. Eleven tooling contracts passed. Initial failure logs are retained, not concealed.

All 48 final untimed per-tick trajectories and all 384 timed final checksums matched across the three binaries and counts. Direct save continuation from the original-build saved state matched across master/fixed and counts 1/2/4/8. Four canonical match verifications passed at counts 1/2/4/8, retaining replay/checksum/verdict files. No save-format, simulation-version, algorithm or publication scheduling changes.

No hardware GPU correctness, Apple/Windows/ARM/browser performance, graphical performance, or long-running CPU-off-after-GPU-use retirement measurements are claimed. High-count pool statistics vary with scheduling; kernel instruction equivalence is not zero total runtime cost. New CPU experiments should use this frozen candidate with telemetry and shadow verification disabled, keeping setup, final drain, confidence intervals and per-case uncertainty visible.

