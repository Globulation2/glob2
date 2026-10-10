# Final consolidated-source confirmation

Commit `c2ab2628bad246b616d0cd03d73fb03533877503`; binary SHA256 `260ac3b46cd8621fe11ad4bd08b8822a427cbc0d805591b2694135ed25979212`; OpenCL source SHA256 `06ccf58ee39e8d0c5faf927c3144473a347683f3833cde86a26e665bcc4b66ae`.

This is one confirmation pass of the final consolidated binary on the same three 512×512 fixtures, with CPU, forced OpenCL and automatic modes. It is separate from the earlier ea72 two-pass campaign. No seeds, map parameters, mode-selection parameters, or thread counts were changed. Each run uses eight compute threads, 20,000 ticks and async0.

## Elapsed times

| Map | CPU runtime / wall | OpenCL runtime / wall | Automatic runtime / wall | Auto regret runtime / wall |
|---|---:|---:|---:|---:|
| watershed | 38.326 / 41.865 | 41.615 / 45.242 | 40.785 / 44.526 | +6.41% / +6.36% |
| bajada | 43.571 / 47.932 | 44.905 / 49.448 | 41.089 / 45.691 | -5.70% / -4.68% |
| contested-commons | 46.933 / 49.672 | 49.207 / 51.999 | 46.586 / 49.400 | -0.74% / -0.55% |

Across these three final-binary observations, automatic versus CPU (also the best forced mode in each case) has an equal-case geometric mean of −0.13% runtime / +0.28% full wall. Forced OpenCL versus CPU is +5.47% / +5.29%. Automatic wins two cases and loses watershed by +6.41% / +6.36%. This single pass supports a near-tie aggregate description, not an automatic-speedup claim or an isolated pitch24 whole-game gain.

Seconds above include all 20,000 ticks in runtime; full wall also includes startup and final saving. These are single observations, useful as final-binary checks but not a new estimate of statistical confidence or a paired before/after speedup claim.

All nine initial/final checksums, compressed final-save hashes, tick counts and terminations match the original CPU reference. Every save was re-read by the audit. Requested and resolved compute counts are eight, histograms contain 19,000 measured ticks, and the seven deterministic gradient workload counters agree across modes. Excluded timings: 0; overlap events among valid samples: 0.

The same CPU0–7/SMT16–23 reservation, CPU15 observer, cooperative GPU0 per-match lock, and disjoint-background policy apply. The lock excludes our benchmark jobs only; desktop graphics remain active. Raw rows preserve clocks, temperature, background processes, CPU usage and all performance counters. This check does not capture per-tick checksums or establish cross-platform compatibility.

Artifacts: `cleanup-final-plan.json`, `cleanup-final-confirmation/metadata.json`, `cleanup-final-confirmation/results.json`, `cleanup-final-analysis.json`, `cleanup-final-audit.json`, `source/cleanup-final/manifest.json`. Earlier repeated evidence and its recommendation remain in `activation12-two-pass-report.md`.

A preflight-only artifact failure was preserved in `cleanup-final-preflight-failure`: the source snapshot initially omitted unchanged Headless.cpp after cleanup restored it to baseline. It was supplemented from verified final HEAD before any match launched; zero timings were taken or discarded by that preflight.
