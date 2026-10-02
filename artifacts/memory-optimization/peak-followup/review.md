# Save/load peak memory improvements

Extends the prior optimized native build with chunked snapshots and direct gzip I/O. No disk spilling, save-format/version changes, or simulation changes.

Candidate SHA256: `54a1a91b347537d045a5355972c0d603fece361d1a53f32a4c7d51ec5b94fe6c`.
Baseline SHA256: `d7d778fd3cf991d5f5c3cfdb055c959fc75d8168e669078427e45a30dc662285`.

## Peak resident memory

Separate native processes, no malloc logging; default gzip compression. Each run loads the fixture, advances one tick, and optionally saves. Exact commands, binary hashes, and save hashes are in `peak-comparison.json`.

| Fixture tick | Load baseline → candidate, GiB | Load/save baseline → candidate, GiB |
| --- | ---: | ---: |
| 0 | 0.157 → 0.125 | 0.273 → 0.170 |
| 15,000 | 2.118 → 1.090 | 2.141 → 1.107 |
| 45,000 | 2.982 → 1.893 | 3.959 → 1.927 |

Late-game peak fell 51.3%; the ≤2.5 GiB gate passes. The earlier pre-edge-fix pilot measured 2.07 GiB; final measurements above use the final binary.

## Snapshot capture and compression

Seven paired interleaved processes use the same final engine and saved state, comparing contiguous versus chunked deferred snapshots. CPU time excludes scheduler pauses; wall time in this condition includes injected pauses and is not an interactive GUI latency estimate. Each pair produced byte-identical compressed saves.

| Stage | CPU change | One-sided 95% upper bound |
| --- | ---: | ---: |
| capture | -34.69% | -30.90% |
| hash | -3.76% | +0.36% |
| compression | +0.63% | +1.32% |
| total | -2.95% | -2.27% |

Seven uninhibited alternating capture triplets use the same warmed engine; reservation is included in capture time. This separately checks the prior autosave reservation, rather than only growing contiguous buffers.

| Baseline capture | CPU change | CPU 95% upper | Wall change | Wall 95% upper |
| --- | ---: | ---: | ---: | ---: |
| growing | -14.06% | -10.73% | -14.13% | -9.76% |
| reserved | -6.84% | -4.98% | -5.84% | -2.67% |

Late snapshot: 1,023,964,056 bytes; allocated blocks: 1,024,458,752 bytes; block table: 8,192 bytes. Capacity is within one 1 MiB block of its byte length. Idle releases owned blocks; allocator retention can keep resident memory elevated afterwards.

## Native full-game CPU gate

Complete: **True**. Alternating pairs with 0.1-second interleaving, 1024 warmup ticks and 4096 measured ticks. The runner was paused during the mid-game set for the separate capture check and during the late-game set for the final integration recheck; process CPU clocks exclude those pauses. Compare against the prior optimized binary, not the original pre-optimization engine. Each pair checks final simulation checksums and exact compressed saves.

| Tick | Stage | Pairs | CPU change | 95% upper bound |
| --- | --- | ---: | ---: | ---: |
| 0 | benchmark_setup_cpu_ns | 7 | -16.43% | -15.65% |
| 0 | benchmark_run_cpu_ns | 7 | -0.02% | +0.41% |
| 0 | benchmark_save_cpu_ns | 7 | -2.33% | -1.63% |
| 15,000 | benchmark_setup_cpu_ns | 7 | -31.03% | -30.26% |
| 15,000 | benchmark_run_cpu_ns | 7 | +0.18% | +0.71% |
| 15,000 | benchmark_save_cpu_ns | 7 | -1.17% | +0.45% |
| 45,000 | benchmark_setup_cpu_ns | 7 | -34.54% | -33.44% |
| 45,000 | benchmark_run_cpu_ns | 7 | -1.06% | +0.14% |
| 45,000 | benchmark_save_cpu_ns | 7 | -3.22% | -1.27% |

## Compatibility and tests

All 438 selected cases passed, including 393 unit cases, persistence fault handling, legacy version 84/88 saves, version 108/121/124 fixtures, and replay/network acceptance boundaries. Client and server release builds succeeded.

Continuations from ticks 0, 15,000 and 45,000 matched all 1024 detailed ticks per fixture (371,421,950 bytes total), final saves and replays. Complete matched checksum streams are retained as `continuation-*-checksums.bin.gz`; raw duplicate streams were removed.

Linux/Windows execution and an interactive maintainer playtest remain unverified. Autosave may pause while a preceding writer completes. Optional level-zero compression retains the legacy whole-buffer path and is outside the default-save memory gate.

Pre-edge-fix CPU pilot results are not the final CPU acceptance evidence. Build logs, JUnit, fixture/binary hashes, raw CPU results, save files, helper sources and commands remain in this ignored artifact directory.
