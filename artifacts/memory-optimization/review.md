# Memory optimization validation

Implemented compact Maxima fields and farming buffers, shared immutable geometry/water caches, compact growth counters, and reduced transient save/load copies. Legacy wire formats and engine version gates remain unchanged.

## Live allocations

| Capture tick | Baseline MiB | Candidate MiB | Saved MiB |
| --- | ---: | ---: | ---: |
| 256 | 322.85 | 264.40 | 58.45 |
| 15104 | 755.60 | 641.76 | 113.84 |
| 30208 | 958.20 | 864.59 | 93.62 |
| 45056 | 1290.97 | 1111.57 | 179.39 |

Late load/save peak RSS: 4.91 GiB → 3.96 GiB (one sequential pair).

## CPU comparison

Negative percentages mean less process CPU. The one-sided 95% paired-bootstrap upper bound must be at most +2%. Early/middle fixtures used sequential alternating pairs; populated fixtures used 100ms within-pair interleaving after sequential timings proved noisy. Both interleaved processes stay resident.

| Initial tick | Pairs | Load change / upper bound | Simulation change / upper bound | Save change / upper bound |
| --- | ---: | ---: | ---: | ---: |
| 0 | 7 | -1.40% / -0.41% | -3.40% / -2.06% | -0.54% / +0.22% |
| 15000 | 7 | -1.67% / -1.43% | -3.45% / -2.53% | +0.11% / +1.17% |
| 30000 | 7 | -2.02% / -1.79% | -5.05% / -4.75% | -3.47% / -1.43% |
| 45000 | 7 | -2.07% / -1.50% | -4.63% / -3.85% | -0.72% / +1.73% |

CPU gate complete: **True**.

## Compatibility and verification

The uninterrupted 512×512, 11-AI, map-seed-42/game-seed-19 comparison matched all 6,766,753,746 detailed simulation bytes through 45,000 ticks. Checkpoint saves, final save and replay bytes matched exactly. All resumed benchmark pairs also check final simulation checksums and compressed save hashes.

The full unit run contains 393 passing cases. Relevant Maxima suites (142 cases), gzip/stream, compact counter, gradient lifecycle and continuation checks passed. Client and server release builds succeeded. Isolated farming and distance-field kernels passed the CPU gate with identical output hashes. See the JUnit files and raw logs in this directory.

## Review limits

Only native macOS arm64 execution was verified; Linux/Windows replay equivalence and interactive maintainer playtesting remain unverified. The shared-machine timing conditions do not establish a portable speedup. Level-zero gzip deliberately keeps legacy buffering because changing zlib output capacity changes its stored-block bytes.

Exact commands, binary/fixture hashes, raw timings, per-pair output hashes, allocation captures and category totals are retained under this ignored artifact directory. `validation.json` collects the results; the sequential timing limitations are retained under `cpu-final/` and `cpu-populated/`.
