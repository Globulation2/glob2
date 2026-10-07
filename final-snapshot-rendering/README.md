# Snapshot rendering: final integrated validation

Source: `1807fbf8e48cf57f3f211841eb1a2568804070b5` (top of #877, #879, #881, #882, #885, #887). Base: `a60018aa0066d1f4dc5e733cb86d0085b80bd604` (master #886 gradient changes integrated throughout the stack). Includes the independent wasm32 declaration repair from #888 (`d98c0db288f0417b11214119e6fa7181995a953f`). No simulation-version, save-format or wire-order change.

## Scope and boundaries

Routine native client frames no longer park the simulation owner. Selection, hit testing, building actions and placement use the displayed immutable Scene. Area strokes have a client-owned preview until the displayed Scene includes their execution acknowledgement. Orders carry local-only world/incarnation identity, validated at admission. Telemetry and Scene requests use synchronized mailboxes. Autosave scheduling crosses a pending request; actual save capture and settings/lifecycle/diagnostic operations retain explicit owner boundaries.

Turn/browser hosts retain their existing simulation and transport owner thread. Their Scene preparation uses the shared executor, with an explicit preparation pump when no worker exists. Graphics API submission remains on its required context thread. Capture still runs on the simulation owner, and a no-worker host necessarily does preparation there. This is not a claim that all rendering has zero owner-thread cost.

## Environment and commands

See [environment](combined-environment.json) and [commands](commands.sh). Linux x86_64, GCC 15.2, native release `-O3`, SDL 3.4.16; Emscripten 4.0.15/Clang 22, web release `-O2`, serial and pthread builds. SDL_image/ttf/net and browser versions are in the manifest. Native build and web packaging passed. Source boundary and whitespace checks passed.

The command file gives reproducible invocations; individual case inventory and results are in the JUnit XML. Native test processes use isolated profiles and display cases use the runner's Xvfb setup. Build output records actual compiler/dependency flags. Browser invocations are sequential because the configuration owns its HTTP server.

## Native correctness

- [209 engine cases](combined-engine.xml): all passed, zero skipped. Covers Scene capture/preparation and retained snapshots, input/selection/touch, real runner input without routine parking, client channels, turn handling, shared-worker teardown, AI/snapshot gradient integration, save/load and farm areas. [Log](combined-engine.log), [artifacts](native-engine-artifacts.tar.gz).
- [19 unit cases](combined-unit.xml): all passed; executor, Scene buffer, telemetry and fog fading. [Log](combined-unit.log).
- [2 Scene source-boundary tests](combined-scene-boundary.log): passed.
- [Five serial/threaded scenarios](combined-sim-equivalence.log): new-game, maxima, generated-load, legacy v121 and resume. Exact per-tick checksum sidecars, replay bytes and final saves matched. [Artifacts](native-equivalence.tar.gz).
- [The same five scenarios with one compute thread and snapshot verification](combined-fallback-equivalence.log): all matched. [Artifacts](fallback-equivalence.tar.gz).
- Native reference: 1,500-tick cross-replay fixture and 702-checksum committed multiplayer record. Browser comparisons below use those same references. The native multiplayer trace matches the committed golden.

## Performance

The end-to-end diagnostic uses the same executable for both arms: a forced frame-wide owner boundary versus ordinary snapshot input. Each arm runs five seconds on balanced.map, seed 123, two compute threads, maximum simulation speed, 800×600 portable graphics. It is not a comparison against an older executable, equal final world states, or event-to-photon latency. Whole-frame time includes drawing; input time excludes it.

Three runs per core allocation, medians of the three run statistics:

| Physical cores | Statistic | Forced parking | Routine input |
| --- | --- | ---: | ---: |
| 4 | Input p95 | 1.013 ms | 0.160 ms |
| 4 | Whole-frame p95 | 25.129 ms | 24.867 ms |
| 4 | Scene age p95 | 47 ms | 50 ms |
| 4 | Completed frames per arm | 242 | 258 |
| 2 | Input p95 | 2.517 ms | 0.220 ms |
| 2 | Whole-frame p95 | 70.171 ms | 69.648 ms |
| 2 | Scene age p95 | 135 ms | 134 ms |
| 2 | Completed frames per arm | 119 | 145 |

[Every run](combined-frame-results.csv), [summary](combined-frame-summary.json). Input time consistently improved in these runs; whole-frame/age tails remain variable. In the first two-core run, whole-frame p95 regressed from 72.6 to 148.1 ms and Scene age from 135 to 271 ms. Two compute participants plus the native client contend on two physical cores at uncapped simulation speed. This is a stress configuration; no general FPS or tail-latency improvement is established. This was not an exclusive host. Other work from this task was pinned away from the benchmark cores and their SMT siblings, but system work was not controlled. No confidence interval or statistical significance claim.

[Extraction microbenchmark](combined-scene-benchmark.log): 128²/512²/1024² maps, 512 units, seed fixed by harness, five warmups/40 samples, unchanged and one-cell-changed terrain, three retained Scenes. At 1024² unchanged, synchronous extraction median was 15.293 ms, owner capture 0.367 ms, and worker preparation 2.779 ms. Changed terrain: 14.179 / 0.411 / 2.836 ms respectively. These are separate measured phases, not end-to-end speedup. Snapshot pool capacity at 1024² was 35,293,512 bytes unchanged / 82,479,432 bytes changed with retained Scenes; leased payload 35,105,552 / 66,563,472 bytes. Pool accounting excludes registry allocations and allocator overhead, and is not a total-memory comparison to the old engine. Raw undermap input adds one byte per terrain cell per retained terrain version; renderer-derived undermap work stays in preparation.

## Browser results

All 16 selected browser cases passed across their final attempts: Chromium threaded startup/shutdown, repeated save reload and resize (3); Chromium/Firefox no-worker fallback reload and resize (4); Firefox threaded startup/shutdown and resize (2); Chromium serial/1 and pthread/1,2,4 per-tick traces plus two match verifications (6); Chromium WebGL2 gameplay/resize (1).

The four 1,500-tick Wasm traces match native byte-for-byte (`5570a99e4345b5aa80d1f14928c4a5e5b37a03244b9327310ca1d0760d3df7e4`). Both browser match verifications match the committed 702-checksum golden (`c02adc67dc9e6b9a30b289f78038d72c3b7550c95db84025b28d65f49bf0cd38`). [Comparison](combined-browser-trace-comparison.json), [raw traces](combined-browser-traces), [native reference](combined-browser-native-reference).

Logs: [Chromium threaded](combined-browser-chromium-threaded.log), [resize rerun](combined-browser-chromium-resize-retry.log), [serial fallback](combined-browser-serial.log), [Firefox threaded](combined-browser-firefox-threaded.log), [determinism](combined-browser-determinism.log), [WebGL2](combined-browser-webgl2.log). The [WebGL2 screenshot](webgl2-match.png) was inspected: terrain, fog, units, building and HUD draw coherently. The automated case also checks the selected renderer, nonempty pixels, resize and no GL errors. This is limited visual evidence, not human play review.

 The initial Chromium run passed startup/shutdown and repeated save reload, but resize setup timed out after 30 seconds waiting for MainMenuScreen (still loading). The isolated resize rerun passed in 46.3 seconds. Both logs are retained; this initial failure is not represented as a clean first-pass suite.

## Limits and review

No Windows, Android, WebKit, sanitizer or full CI matrix run. Compatibility evidence is Linux serial/threaded plus browser Wasm32 for the specified fixtures, not proof for every platform/workload. No separately built original-base binary performance comparison. Browser graphics use software-backed/headless contexts. Human play review is still needed for input-on-displayed-frame behavior, painting feedback, animation smoothness and pacing. Draft PRs remain unmerged.

All linked archives/logs are in this dedicated evidence branch, not production documentation. SHA256SUMS covers published files.
