# PR 1196 final geometric-flight verification

Tested clean source `933eea3e4021c9b0fd04b563fe5f073a10739966` against integrated master `c7cdf04d4fbcdd4d09f81e2953d6a5e77e5ee4b2`.

Ordinary flyers use direct steering, historical geometric service ranking, inn rebooking, building admission and explorer/warrior recruitment. Forbidden paint stays ground-only. Air-constrained maps use the shared field pipeline. Ground material/clearing/guard loops and the complete material-cache kernel have separate compiled specializations; ground-only cache budgets omit unused air templates. Resource and clearing goals retain shared fields.

Native suite: 3018 passed, zero failures/errors/skips; engine and unit partitions have exact commands, inventories, timings and XML beside this report. `--fullscreen` enabled. Contributor Python suite results are in `python-final75.log`.

SIM_REVISION 75; save format 172 (floor 58), replay floor 172, network protocol 72. Native replay/network acceptance, the golden match, native browser-reference corpora, complete per-tick records across 1/2/4/8 participants, eight save phases, real 168/171 save continuations and Maxima serial/parallel continuations are in the archive.

Build: `python3 tools/dev_build.py dev_fast=0 release=1 retain_symbols=1 CXXFLAGS=-g1 --build=/dev/shm/glob2-direct-air-release -j8 /dev/shm/glob2-direct-air-release/src/glob2 engine-tests unit-tests`, with pinned SDL prefix and glslang paths retained in the build logs and scripts. Identity, compiler, binary hashes, exact commands, inputs and seeds are machine-readable.

| Scenario | Master CPU ms | PR CPU ms | Delta |
| --- | ---: | ---: | ---: |
| midgame24k | 153.94 | 155.64 | +1.10% |
| islands | 289.22 | 284.72 | -1.55% |
| swamp | 288.81 | 286.61 | -0.76% |
| lava-shield | 191.70 | 191.50 | -0.11% |
| constrained64 | 1175.38 | 64.35 | -94.53% |
| ordinary32 | 7.75 | 7.68 | -0.89% |

Ten measured trials per case/revision after one priming run, four fixed physical CPU cores, 128 warmup + 256 measured ticks. Every endpoint was stable within its revision. Samples, ranges, quartiles, per-run process/load snapshots and input hashes are retained. Timing started after all task builds and tests completed. Single-run instruction profiles reconcile every thread against Callgrind totals and reproduce native endpoint checksums. They are diagnostic instruction counts, not native timing.

Known limits:
- Linux x86_64 GCC 13.3 only. Other-platform per-tick execution was not verified.
- Actual browser, Windows, macOS, mobile, sanitizer, hosted CI and release/store packaging matrices were not run.
- Native browser-reference corpus passed; actual browsers remain unqualified.
- Instruction profiles ran while an unrelated build was active; their timing fields are not used as native measurements. Native timing waits for an idle host.
- 38 opt-in microbenchmarks are outside the standard suite. Separate six-scenario release timing used ten trials per revision.
- Automated gameplay scenarios do not establish human play feel.
- Timing attempts were discarded when unrelated builds started. The successful retry requires 120 seconds without competing compilers/game/profile processes, then checks before and after every sample.
- Old partial/stopped suites and earlier policy-test failure are retained and superseded by the final suite.

Earlier source revisions are diagnostics only: partial suites were stopped to make the caller fixes. The sole completed intermediate engine-headless failure was an old explorer test expecting fields on an ordinary map. The final test explicitly uses constrained terrain and preserves all previous accessibility/hunger assertions; separate ordinary-map cases verify no air fields are allocated.

Download verification.tar.gz, check archive-sha256.txt, and extract with `tar -xzf verification.tar.gz`. If the archive is split, concatenate the numbered .part files in order before checking the digest.
Evidence contains logs, XML, provenance, raw profiles and applicable saves/traces. Executables, objects, dependency caches, temporary profiles and large UI captures are excluded. Maintainer acceptance and human play review remain with the user.
