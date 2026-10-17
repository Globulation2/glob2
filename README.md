# PR 1196: shared air movement verification

PR: https://github.com/Globulation2/glob2/pull/1196

- Final head: `522a89ff9b36a1c438114f4581a210f00bd5178a`.
- Final production code: `b2264eb567c7b6a8ff33282f9ffaf59fd202ef4e`; the head adds only two replay-acceptance test updates.
- Base integrated and fetched again before final validation: `486a2230a4ddb257ba9dee34d3aace05f2260d6f`.
- Environment: Ubuntu Linux x86_64, GCC 13.3.0, Valgrind 3.22.0. Exact compiler, dependency prefixes, flags and binary hashes are in [validation-provenance.json](validation-provenance.json).
- Development: `tools/dev_build.py`, `dev_fast=1`, `-O0 -g1`; optimized game: `dev_fast=0 release=1 retain_symbols=1 CXXFLAGS=-g1`, `-O3`. No PCH/unity. No runtime dependencies added.

## Results and scope

The final production revision passes 202 focused engine cases and 43 focused unit cases. These cover shared field preparation, owner/worker equivalence, independent heap oracles, lazy maximum-size maps, custom air speed/damage, air blockers, forbidden paint, cache activation and namespaces, service/hiring policies, physical movement, water-only preservation, and save/load continuation. The final test-only revision also passes the integrated JavaScript and team-stat replay acceptance checks, including the new floor and draft-version rejection.

The supplemental broad implementation-era run executed 1,581 engine cases: 1,566 passed, eight failed assertions and seven timed out; its JUnit also records 206 skipped cases. Every initial failure/timeout has a passing follow-up, recorded individually in [reconciliation.json](reconciliation.json). Relevant assertions were updated for the intended shared-routing policy and new format; a resource golden changed only by the format metadata XOR, with all seeds/ticks/random hashes unchanged. Slow continuation cases pass with a 600-second budget. The LAN latency threshold fails under development optimization and passes with optimized production code. Thresholds were not weakened.

That broad run predates the final lazy-cache optimization and test updates. Its embedded provenance reports the development snapshot under `af35933bd5f15ae8aeda8c90603353631c868757`, dirty source tree hash `d23acf229234be81142321db91ca96485af358f5788052e329993700ebcebc9c`. It is supplemental evidence, not an all-suite certification of the final head. The final 202/43-case refresh, final gate tests and deterministic release runs cover the subsequent changes. All original failure reports remain available.

The supplemental broad unit run records 1,097 passes, 89 skips and one development-build performance-threshold failure. Its exact fertility case passes in an optimized standalone build: 323,935 assertions, 23,314 microseconds CPU against the existing 100,000-microsecond threshold. The compile command and both logs are retained.

The supplemental optimized engine binaries link the normal release production objects with existing development test-support objects and the development QuickJS archive. The LAN test case itself is compiled with `-O3`; the generator/slow-case test objects remain `-O0`. Exact link commands describe this mixed configuration. They establish the selected assertions, not a complete optimized test build or native performance estimate.

## Determinism, saves and version gates

Five setups (Midgame 24k, Islands, Swamp, Lava Shield and the authored 64-flyer stress map) each run 128 ticks with 1/2/4/8 compute participants. Every per-tick aggregate checksum and every team/entity checksum byte matches across participants. Inputs are first promoted to a common format-171 checkpoint.

Lava and stress also save at each of all eight pipeline offsets and resume with different participant counts. All subsequent per-tick state matches uninterrupted execution. A clean optimized build without the temporary profiling hooks reproduces the complete checksum files on all five setups. The stress fixture exports both a current save and a released-format-167 save; candidate loading and baseline CLI execution are verified. Released save fixtures are also exercised by the engine tests.

Save floor 58 remains; format 171 adds air routing slots with version-gated historical layout remapping. Draft formats 168–170 are rejected. Replay minimum is 171; network protocol remains 71; SIM revision is 70. The golden match record verification and sim-revision policy check pass. Generated preparation shaders match their source rules.

## Callgrind

[performance-comparison.json](performance-comparison.json) records executed instructions, not CPU or wall-clock time. Both versions use four compute participants, 128 warmup ticks, the same input fixtures and the same measured tick ranges. Native and profiled final checksums match within each version. Expected behavior changes mean baseline and candidate end states differ; deltas include changed routing trajectories.

| Setup | Measured ticks | Owner Ir change | All-thread Ir change |
| --- | ---: | ---: | ---: |
| Midgame 24k | 256 | +2.14% | +3.99% |
| Islands | 256 | +9.86% | +6.73% |
| Swamp | 256 | +1.96% | +2.95% |
| Lava Shield | 256 | -14.09% | -0.60% |
| Authored 64-flyer stress | 32 | -99.46% | -93.21% |

The stress case exposes the old owner-side destination scans; it is synthetic and does not represent typical-map speedup. Ordinary cases regress in instruction count. There is no universal performance-win claim. Zero old private-field symbols in candidate attribution does not mean shared AIR routing has zero cost. Shared functions also process ground fields and cannot be split by movement class from symbol names alone. Per-thread self-cost totals reconcile with Callgrind footers and global totals.

## Evidence and reproduction

- [commands.txt](commands.txt): build/test invocations and selection rationale.
- [checks.tar.gz](checks.tar.gz): original logs, inventories, JUnit files, failure follow-ups, binary/source provenance, optimized-check commands, shader/sim checks and the resource-golden metadata proof.
- [determinism.tar.gz](determinism.tar.gz): exact commands, scripts, current saves, replay checksum sidecars and all continuation results, including the uninstrumented release check.
- [profiling.tar.gz](profiling.tar.gz): master/candidate raw per-thread Callgrind files, analysis/parser scripts, measurement hooks, input hashes and native/profiled checksum comparisons.
- [fixtures.tar.gz](fixtures.tar.gz): all original performance inputs plus the current and format-167 authored stress saves.
- [routing-artifacts.tar.gz](routing-artifacts.tar.gz): focused routing/save/hiring test artifacts and authored stress exports.
- [continuation-artifacts.tar.gz](continuation-artifacts.tar.gz): supplemental continuation, generator and optimized timing-check artifacts.
- [SHA256SUMS](SHA256SUMS): archive integrity hashes.

Extract the archives and adjust the machine-specific source, binary, input and output roots in the recorded commands/scripts. Keep seeds, inputs, orders, warmup/stop ticks, flags and participant counts unchanged. No build binaries are published.

Windows, macOS, Android, iOS, browsers, actual GPU execution, NEON execution, display tests and maintainer playtesting are not verified. Linux checks do not establish those platforms' simulation equivalence. Hosted CI was not requested. The PR remains draft, with maintainer acceptance pending.
