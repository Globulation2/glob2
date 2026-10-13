# Movement shadow fixes: evidence

Tested source: **3a3917757752fe64a2af9c1dc964c074794ebf3e**; base **0709322f3b140507f432aa820611c455314c91c2**. Linux x86-64 / GCC 15.2.0. Final commit updates generated CLI references only; engine source built at parent 675c8f823.

Three fixes remove duplicate movement capture, repeated map-view construction, and cache fragmentation by caller task state. 97 engine and 71 unit cases pass. Previous reference, updated reference and updated shadow replays, per-tick checksum files and final saves are byte-identical.

Ten rotated release pairs after warmup, 128-tick preliminary colony fixture: **65.56% less shadow engine elapsed time**, paired bootstrap 95% interval **64.11–66.88%**. The comparison also includes the integrated executor diagnostics master update, disabled by default. It is not a live movement speedup claim or gameplay acceptance.

Matching 16-tick Callgrind: captures **33 -> 17**, route fields **637 -> 64**, legality-to-view calls **82,208,713 -> 0**, route-field instructions **94.5% lower**. Across the full 128 ticks, fields decline **1,836 -> 1,086**, indicating later rebuilds remain significant.

**Stage A remains failed for all four delays across the corpus.** Owner admission/capture cost and K=8 coverage still fail unchanged gates. Fixtures remain preliminary. No live rule or Stage B/C adoption.

- [Detailed report](report.md)
- [Retest chart](retest-charts.png)
- [Download complete evidence archive](movement-fixes-evidence.zip): generated initial fixtures, raw samples, command files, source/binary hashes, gate output, correctness saves/replays/checksums, before/after Callgrind outputs and annotations, focused JUnit XML and build logs. Executables and personal preferences excluded.

Cross-platform verification, sustained workload qualification, 30 gameplay seeds, footage and full allocator-inclusive memory acceptance are unavailable/incomplete. No compatibility floor, save/replay/network format or SIM_REVISION change; reference movement stays authoritative.
