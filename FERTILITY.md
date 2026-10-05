# Production-entry classic land-field results

Per-case percentile bootstrap of eleven paired process-level log ratios; 10,000 draws. Warm observation per process is median of nine retained-workspace calls.

Unadjusted per-case/per-metric 95% intervals for these fixed inputs; not simultaneous intervals or population uncertainty. Nine inner warm calls are not treated as independent samples. Serial host drift and allocation effects are not corrected.

| Map / fixture | Warm ms, old → final | Warm paired change [95% CI] | Construction + first ms, old → final | First paired change [95% CI] |
| --- | ---: | ---: | ---: | ---: |
| 128² sparse | 0.480 → 0.508 | +5.1% [+4.4%, +6.0%] | 0.590 → 0.669 | +15.5% [+12.7%, +17.5%] |
| 128² striped | 1.248 → 1.261 | +2.6% [+1.0%, +31.1%] | 1.062 → 1.472 | +36.5% [+15.9%, +40.3%] |
| 128² dense | 10.848 → 6.070 | -47.0% [-47.9%, -45.7%] | 11.305 → 6.168 | -45.8% [-47.0%, -44.5%] |
| 256² sparse | 1.965 → 2.015 | +2.6% [-46.6%, +9.1%] | 2.190 → 2.578 | +19.2% [-2.0%, +24.8%] |
| 256² striped | 3.680 → 5.742 | +55.1% [+17.1%, +56.5%] | 4.048 → 6.421 | +60.0% [+19.2%, +64.1%] |
| 256² dense | 45.016 → 24.546 | -46.3% [-50.3%, -45.0%] | 46.331 → 25.321 | -45.7% [-47.1%, -43.9%] |
| 512² sparse | 15.060 → 8.704 | -42.1% [-47.6%, -39.7%] | 13.261 → 11.115 | -22.0% [-28.6%, +13.8%] |
| 512² striped | 20.064 → 27.409 | +38.2% [+31.3%, +56.3%] | 23.350 → 29.977 | +28.3% [+27.3%, +63.1%] |
| 512² dense | 258.680 → 94.101 | -61.4% [-64.6%, -50.9%] | 227.641 → 101.175 | -55.2% [-61.4%, -48.8%] |

Absolute times are separate variant medians; percentage estimates use matched pairs and therefore need not equal the ratio of displayed medians.

| Map / fixture | Field retained bytes, old → final | Actual input-plane bytes, old → final |
| --- | ---: | ---: |
| 128² sparse | 130192 → 65584 | 32768 → 65536 |
| 128² striped | 195728 → 65584 | 32768 → 65536 |
| 128² dense | 130192 → 65584 | 32768 → 65536 |
| 256² sparse | 391312 → 262192 | 131072 → 262144 |
| 256² striped | 653456 → 262192 | 131072 → 262144 |
| 256² dense | 391312 → 262192 | 131072 → 262144 |
| 512² sparse | 1306768 → 1048624 | 524288 → 1048576 |
| 512² striped | 2355344 → 1048624 | 524288 → 1048576 |
| 512² dense | 1306768 → 1048624 | 524288 → 1048576 |

Retained Field bytes exclude input planes, temporary peak allocations and allocator overhead. Original warm calls reuse owned scratch; final scratch is temporary and its allocation remains inside kernel timing.

Original934c calls binary-mask Field::rebuild; final37d calls Field::rebuildWeighted with prebuilt Q8 inputs. Plane preparation is outside both kernel timers and separately logged; its common-mask conversion/copy is not Map property collection. Entire GrowthCache construction, aquatic fields, habitat/local-growth work, and cache request frequency are excluded.

Earlier ecology optimization evidence used post-refactor42848408 as its baseline and prebuilt Q8 weighted calls on both sides. It measures a later optimization stage and cannot substitute for these pre-refactor-to-final results. The binary-adapter control timing was rejected by its host-load protocol and is excluded here.

Host observations passed: maximum load1 8.790, compiler count 1, sibling31 five-second busy 0.932%. This is observed coarse load bounding, not isolation.
