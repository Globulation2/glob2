# Compact unit cores: review evidence

The refactor uses a 132-byte authoritative core and a 160-byte snapshot core (including relationship metadata), within the 192-byte budget on Linux x86-64 GCC 15.2. Full observation payload is 376 bytes (160 core + 216 details), versus the previous 384-byte record. Full observations borrow both arrays by value; there is no second materialized full-unit array. Save/checksum ordering remains explicit and unchanged.

Guard crowding requests cores only. Market gradients request buildings only: zero unit-state bytes are copied for a standalone market request. Rendering, movement and applicable AI scans share cores; details remain optional. The combined-request tests assert component reuse across requirement upgrades and compare scalar reference seeds and propagated values through supplier stock changes, depletion, inside status, dead units and foreign units. Snapshot lifetime, invalidation, generation and retained-lease tests cover the new arrays and shared index.

## Isolated release measurements

Before: `3a3917757752fe64a2af9c1dc964c074794ebf3e`. After: `5a40f7a9f281cd1aa71c6980573cee3f6f35a36f`. Linux x86-64, GCC 15.2, ordinary SCons release=1 (-O3); pinned existing SDL/dependency prefixes. No runtime dependency added. One warmup, ten paired repetitions with rotating order. Independent timing, correctness and Callgrind runs. Paired log-ratio bootstrap (20,000 resamples, seed 713) supplies 95% intervals. These are capture costs in small diagnostic fixtures, not qualified movement trial results.

Later master CPU-gradient and CPU-render changes are excluded from this comparison. Ordered admission and incremental/effective-goal route optimization candidates remain separate; they were not combined with this refactor's timings.

| 1,024 units / 128 advancing captures | Before logical copy/payload MiB | After MiB | Capture time reduction (95% CI) |
|---|---:|---:|---:|
| movement | 49.302 | 21.302 | 64.93% (63.24–66.61%) |
| guard | 49.050 | 20.500 | 66.71% (65.48–67.96%) |
| market | 49.050 | 0.550 | 99.05% (99.00–99.10%) |
| render | 49.467 | 21.467 | 64.78% (63.64–65.66%) |
| render-panels | 49.467 | 48.467 | 49.84% (46.74–52.90%) |
| render-movement | 49.467 | 21.467 | 64.64% (63.01–66.11%) |
| render-ai | 49.467 | 48.467 | 47.55% (45.08–50.13%) |
| render-all | 49.467 | 48.467 | 51.38% (48.37–54.60%) |

The `bytes_copied` counter includes populated record payload and indexes; it is not a claim about exact memcpy traffic. New `unit_state_bytes_copied` counts actual state memcpy bytes, `unit_record_payload_bytes` counts populated core/detail payload, and capacity/retained counters report retained buffers. For guard-only, these are 17,301,504 state bytes and 20,971,520 record payload bytes across 128 captures. For market-only both unit counters are zero. Rendering plus full AI requirements copies 45,613,056 state bytes and populates 49,283,072 record bytes; adding guard and market does not increase those totals. The render-AI case requests full AI observations but does not step an AI controller. Allocations are reported in raw samples: full shared captures increased tracked allocations (62 to 88); smaller copying does not imply fewer allocations.

Optimized Callgrind scoped to `Store::captureBoundary` reports 35,246,712 -> 19,461,876 instructions (44.78% fewer) for guard-only. Startup outside capture is excluded; warm captures are included. This establishes causal instruction savings, not runtime benefit for the whole engine.

## Engine and drawing outcomes

- engine large-colony/reference: -4.11% reduction; 95% interval -11.47–2.60%.
- engine large-colony/shadow: 2.91% reduction; 95% interval 0.14–5.62%.
- engine ordinary-control/reference: -3.65% reduction; 95% interval -11.32–3.90%.
- engine ordinary-control/shadow: 1.11% reduction; 95% interval -7.13–10.93%.
- render 64: -1.85% reduction; 95% interval -7.35–3.28%.
- render 1024: -3.01% reduction; 95% interval -11.84–4.37%.

The preliminary large-colony shadow K=1 run has a small positive engine interval. Reference engine controls and drawing intervals include no change, so their runtime benefit/regression is inconclusive. Capture cost reduction is supported; broad runtime improvement is not established. Synthetic 64/1,024-unit capture fixtures supplement the existing ordinary and preliminary colony engine controls. They do not qualify a sustainable 1,000-active-worker gameplay corpus.

