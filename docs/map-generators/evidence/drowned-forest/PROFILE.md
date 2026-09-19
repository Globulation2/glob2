# Final generation profiling

CPU seconds from child process resource accounting; seed 7, default controls. Shared-host wall times are retained in timings.json but are not compared as CPU performance.

| Size / colonies | Original CPU | Final CPU | Reduction | Forts final CPU |
| --- | ---: | ---: | ---: | ---: |
| 128 / 2 | 0.429 | 0.502 | -17.0% | 0.054 |
| 256 / 4 | 1.318 | 1.097 | 16.8% | 0.153 |
| 512 / 8 | 15.529 | 10.003 | 35.6% | 0.902 |

Sampling identified repeated full-map flood searches and building-room checks as hotspots. Fixes use bounded room grids with an exact global fallback, direct footprint enumeration, bounded shortcut searches, lazy reverse-mouth searches, and a distance-60 exit search. Redundant final validation was removed from generation because GenerationService performs it. No search bound or gameplay contract was weakened.

The largest case still costs about ten CPU seconds; bounded search on difficult compact or crowded settings can cost substantially more. Forts remains much faster. These are single-seed process measurements, not a broad latency guarantee. Telemetry-on measurements and exact commands are retained alongside this report.

The final exit cutoff is an exact-output optimization, with independent review and four before/after byte comparisons. The independent local-room comparison covered 800 cases and supplied a blocked-root regression guard.
