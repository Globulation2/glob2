# Publication-wait diagnostic on PR1194 head403dfd099

One fixed-seed20,000-tick Playground128×128 run, eight Nicowars with no legacy observer, unrestricted auto32, CPU backend, release binary with compute telemetry set to `waits`. This is a separate instrumented diagnostic, excluded from ordinary benchmark results.

| Awaited producer | Required background wait | Share |
| --- | ---: | ---: |
| AI |192.680ms|99.519%|
| Other (includes hiring/statistics/untagged work)|0.932ms|0.481%|
| Periodic/cold gradient|0|0%|
| Building gradient|0|0%|
| Resource growth|0|0%|

Total193.611ms across142of20,000ticks, versus8.042s native run elapsed time (about2.41%). Maximum affected-tick wait6.056ms. Teardown/save/verification are excluded; building-gradient verification separately recorded0.293ms.

Attribution names the batch awaited, not the root cause: its work may be queued behind other producers. Wait duration includes wake/scheduling/mutex reacquisition and is not worker CPU time. Logical waiting for a field's fixed publication tick is a separate gameplay latency; it is not counted when the owner need not block.

This opening-to20,000-tick single-seed sample does not establish mature-game or whole-game shares. Existing tags combine cold field families under periodic_gradient, and hiring/statistics under Other. No per-family or per-AI-controller root-cause ranking is established. `waits` lacks callback execution and eligible-queue decomposition.

Raw commands, environment, revision/binary hash, native JSON and extracted totals are retained. This diagnostic does not replace or alter the32-game uninstrumented timing results.