The original software Torus captures accidentally read the stale OpenGL startup logo. Those images are not visual parity evidence. A benchmark-only correction reads the current software drawing surface after flushing; corrected before/after images and their comparison are recorded separately: the 64- and 1,024-unit scenes each match byte-for-byte, with different hashes between scene sizes. Existing actual GUI/scene/render suites are also retained. Performance runs remain valid timing observations; their image hashes must not be treated as gameplay screenshots.

## Correctness and known failures

Four engine fixtures, 64 ticks, four compute participants, reference and shadow delays 1/2/4/8: final saves, replay bytes, replay checksum files and per-tick checksum files match the original release baseline exactly (`correctness/comparison.json`). Focused movement tests additionally exercise the delay/participant matrix and save/lease/supersession boundaries. All ability indices are checked against full captures and save/load. Representation-only version constants and compatibility floor remain unchanged.

Development engine focused inventory: 262 pass / 1 failure / 1 skip initially. The Nicowar cold combat-role lookup error was fixed and a regression test added; correction runs passed including the long native-controller case. Final integration focused inventory before the latest renderer base update: 144 pass / 1 baseline LAN failure / 2 skip.

Broad unit inventory: 1,061 pass / 2 failures / 88 skips. Maxima's fixed CPU-time guard fails in the development build but passes in release. ResourceRegistry's maximum-size JSON fixture fails identically against original baseline source. Broad compatibility inventory: 876 pass / 6 failures / 68 skips. Three process timeouts pass when rerun with 600-second process deadlines and unchanged assertions. Two old golden failures and the LAN wake-time failure reproduce identically in a separately built pre-refactor checkout. Golden fixtures were not rewritten to hide failures.

Under Xvfb/Openbox with SDL X11 forced: 25 actual display cases pass. The touch/window-resize case fails its resize-settle assertion both before and after. Initial Wayland display failures are environment failures, not a passing visual test. Baseline reproductions, JUnit XML and logs are included. The 24 CI-policy tests and documentation checks pass; native compatibility registration and shared-header boundaries are updated.

This is Linux-only local evidence. macOS arm64, Windows x64, browser serial/threaded execution, complete screenshot scenario coverage, allocator-inclusive memory ceiling, 30-seed gameplay distributions and footage review remain unavailable/unqualified. No cross-platform equivalence claim is made.

## Recommendation

Retain this refactor in the draft: core size, canonical Linux comparisons and reduced copying/capture cost support it. Whole-engine/render runtime benefit is separate and mostly inconclusive. Investigate any statistically supported regressions before recommending integration. Movement stays Stage A, diagnostic only; earlier gates remain failed/inconclusive, and no delay is qualified for live adoption. No thresholds were changed.

The archive includes commands, helper sources, paired raw samples, binary/fixture hashes, Callgrind profiles/annotations, JUnit reports, baseline failure reproductions, saves, replays and checksums. Build outputs, executables, virtual environments and personal preference directories are excluded. Commands contain original local dependency paths as provenance, not as dependencies required by reviewers.

## Final integration revision

Final source: `523bac7c7874dd0e6c4b9667d7428342a5444bcd`; master base: `277510840054b1416f2defd7d86fe1431dd0ddc0`, fetched again before publication. Latest master includes CPU gradient (#1158) and CPU rendering (#1160) optimizations; integration checks use these, while the paired performance comparison remains frozen at the isolated revisions above. Final engine behavior was compiled with the latest-master development and release builds; the final follow-up only corrects diagnostic software capture. No simulation/version constants changed.

Latest-master focused engine check: 122 pass / 0 failures / 2 skips; terrain check: 21 pass / 0 failures / 15 display skips; SurfaceCoverage unit check: 15 cases pass / 3 display skips. These supplement the broader inventories and baseline failure reproductions described above. Final canonical results are in `final-correctness/comparison.json`; build logs and source metadata are included.

Byte-identical archive entries are deduplicated. `manifest.json` maps every logical output path to its stored `archive_member`, byte count and SHA-256, preserving all variant comparisons without storing repeated identical checksum files.

## Subsequent runtime evidence

The original Torus timing prepared its Scene once, so its drawing results do not include fresh capture. A [separate capture-inclusive follow-up](runtime-report.md) now establishes supported large-colony pipeline benefit, with a static reuse attribution control, longer engine runs and targeted panel replication. Original measurements and limitations above remain intact.
