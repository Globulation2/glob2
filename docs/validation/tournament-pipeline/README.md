# Tournament pipeline validation

Real-engine comparison on macOS ARM64 using the exact same pinned binary/data,
128×128 Continents map (generator 44, map seed 16297475), Maxima versus Nicowar,
game seeds 23000–23023, 20,000-tick cap, 24 games, four execution slots and two
packing processes. Both runs retain initial/final saves and replays. Runs were
sequential; bundle and input warm-up is excluded, final collection is included.

| Measurement | Before | Pipelined |
|---|---:|---:|
| Wall seconds | 92.600 | 37.338 |
| Games/minute | 15.55 | 38.57 |
| Occupied execution slots | 45.0% | 79.8% |
| Engine CPU / four reserved cores | 39.0% | 67.2% |

This is **2.48× throughput** on this short-job workload, not a universal speedup.
Each game result and the raw initial-save, final-save and replay hashes matched
exactly between variants; `before-outcomes.json.gz` and `after-outcomes.json.gz`
retain all 24 comparisons. The tiny difference in total log bytes comes from output
path lengths, not game artifacts. Other work on the machine was not controlled by
this benchmark, so repeat on idle dedicated hardware for capacity planning.

`before.json` and `after.json` retain package IDs, commands, timestamps, sizes, and
measurements. Original complete results and worker artifacts remain under
`/Users/bradley/glob2-tournament-pipeline/artifacts/pipeline-{before,final}`.
The baseline tools came from master 05207be36; both runs use the Maxima tournament
bundle 1393313332d34faef1e0ba5b02805471d76df2ad9ac7ae11f5d0d5b6a4863a76,
source revision 86ad59ffe. No engine or AI code is changed by this PR.

Reproduce with `test/tournament_pipeline_benchmark.py`, using the commands in the
JSON reports and separate package roots/output directories. Compare decoded
`outcomes` and raw game artifact hashes, rather than timestamp/path-bearing logs.

Reliability checks: `python3 test/test_tournaments.py` (20 tests) and
`python3 test/test_tournament_pipeline.py` (11 tests). Coverage includes bounded
concurrent stages; execution and lease renewal while both collectors block beyond
the lease; pause/cancel during input delivery, final cancellation propagation and rejection of late enqueues;
host fairness when transfer capacity is smaller than host count; actual simultaneous compression and
shared-artifact acknowledgement; live limits and result-backlog backpressure;
persistent RPC reconnect and blocked-stdin timeout. Existing tests retain corrupt
chunk, expiry, lost-ack, duplicate acceptance, retry budget and restart coverage.

## SSH comparison

A separate pair uses devlaptop.local (Linux), four execution slots, two packers,
12 games with seeds 23000–23011, the same map and 20,000-tick cap, and pinned Linux
bundle `09545ff11d99bdede4296dca3c33b63fa8476ebb7f79514be738842c4a32ec4d`.
The coordinator runs on macOS; bundle and map warm-up is excluded.

| Measurement | Before | Pipelined |
|---|---:|---:|
| Wall seconds | 107.584 | 25.972 |
| Games/minute | 6.69 | 27.72 |
| Occupied execution slots | 14.1% | 61.4% |
| Engine CPU / four reserved cores | 13.1% | 56.7% |

This pair shows **4.14× throughput**; all 12 complete game results and raw
initial-save/final-save/replay hashes match exactly. `ssh-*.json` and
`ssh-*-outcomes.json.gz` retain the reports and comparisons. Full artifacts are in
`artifacts/pipeline-ssh-{before,final}`. Both final candidates include the
cancellation and host-fairness review fixes. The final local and SSH candidate
runs overlapped; the SSH game engine executes on a different machine.

An earlier candidate trial measured 47.572 seconds locally and 23.863 seconds over
SSH (1.95× and 4.51× against the same baselines). Its reports are retained as
`initial-local-candidate.json` and `initial-ssh-candidate.json`. This variation
reinforces that these are workload observations, not controlled capacity claims.
