# Tournament pipeline validation

Real-engine comparison on macOS ARM64 using the exact same pinned binary/data,
128×128 Continents map (generator 44, map seed 16297475), Maxima versus Nicowar,
game seeds 23000–23023, 20,000-tick cap, 24 games, four execution slots and two
packing processes. Both runs retain initial/final saves and replays. Runs were
sequential; bundle and input warm-up is excluded, final collection is included.

| Measurement | Before | Pipelined |
|---|---:|---:|
| Wall seconds | 92.600 | 47.572 |
| Games/minute | 15.55 | 30.27 |
| Occupied execution slots | 45.0% | 83.7% |
| Engine CPU / four reserved cores | 39.0% | 67.7% |

This is **1.95× throughput** on this short-job workload, not a universal speedup.
Each game result and the raw initial-save, final-save and replay hashes matched
exactly between variants; `before-outcomes.json.gz` and `after-outcomes.json.gz`
retain all 24 comparisons. The tiny difference in total log bytes comes from output
path lengths, not game artifacts. Other work on the machine was not controlled by
this benchmark, so repeat on idle dedicated hardware for capacity planning.

`before.json` and `after.json` retain package IDs, commands, timestamps, sizes, and
measurements. Original complete results and worker artifacts remain under
`/Users/bradley/glob2-tournament-pipeline/artifacts/pipeline-{before,after}`.
The baseline tools came from master 05207be36; both runs use the Maxima tournament
bundle 1393313332d34faef1e0ba5b02805471d76df2ad9ac7ae11f5d0d5b6a4863a76,
source revision 86ad59ffe. No engine or AI code is changed by this PR.

Reproduce with `test/tournament_pipeline_benchmark.py`, using the commands in the
JSON reports and separate package roots/output directories. Compare decoded
`outcomes` and raw game artifact hashes, rather than timestamp/path-bearing logs.

Reliability checks: `python3 test/test_tournaments.py` (20 tests) and
`python3 test/test_tournament_pipeline.py` (8 tests). Coverage includes bounded
concurrent stages; execution and lease renewal while both collectors block beyond
the lease; pause/cancel during input delivery; actual simultaneous compression and
shared-artifact acknowledgement; live limits and result-backlog backpressure;
persistent RPC reconnect and blocked-stdin timeout. Existing tests retain corrupt
chunk, expiry, lost-ack, duplicate acceptance, retry budget and restart coverage.
