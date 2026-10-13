# Shared-snapshot hiring preparation: frozen before/after

Tested source: `db0e4b064344e4f6aa88c998093944240f668a3d`; comparison engine base: `6b95c81c0d438f64e3391ff4785bc95d3436d174` (SIM48). Both controls use this engine base. Old preparation/planner comes from `c2fa28a103ed0726c57b25770313bc47df3afbb9`. Later master integration was stopped at the user’s request and is not part of these numbers.

## Quiet release timing

Twelve retained generated 256×256 maps, eight Nicowar players, four compute participants, 16,384 ticks with 4,096 warmup ticks, K4/R4 and vacancies+2. Ten rotated, alternating before/after pairs per map; process warmup once per map/arm. Diagnostics, traces and saves disabled. Same cached material policy in both arms. macOS arm64 ordinary release builds.

120 valid pairs; 9 build/test-overlapped pairs retained and excluded, then repeated. Outliers retained; OS/UI background activity recorded and remains a limitation. These are fixed-case timing estimates, not a new gameplay study.

| Metric | Before (normalized) | After | Paired median delta | Paired bootstrap 95% interval |
|---|---:|---:|---:|---:|
| Measured elapsed | 100 | 98.97 | -1.03% | -1.85% to +0.42% |
| Process CPU | 100 | 98.57 | -1.43% | -1.86% to -0.62% |
| Tick p95 | 100 | 99.84 | -0.16% | -1.18% to +1.02% |

Delta: geometric mean of the twelve within-map median paired ratios. Interval: 10,000 paired within-map bootstrap draws (seed713105), retaining ten pairs per map. Process CPU includes the whole run and final drain; measured elapsed excludes initial tick warmup and includes final drain. Tick p95 is the engine’s measured-window latency statistic.

| Map | Before elapsed (s) | After elapsed (s) | Paired elapsed delta | Paired CPU delta |
|---|---:|---:|---:|---:|
| islands-2017 | 7.202 | 7.204 | -0.70% | -1.68% |
| islands-4093 | 7.773 | 7.550 | -0.98% | -1.69% |
| islands-713 | 6.595 | 6.514 | -3.99% | -2.32% |
| islands-8191 | 7.382 | 7.428 | -1.86% | -1.82% |
| river-2017 | 8.173 | 8.456 | -1.18% | -2.19% |
| river-4093 | 4.375 | 4.326 | -0.55% | -0.51% |
| river-713 | 6.206 | 6.445 | -0.24% | -0.39% |
| river-8191 | 8.072 | 8.172 | -0.71% | -1.58% |
| swamp-2017 | 6.043 | 5.937 | -1.66% | -2.55% |
| swamp-4093 | 9.526 | 9.528 | -0.70% | -1.04% |
| swamp-713 | 4.725 | 5.192 | +1.49% | +0.68% |
| swamp-8191 | 7.308 | 7.369 | -1.17% | -2.03% |

## Separate diagnostic breakdown

One instrumented run per case/arm, not the quiet paired estimate. Owner scheduling counters and fact capture cover the full 16,384 ticks; isolated owner consumption/snapshot timers cover the 12,288 measured ticks. Do not add timers across those windows. Publication includes joins; deadline waiting is also reported separately.

| Case | Owner route-handle/input capture before→after (ms) | Owner scheduling preparation before→after (ms) | Worker preparation after (ms) | Added sparse world payload (MiB/full run) |
|---|---:|---:|---:|---:|
| islands-713 | 729.09→526.24 | 124.54→39.15 | 281.43 | 74.45 |
| river-713 | 176.29→84.24 | 29.44→6.22 | 65.44 | 64.12 |
| swamp-713 | 157.66→79.49 | 29.56→5.37 | 51.87 | 57.60 |

The planner references normal immutable snapshot components directly. No per-team world-record copies or owner-built demand/supplier/attack vectors remain. One shared boundary route-handle vector and job/output objects still allocate. Observed funding facts require sparse 180-byte records for buildings with nonzero reserved/local-reserved/funding state. Packed index, supplier presence and attack modifier use existing record space: measured Mac layouts remain building408/unit384/team2048 bytes. This is near-zero incremental record copying, not literally zero: sparse funding adds approximately 0.4–0.7% of captured payload in the diagnostic cases. There were 63–69 funding-vector growths over16,385 captures. Growth counters are scoped, not a process-wide allocation census. Packed supplier-bit extraction time remains within normal team snapshot capture, outside the new-fact batch timer. Full phase, submission, publication, consumption and allocation counters are in phase-findings.json and raw diagnostics.

Existing shared building route fields retain lifetime/generation/locking rules. Workers calculate demand, apportionment, supplier modes, team qualification and visit ranks; live owner eligibility, admission and hiring rounds are unchanged. The private live-consumer wrapper’s route bootstrap remains owner-side, measured separately; this prototype does not claim production persistence or elimination of all owner route setup.

## Verification and limits

190 focused test cases passed (474,474 assertions; zero failures/errors). Three8192-tick map-family controls compare legacy cached→fresh before→after: per-tick world checksums and candidate publication streams agree; after participants1/2/4/8 agree. Experiment-disabled before/after traces agree. Ordinary release golden CLI matches committed trace SHA2287760e6a3f627e08584369f4cb7c202019d69458be4751d42790ebd19b3601. No simulation revision bump: observed behavior was preserved.

Coverage is macOS arm64, focused harnesses and private opt-in material-consumer controls. No claim of Windows/browser/Linux final-head equivalence, shipping readiness, full production cached-state persistence or general gameplay equivalence. Earlier 1,200-attempt cohort and retained Nicowar OOM evidence remain separate and unchanged. Keep PR draft; normal production hiring stays authoritative.
