# Movement planning: deeper profiling

Analyzes unchanged production revision **3a3917757752fe64a2af9c1dc964c074794ebf3e**, base 0709322f3, on Linux x86-64 with GCC 15.2 and Valgrind 3.26.0. One preliminary 1,024-worker colony, seed 713, K=1, four participants.

Two isolated optimization candidates now have direct evidence:

- Ordered owner traversal reduces boundary instructions **43.77%** in the late 64-tick profile, retaining deterministic scheduling.
- Sharing resource fields by effective goals removes **242 duplicate builds**: **1,086 -> 844 fields**, **16.48 -> 12.82 million expansions** over 128 ticks.
- All **7,676 suggestion outputs** match the control for each candidate and both together; final checksums match.

Most later field invalidations follow real changes: harvesting removes ground-blocking wheat. The quantity-only invalidation hypothesis does not explain this input. The owner still scans 65,536 records for 3,154 requests in ticks 64–127 and extracts full 384-byte unit views.

[Detailed report](report.md) · [Charts](deep-findings.png) · [Evidence archive](deep-analysis-evidence.zip).

The archive retains diagnostic source copies, build/link commands, fixture, source-line Callgrind profiles/annotations, causal traces, exact output comparisons and summary JSON. No production code was changed by this investigation. These single-input experiments need lifecycle/slot-reuse, market/supplier/depletion, all-delay/participant and platform tests before promotion. Compiler contention invalidates new native wall-time inference. No new runtime speedup, gate pass, memory acceptance or live-adoption claim.
