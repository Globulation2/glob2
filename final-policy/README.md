# Final Maxima policy verification

Candidate: `1842a8f1db4b66740a654eeaeae61113eb4f4f13`. Master control: `c2c086ad5b08bace7c27399d4eaa0ffd3f950d45`. Both include master’s on-demand building gradients. Candidate keeps permanent dotted interior/checkerboard boundary wheat seeds and renewable wood reserves, removes supplemental expansion support and maturity toggles, and includes the behavior-preserving capped food-query optimization.

## Functional and platform validation

All 19 Maxima suites pass on Linux. All pass on macOS across the initial run and a focused retry: the initial concurrent run exceeded the unchanged farming standalone 100 ms timing assertion (113.854 ms); the focused retry passed. Logs retain both outcomes.

Full checksum sidecars match byte-for-byte between macOS and Linux for 23,040 initial ticks and 11,520 resumed ticks across two fresh maps, the crowded save, and retained v115/v117 saves. The matching sidecar SHA-256 manifests and per-tick team/entity hashes are attached. The retained v115 baseline was updated only after platform equality and its 256-tick midpoint continuation passed. Windows execution was not checked here.

Save/reload continuity passes for g52 (4096 ticks), crowded (2048), and v115 (256). It fails for g1 at6171 and v117 at7808 on both platforms. Untouched master’s corresponding g1 test passes. The g1 order diagnostic identifies 89 matching post-save orders, then the same Nicowar building request one tick later after reload; no preceding Maxima order difference. Echo/Nicowar's unsaved planning caches are a hypothesis, not an established root cause. The PR remains draft pending this continuation investigation; platform parity must not be confused with save/reload parity.

## Current-master CPU comparison

Release Linux builds, 8192 ticks from identical developed saves, two repetitions each pinned to separate physical cores0 and2, randomized variant/case order within each core. No builds or diagnostic game reruns overlapped these timings. These are scenario-specific total simulation CPU measurements, not a statistically broad policy tournament or an isolated estimate of support-removal cost.

| Save | Master CPU seconds | Candidate CPU seconds | Change |
| --- | ---: | ---: | ---: |
| Crowded Rugged Isles | 34.240 | 35.015 | +2.3% |
| Developed g15 | 21.615 | 15.300 | -29.2% |
| Developed g52 | 65.055 | 50.630 | -22.2% |

Per-run commands, CPU files and result JSON are under linux/benchmark. Benchmark inputs are gzip-compressed under inputs (developed-g15.game.gz, developed-g52.game.gz and crowded.game.gz); decompress before running. The map/save validation inputs and diagnostic replays are compressed too. validate.py, benchmark.py and summarize.py retain exact commands; host paths need adjustment on another machine. Source is the candidate commit above. Full raw checksum sidecars remain in the original local/therig artifact directories; attached hashes and initial saves allow reproducing every comparison. Order-diagnostic replays and orders are attached.
