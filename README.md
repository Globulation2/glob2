# CI efficiency prototype

Worktree: `codex/ci-build-investigation`, based on
`c2c086ad5b08bace7c27399d4eaa0ffd3f950d45`.

## Prototype changes

- Restore the existing Ubuntu 24.04 native compiler cache read-only in the browser
  job. Explicitly select g++-13 for the router and transport fixtures, matching the
  compiler that produced the cache.
- Add a separate 500 MB WebAssembly compiler cache through Emscripten's compiler
  wrapper. The key includes OS, architecture and pinned SDK. Only master saves;
  unused entries are pruned. Port libraries and final outputs are still rebuilt
  on fresh runners. No timestamp/time-macro sloppiness is enabled.
- Remove the duplicate YOG server builds from the main Linux matrix. Identical
  builds remain in the variants matrix for both supported Linux toolchains, and
  Windows still builds its server.
- Run the seven explicit renderer-contract tests once. They choose their renderer
  internally, so changing GLOB2_TEST_RENDERER did not make their second run useful.
- Move the browser-independent WASM export check into its own file, included once
  in the full Chromium suite instead of repeated in three other browser/renderer
  invocations.
- Transfer the 3.6 MB determinism checksum file as a Base64 string instead of
  millions of individually serialized JavaScript numbers; decoded bytes are unchanged.
- Give browser setup, unit tests, full behavior tests, cross-browser smoke tests,
  WebGL2 integration and visibility checks separate named steps for useful timings.

## Test matrix

| Coverage | Before | Prototype |
| --- | ---: | ---: |
| Full Chromium behavior and renderer contracts | 94 | 94 |
| Firefox/WebKit startup and viewport smoke | 20 | 18 |
| Chromium renderer-sensitive WebGL2 checks | 23 | 15 |
| Foreground/background visibility, both renderers | 2 | 2 |
| Total scheduled cases | 139 | 129 |

Both totals include a software-only viewport case that skips in the WebGL2 group.
No unique browser/renderer contract was removed. The full behavior suite already
ran only in Chromium before this change; Firefox/WebKit were already smoke lanes.
The before/after selection audit is in `artifacts/ci-experiments/coverage-audit.json`.

## Measured experiments

Four real production translation units were compiled sequentially with the pinned
Emscripten 4.0.15 and the same production flags, then recompiled after deleting
outputs while retaining only their compiler cache. All four output SHA-256 hashes
matched between cold and warm compiles.

| Source | Cold seconds | Warm seconds |
| --- | ---: | ---: |
| GraphicContextDraw.cpp | 11.59 | 0.89 |
| OrderMisc.cpp | 11.41 | 1.20 |
| AICortex.cpp | 15.93 | 1.09 |
| PlantationsGenerator.cpp | 26.17 | 1.22 |
| Total | 65.09 | 4.40 |

That is a 93.2% reduction for this compilation sample, not the whole CI workflow.
This is a shared development machine with other workloads; these are indicative
paired measurements, not hosted-runner benchmarks. Exact commands, object hashes,
cache statistics and the rerunnable script are retained under
`artifacts/ci-experiments/object-benchmark/` and `benchmark-objects.py`.

An independent Emscripten header-invalidation probe also passed: a warm compile
hit the cache and returned identical bytes; changing an included header caused a
miss and changed the object. Results are in `wasm-invalidation/results.json`.
The existing native cold/warm/header-invalidation integration test also passed.

In recorded hosted run 36465771077, the now-removed Linux server steps took 38
seconds combined, and the duplicated renderer cases took 44.3 seconds. Those
82.3 runner-seconds are observed redundant work, not an end-to-end wall-time saving
because jobs run concurrently. See `duplicate-work.json`.

### Complete WebAssembly rebuild

A successful cold build took 1,925.11 seconds (32m 05s). After deleting all game
objects, linked output, Emscripten port sources and port/system-library caches,
the same source build with the populated compiler cache took 836.38 seconds
(13m 56s): 56.6% less wall time on this shared machine. Source files and SCons
metadata remained in place, so this models fresh build outputs rather than a
completely new checkout. SDK installation is excluded from both timings. Other
workloads and overlapping browser checks limit timing comparability; these are
not predictions of hosted CI duration.

The compiler cache hit 991 of 992 cacheable calls (99.90%).
Statistics are recorded in `wasm-warm-fresh-stats.txt`. The final
optimizer/link still runs, and dependency-library assembly still incurs work even
when its compiler calls hit the cache. Port preparation before game compilation
alone took approximately 125 seconds on the warm run, suggesting a future
experiment with a separately keyed immutable SDK/port cache.

Exact build commands and timings are in `wasm-cold.json`, `wasm-warm-fresh.json`
and `benchmark-wasm.py`. The initial cold run used an empty compiler cache;
`benchmark-wasm.py warm-fresh` clears generated outputs and retains that cache.

### Checksum-transfer experiment

The original array transfer exceeded the normal 90-second timeout locally.
A diagnostic copy with console logging and a local 300-second timeout passed in
143.7 seconds. The Base64 version passed in 16.8 seconds with the original timeout
and the same frozen exact-source binary. This is a directional comparison under
shared-machine load, not a controlled hosted benchmark. All 3,601,608 decoded
bytes match the cold/reference checksum file (SHA-256
`110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`).
Production timeouts, trace settings, fixture, tick count and assertions are unchanged.
See `trace-transfer-comparison.json` and the full Playwright JSON reports.

## Validation and limits

- Actionlint 1.7.7 passed (shellcheck disabled because it is not installed).
- All 10 build-system unit tests passed.
- All 15 browser shell unit tests passed.
- Native compiler-cache cold/warm/header invalidation passed.
- Emscripten compiler-cache cold/warm/header invalidation passed.
- Playwright discovery confirms the 129-case matrix and retention of all seven
  explicit renderer contracts.
- All ten focused browser checks passed against the exact-source cold build: the
  WASM artifact check, determinism, all seven renderer contracts and startup.
- Downloaded Linux, Windows and WASM checksum traces from the exact base commit
  match both local cold and cached builds byte-for-byte (1,500 ticks).
- Three final browser checks passed against the cached output: startup, the WASM
  artifact contract, and determinism.
- The first local original-array determinism run timed out; the new encoding
  resolves that timeout without changing production limits.
- A separate trace-screenshot performance experiment used an existing developer
  build of unverified revision and encountered editor UI failures. It was stopped
  and is excluded from performance claims. Diagnostic trace behavior is unchanged.
- No hosted run of the modified workflow has been dispatched. There is no claimed
  whole-pipeline speedup. The first master run needs to seed the new WASM cache.

Evidence is archived under `artifacts/ci-experiments/`. No CI artifacts were deleted,
no test assertions weakened, no browser engine removed, and no simulation code or
compatibility version changed. This evidence branch is review material and is not merged into master.
