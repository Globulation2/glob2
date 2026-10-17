# PR 1196 full Linux client qualification

Tested revision: `656921aa2cb275a294508ef07e9614ead56be0de`.
Fetched master/base: `486a2230a4ddb257ba9dee34d3aace05f2260d6f`.
Linux x86_64, GCC 13.3.0, default free client profile. No PCH or unity build.

The optimized engine and unit binaries pass every one of the **2,985 eligible native cases**: 1,798 engine and 1,187 unit, with no failures, errors or skips. Their inventories match the full native inventory exactly. The 38 opt-in benchmark cases are excluded by the standard runner, as documented in `test/README.md`. The development unit binary also passes its complete 1,187-case inventory.

| Additional verification | Result |
| --- | --- |
| Root Python tooling, including real CLI and decoded recording checks | 339 passed |
| Build-system tests, including pinned asset encoder, SDL source patch and real NSIS compiler | 568 passed, no skips |
| Deployment helper tests | 48 passed |
| Terrain ecology / gradient benchmark tooling | 8 / 11 passed |
| Maxima strategy configuration and policy tests | 75 passed |
| Browser JavaScript unit tests (Node, not browser execution) | 130 passed |
| Generator golden rows | 564 compared, no failures |
| Generator telemetry neutrality | 204 generated cases, no semantic failures |
| Full colony-count generator sweep | All four shards passed |
| Shared JavaScript corpus | Both native producers passed, clean matching source provenance |
| Frozen JavaScript profiles | Both profiles passed complete checksums, 1/4-participant equivalence and six reload continuations |
| Native browser-comparison capture | Current-source stock, resource and unit traces captured successfully |
| CLI/map/tournament/LAN/menu/save/thread/pipeline/static integration commands | All 21 effective results passed |

There are **1,179 contributor-tool test cases** in the first six table rows. Corpus and integration reruns are not added to the native case count. `summary.json`, the raw JUnit files and `integrations-final.json` record the result inventory. The original integration ledger retains its first Maxima-baseline failure; the final ledger links the successful current-baseline reruns instead of erasing that failure.

Two test corrections were committed. The retained format-115 Maxima checkpoint first differs from master at tick 30262: explorer 70 at (59,77) selects the inn at (34,105) instead of (38,107). The PR already ranks flying service destinations with shared air-route distance rather than the old geometric ranking. This is the documented service-selection change, including on maps that keep direct steering. Master matches the original 512-tick baseline. The PR passes independent midpoint reload comparison before any baseline regeneration; only the 250 hashes for ticks 30262–30511 were refreshed. Final serial and four-participant checks each match all 512 current hashes and all 256 resumed records. The original expected JSON, both complete producer traces, checkpoints, commands and first differing entity fields are retained.

The fertility cache's 100 ms production CPU budget remains unchanged and is enforced in optimized builds. Fast development and coverage builds validate the cache without applying a release performance threshold. The full optimized unit run passes with that assertion present in its object file. A separate optimized execution also passed at 35.8 ms process CPU time; its exact compile/link commands and raw JUnit are retained. That diagnostic is not a representative application benchmark.

This task changes only test code, a test README and the retained expected trajectory. Application source is unchanged since `2c057dfd91816c08dc2234f7cb889f108381a4f9`; SIM revision remains 72 and the multiplayer golden record is unchanged. A normal build against the final revision confirms the optimized game is up to date and byte-identical to the game used for the integration runs (`game-final-build-identity.json`). Both final native test binaries embed clean provenance for the final revision.

Earlier exploratory results are retained: missing Vulkan shader compilation, an unconfigured SDL font-library path, asset tests run outside the pinned Pillow interpreter, the development-build CPU-budget failure, and development engine timeouts/latency-sensitive failures. The complete optimized runs are the final qualification. The remaining exploratory development display run was stopped only after the complete optimized engine and unit runs passed; its partial log and status are retained.

The raw bundle contains full logs, JUnit, selected saved worlds, complete Maxima checksum streams, scripting corpora, native browser-comparison traces, CLI/tournament evidence, build commands, binary hashes and dependency/tool receipts. Large screenshots and unrelated native fixture outputs are not included. PR-specific flight recordings and deterministic courier/hazard fixtures from the unchanged application source remain available in the previous qualification: https://github.com/Globulation2/glob2/tree/38c3bc0f469947b1d92156f79e73306ffda88d89 .

The archive is `full-suite-evidence.tar.gz`. If numbered `.partNNN` files are present, concatenate them in filename order before extraction. `archive-sha256.txt` identifies the complete archive. Main result files and the two test-fix patch are also available directly beside this README.

Builds used `python3 tools/dev_build.py dev_fast=0 release=1 retain_symbols=1 CXXFLAGS=-g1 --build=/dev/shm/glob2-direct-air-release` with the existing SDL prefix, no runtime dependency upgrades, and pinned build-only glslang 15.1.0 (`1062752a891c95b2bfeed9e356562d88f9df84ac`) for Vulkan shaders. The development variant used `dev_fast=1`, `-O0 -g1`. Exact build commands and compiler/link flags are in the logs and provenance. The native final commands were:

```sh
env -u DISPLAY python3 test/run_tests.py --build-dir /dev/shm/glob2-direct-air-release --binary engine --fullscreen --display-jobs 1 -j4 --timeout 1200 --write-inventory artifacts/pr1196-full-suite/engine-final-inventory.json --write-timings artifacts/pr1196-full-suite/engine-final-timings.json --junit artifacts/pr1196-full-suite/engine-final.xml --artifacts artifacts/pr1196-full-suite/native-final-engine
env -u DISPLAY python3 test/run_tests.py --build-dir /dev/shm/glob2-direct-air-release --binary unit --fullscreen --display-jobs 1 -j4 --timeout 1200 --write-inventory artifacts/pr1196-full-suite/unit-release-inventory.json --junit artifacts/pr1196-full-suite/unit-release.xml --artifacts artifacts/pr1196-full-suite/native-release-unit
```

The runner owned Xvfb/Openbox sessions. Software-backed OpenGL/Vulkan paths ran; physical GPU behavior was not qualified. Tests, builds and sweeps overlapped on a shared host, so their wall times are correctness evidence rather than application performance measurements.

Not covered: other operating systems/architectures, physical GPU/NEON execution, full browser integration, sanitizer matrices, server/relay/Postgres service matrices, store/release packaging qualification, or human maintainer playtesting. Local Linux results do not establish cross-platform checksum equality. The PR remains draft and has not been merged.
