# Terrain stall investigation

The moving zoom path repeatedly changes terrain composition admission. Per-cell SDL GPU render passes caused thousands of page attachment load/store cycles; composition switches discarded valid CPU pages at other densities. Consecutive unreduced cells now share their page pass, while reusable CPU pages survive switches within existing budgets. GPU admission reserves target bytes and the bounded coverage lease allowance. Reduced scratch passes and packet boundaries retain ordering.

## Fair retake

Each mode now runs in a fresh process with the same seed, camera path, frozen animation, 120 warmup frames and 240 measured frames. Seven alternating pairs include baseline Metal, optimized Metal and current legacy OpenGL. Optimized cache traces match exactly between cloud modes for all seven pairs. One baseline pair has different cache-hit counts from admission retries; completed-frame bytes/rebuilds and camera settings agree. The earlier combined-mode cloud result was biased by the preceding no-cloud warmup and is discarded.

Median of seven per-run values, milliseconds:

| Mode | Backend | Median | p95 | p99 |
|---|---|---:|---:|---:|
| 2D no clouds | baseline | 5.772 | 66.079 | 340.667 |
| 2D no clouds | candidate | 7.666 | 49.307 | 155.448 |
| 2D no clouds | legacy-gl | 1.272 | 5.922 | 77.421 |
| 2D clouds | baseline | 9.914 | 70.988 | 316.714 |
| 2D clouds | candidate | 10.107 | 43.647 | 138.298 |
| 2D clouds | legacy-gl | 4.259 | 12.012 | 83.833 |

Optimized p95 ranges are 41.401–54.034 ms without clouds and 43.305–44.231 ms with clouds. These ranges overlap; the percentile crossing does not establish negative cloud cost. The median cloud cost remains positive. No-cloud median regresses by about 33%; the change improves stall tails, not every frame. Remaining large frames still contain substantial CPU terrain generation/composition/upload work.

All eight baseline/optimized capture pairs are byte-identical; paired simulation checksums and viewports match. Peak candidate terrain cache is 268275456 bytes (cap 268435456), device 166869154 bytes (cap 268435456). Static scenes have one pair each and serve as smoke/correctness checks.

24 terrain presentation cases, 21 native Metal cases/221944 assertions, 21 native Vulkan cases/222062 assertions and 16 shader-tool tests pass. Native device source/test hashes match the final revision and remote tested bytes. Builds use dev_fast=0 release=1 / -O3, ordinary compilation, existing SDL 3.4.18 macOS and pinned 3.4.16 Linux. No simulation/save/replay/network changes.

See report.json for source/binary/fixture/shader hashes and every command/environment. raw-evidence.zip includes per-frame logs, one capture per exact before/after pair, tests, build logs, source and scripts. Native Metal runner metadata has a historical Vulkan label; the Apple factory selects Metal.

Limits: hidden-window completion/readback-inclusive timings, not gameplay FPS; early saved game; no comparative GPU timestamps; no Windows/iOS/full matrix or maintainer playtest. Latest fetched master adds unrelated packaging identities and merges cleanly.
