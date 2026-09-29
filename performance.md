# Final combined performance

Baseline ef1f90ed1 → final b8b328771. Therig, release GCC build, one gradient worker with delay eight, pinned to CPUs 8 and 10. Each cell is a median of three measured runs after a discarded warmup. Positive reductions mean faster. Ranges are sample minima/maxima, not confidence intervals.

| Workload | Ticks | Baseline simulation, s (range) | Candidate simulation, s (range) | Reduction | Whole-process before → after, s | Process reduction |
|---|---:|---:|---:|---:|---:|---:|
| Mixed AIs, 512×512 | 1,000 | 7.970 (7.947–8.047) | 6.992 (6.837–7.072) | 12.27% | 17.632 → 16.602 | 5.84% |
| Castor/Numbi, 256×256 | 10,000 | 9.045 (8.861–9.116) | 8.502 (8.473–8.586) | 6.00% | 10.211 → 9.695 | 5.05% |
| Late Maxima/Cortex, 512×256 | 1,000 | 25.384 (25.381–25.492) | 23.283 (22.849–23.333) | 8.28% | 38.738 → 36.612 | 5.49% |
| 12 Cortex, islands, 512×256 | 1,000 | 12.292 (12.160–12.353) | 10.009 (9.839–10.146) | 18.57% | 16.819 → 14.668 | 12.79% |
| 12 Cortex, continents, 512×256 | 1,000 | 8.055 (7.985–8.102) | 7.021 (6.805–7.069) | 12.84% | 11.748 → 10.688 | 9.02% |

Simulation timing excludes startup/save loading and includes gradient-worker drain. Whole-process CPU time and all raw samples are retained in performance.json and timing/. Final timings waited for compiler activity to finish; detected overlapping samples are excluded and retried. Results are workload-specific and do not imply a universal speedup.
