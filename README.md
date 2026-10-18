# PR 1196 integrated verification

Tested final source `a7c944a2f303c7dc2ecba1845ce9cd89072b1c55`, integrated master `13d99a181e3225738411f394e7c29edbabe11b47`.
Ubuntu 24.04.5 x86_64, GCC13.3, pinned dependencies recorded in build logs and native provenance. Release -O3, dev_fast=0, retain_symbols=1 CXXFLAGS=-g1, no PCH/unity; same dependencies and flags for both timing binaries.

3042 standard native cases passed, zero failures/errors/skips; 340 Python tests passed. Full native inventory ran at143937cc5; final0d68d4916 changes only three checksum reference fixtures, followed by a7c944a2f updating Python format-boundary expectations. The two affected JavaScript cases ran on clean0d68d4916 source and two display cases interrupted by disk exhaustion passed reruns with available capacity. All original failures, replacement mapping, inventories, commands and logs are retained. The 38 opt-in native benchmarks are excluded. Generated UI screenshots were discarded to recover disk capacity; UI pass claims refer to assertions and completion logs.

The JavaScript references changed only aggregate map/routing metadata; all team/entity records match over512ticks. The ablation runner reproduces all96 master golden checksums; all unit vectors and RNG match the PR and only the map vector changes. The Maxima old reference fails on current master too; master and PR team/entity payloads match for512ticks, and PR serial/parallel plus midpoint reload all match complete records. References were updated only after these comparisons; assertions remain intact.

Real saved formats168,169,171,172 load into173 and continue with matching aggregate/entity records except explicit header-version checksum delta. Ordinary/constrained Air cases compare128 full per-tick records across1/2/4/8participants plus eight reload phases. Native browser-reference corpus, native replay/save continuations, golden match, sim revision76, docs and gradient contract checks pass. Saves retain floor58; replay floor173, network72. Released169 delayed-ground and171/172 Air layouts are decoded independently.

Fresh optimized CPU medians, four physical cores/four participants,128warmup+256measured ticks, ten trials per scenario/version (120 measured runs); raw ranges, process states, input hashes, commands and samples retained. Timing waits for120seconds without competing compilers/game/profile processes and checks before/after every sample.

| Scenario | Master CPU ms | PR CPU ms | Change |
| --- | ---: | ---: | ---: |
| midgame24k | 137.96 | 138.09 | +0.09% |
| islands | 272.39 | 271.81 | -0.21% |
| swamp | 279.40 | 275.39 | -1.44% |
| lava-shield | 189.80 | 188.77 | -0.54% |
| constrained64 | 1172.04 | 17.70 | -98.49% |
| ordinary32 | 7.71 | 7.83 | +1.51% |

Small timing differences must be assessed against sample variability. This refresh supersedes prior source933/c7 timings. Linux-only; actual Windows/macOS/mobile/browser execution, cross-platform checksum equivalence, sanitizers, hosted CI, release/store packaging and human play review are not newly qualified. Archive contains source diff, exact commands/results, actual saves/replays/checksums, diagnostic comparison proof and all timing samples. Binary files and compiler objects are excluded. Earlier failure logs are diagnostic history, not final failures.
