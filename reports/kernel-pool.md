# Fresh kernel investigation and integrated result

The integrated six-candidate pool retains 16×16 output cores and one Jacobi fallback. Physical workgroups are independent of output geometry. No production dependency was added.

| Candidate | Algorithm | Local sweeps | Threads | Halo |
| --- | --- | ---: | ---: | --- |
| 0 | Jacobi | 4 | 256 | Expanding |
| 1 | Four-color | 2 | 128 | Expanding |
| 2 | Four-color | 4 | 128 | Expanding |
| 3 | Four-color | 8 | 256 | Expanding |
| 4 | Four-color | 8 | 128 | Frozen one-cell border |
| 5 | Four-color | 16 | 64 | Frozen one-cell border |

The work began by modeling the current kernel independently. Its colored step-2/step-4 patches assign only 100/144 parity cells to 256 work items, and every group pays eight final reduction barriers. The experiments varied physical thread count independently of the core, factored the candidate acceptance bound, factored uniform cardinal/diagonal maxima, replaced the final reduction, padded local memory to remove boundary predicates, and tried larger rectangular cores. Frozen-halo candidates from the independent algorithm investigation combined particularly well with these instruction and workgroup changes. Wider cores and padded local storage were omitted from production because the combined compact pool achieved stronger results with less source complexity.

Each cell now maximizes signed neighbor candidates before testing the shared acceptance threshold once. The final changed reduction uses eight partial reducers and a single serial reducer after their barrier. Frozen-halo variants update only the core, so their inner neighbor bounds are guaranteed by the one-cell border. They test for a local fixed point after each sweep. The current expanding-halo variants retain their first-sweep fixed-point check.

The host dispatches `local={threads,1,1}` and `global={tileColumns*threads,tileRows,fields}`. Device-axis and kernel-workgroup checks use physical threads. Unsupported candidates are skipped internally; Jacobi4 is preferred as the initial candidate, with the first supported candidate as fallback. The convergence guard counts 65,536 global dispatches independently of local sweep count. Field-change flags clear only immediately before the eighth dispatch, whose flags the host reads; tile masks still clear every dispatch.

## Timing

All reported kernel comparisons used the RTX 2070 SUPER, native C++ `-O3` queue submission, no profiling-enabled queue or requested events, CPU31 affinity, randomized interleaved repeats, and `/tmp/glob2-gpu-optimization-gpu1.lock`. They include seed/descriptor upload, mask fills, launches, convergence readbacks, field retirement, descriptor updates, and final result readback. Cost buffers were resident. Every result was checked exactly against the independent CPU output. Correctness-only measurements during builds were discarded as performance evidence.

`compact-results.json` compares all six original configurations with all six compact candidates, each with unchanged flag clearing and with clearing only before the host check. Fifteen repetitions were interleaved; the first warmup was excluded. The 46 unique changing captures were separated from 88 already-fixed captures.

| Changing single fields | Kernel-only geometric speedup | Kernel + flag-clear geometric speedup | First-field-selected aggregate, combined |
| --- | ---: | ---: | ---: |
| 25 × 256² | 1.182× | 1.272× | 1.293× |
| 21 × 512² | 1.310× | 1.383× | 1.351× |

The first-field-selected kernel-only pool had one 3.2% regression, the later 256² Clear capture `field-14224.bin`; including the host flag-clear change still improved it by 9.2%. This avoids claiming an oracle-selected best variant is always available in production.

The final quiet repeat (`quiet-results.json`) tested the unified production kernel against every original configuration, 21 interleaved repetitions over nine changing singles, two eight-field batches and seven synthetic controls. Whole-match work was paused for this window. All 6,804 runs were exact. Best-pool kernel speedups were 1.036–1.502× for singles, 1.430/1.699× for the batches, and 1.045–1.152× for synthetic controls. Combined speedups were 1.112–1.552×, 1.465/1.616×, and 1.089–1.163× respectively. Flag clearing alone is noisy or neutral on the large batch; the strongest repeat's large batch medians were 3.250 ms with per-dispatch clearing and 3.416 ms with last-dispatch clearing, versus 5.521 ms for the best original kernel. The earlier broad run measured 3.277/3.291 ms for that pair. Whole-game performance is a separate parent investigation.

## Exactness and failure handling

The final native integrated-host check uses the latest production C++ source, including the parent's independently approved omission of the final retired-descriptor upload. Only device enumeration changes to isolate GPU1. It ran every one of the six supported candidates through 134 unique captured fields, 160 extreme-cost/cap/deferred-seed corner fields, and the long alternating-tile guard fixture: 1,770 exact field comparisons, 222 native batches, 28,200 dispatches and 3,525 host checks. Input captures were independently checked with heap Dijkstra before the GPU run.

The 32×32768 alternating-tile corridor needs 9,360 frozen-halo global exchanges. This exceeds the old step-8 guard of 8,192 dispatches and is an explicit regression fixture for the new global bound. It passed all six candidates.

All seven injected native API failures passed: queue creation, initial dispatch, later batch group, calibration, tuning, CPU mismatch, and concurrent lanes. The original concurrent-lane injector relied only on a starting barrier and could allow a healthy lane to finish before the failing thread resumed on one CPU. Its initial failure is retained. The deterministic injector now waits for the production catch path to publish shared failure before releasing peer lanes, proving that already-known device failure prevents publication and preserves original seeds for CPU recovery.

Independent source review and CPU modeling by the correctness agent is recorded separately under its artifact directory. No cross-vendor or cross-OS GPU hardware verification was performed in this kernel investigation.

## Evidence map

- `baseline.cl`, `manifest.json`: source and captured-input provenance; commands and dependencies.
- `screen-results.json`, `final-results.json`: instruction/layout/geometry screening and broad finalist comparison.
- `cross-results.json`: frozen-halo and microkernel combinations.
- `compact-results.json`, `compact-summary.json`: broad bounded-pool timings and first-field selection behavior.
- `quiet-results.json`, `quiet-summary.json`: final quiet unified-source repeat.
- `compact-measured.cl`: reviewed and measured kernel, SHA256 `13fda814d8088f0b919c0428116cf4244aa1e1407f5bdad8387f53b770e35494`.
- `compact.cl`: integrated kernel, SHA256 `a8518f7f7fb8991faa70669e515b1b59a778cd60275210e1a71b5a3e8608e955`; only one whitespace-only blank line differs.
- `actual-driver-sass/`, `assembly-unified-results.json`: actual NVIDIA driver cubins, disassembly, register liveness and resources. All candidates have zero spills/stack; frozen variants use 2,464/2,208 shared bytes versus expanding step-8's 7,176 bytes.
- `OpenCLGradient.before.cpp`, `integration.diff`: pre-integration snapshot and kernel/host changes before the parent's final-upload change.
- `runtime-source.cpp`, `native-integrated.cpp`, `runtime-failures.cpp`, `integrated.py`, `integrated-results.json`, `integrated.log`: final native exactness and seven failure tests. Tested production C++ SHA256 `b5dffadcbd7591db30b092c81a27e40391cd667f6671b62144b40fc8de020b31`.

These files are ignored review evidence, not committed documentation. Durable behavior is documented in `docs/development/reference.md`.
