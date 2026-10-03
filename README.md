# Browser performance evidence for PR #641

[Production PR](https://github.com/Globulation2/glob2/pull/641)

[Download the evidence archive](browser-performance-evidence.tar.gz). Extract it locally; JSON files, Chromium CPU profiles, checksum traces, screenshots and logs are ordinary files. No compiled runtimes or SDKs are included.

## Provenance and limits

`pr-validation.json` records the final PR revision and commands. `pr-runtime-manifest.json` identifies the integrated browser runtime at d016b4ea5; that is an earlier integration. `pr-final-runtime-manifest.json` identifies the final dbfa56cb9 runtime against master 4da094543, including telemetry data-header cleanup and recording draw hooks. Timed profiles were not recaptured after this final integration. `pr-serial/` and `pr-threaded/` contain refreshed 15-second profiles of the earlier d016b4ea5 runtime. Their summaries record fixture SHA-256, browser, viewport, GPU, CPU and system load. The fixture is the repository's `games/gd-bigarena-long.game.gz`.

The retained baseline and `final-*` measurements predate integration with current master. They document the original investigation, not a controlled comparison against today's master. Concurrent unrelated CPU/GPU workloads affect both old and refreshed measurements. Host callbacks and GL submissions are not physical scanout. Extra rendering increases active CPU: this PR does not claim an overall CPU reduction.

## Validation

`pr-merge-ready-chromium-results/` and `pr-merge-ready-software-results/` retain final simulation checksums and rendering screenshots. `pr-checksums.json` supplies their SHA-256 hashes. The real-UI pacing checks compare serial/threaded matches against the same 1,500-tick headless initial state, seed and orders, including software rendering. Logs record 27 Chromium cases, 34 browser unit tests, 285 build-system tests (one skip), 13 package contracts, native ARM syntax checks and native probe builds. Native/mobile full-game equivalence is not established by syntax checks; hosted CI was explicitly waived.

`pr-review.txt` records the sub-agent's feedback and fixes. `pr-final.patch` is the production change snapshot. Temporary experiment sources remain out of the production branch.

## Gradient investigation

`gradient-micro/` retains commands and JSON results; `gradient-benchmark.cpp` exercises the production kernels. `gradient-experiments.tar.gz` archives prototype sources. The tested scalar, native ARM, SSE-translated Wasm and direct Wasm SIMD variants agreed on 1,008 oracle cases (digest 742499262). SIMD improved some small fields but regressed large ones, so production retains scalar Wasm. The branchy scalar prototype was compiled but not benchmarked; it has no performance result.

## Reproduction

Build with `scons target=web release=1 web_profile=1 web-package web-tests -j4`. Serve the browser package with the repository's browser server. Run `GLOB2_PROFILE_SECONDS=15 GLOB2_PROFILE_FIXTURE=games/gd-bigarena-long.game.gz node browser/profile.js 'http://127.0.0.1:8782/?threads=serial&renderer=webgl2' artifacts/browser-performance/reproduced`; repeat with `threads=threaded`. See `browser/README.md` for profiler semantics, worker coverage and optional WebGL diagnostics.

Hosted CI is backlogged. The maintainer explicitly authorized merging with passing relevant local checks. Full native/mobile gameplay equivalence has not been established locally.
