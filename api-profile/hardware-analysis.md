# Hardware-counter trace

A separate Instruments 27 CPU Counters recording used the clean API binary on the 512²/four-player arena. The game completed all 16,384 ticks, exited successfully, and matched the baseline's final ticks, termination and team results. The trace ran for 90 seconds in guided **CPU Bottlenecks** mode, user-level counting, without high-frequency sampling.

This was a diagnostic trace, not a controlled performance comparison: a separate Glob2 app was still running during its early portion. It was stopped before all paired/phase measurements. Instrumentation also adds overhead. The trace must not be used to infer the API change's speedup or regression.

The exported tables contain 198,138,562,514 counted cycles and 101,593 bottleneck-triggered samples. The cycle-weighted means of Instruments' normalized fractions are:

| Instruments category | Cycle-weighted mean |
| --- | ---: |
| Instruction Delivery Bottleneck | 14.51% |
| Discarded Bottleneck | 27.99% |
| Instruction Processing Bottleneck | 21.11% |
| Useful | 36.39% |

These cover the **whole process**, not just building gradients. They describe the instrument's instruction-bandwidth classification, not literal percentages of elapsed time or direct cache-miss rates. The script verifies that each ratio is paired with the cycle count from its matching interval/core before weighting.

The selected samples include 6,068 stacks within building propagation, 4,821 within building initialization, and 1,413 within building search setup/scanning. Other gradient propagation accounts for 50,830 selected stacks. Samples are triggered by high bottleneck events and are **not uniform time samples**: these counts must not be turned into CPU-time percentages or used to compare stall categories with different sampling criteria. Inlining/unwinding also limits precise phase attribution. The direct phase timers are the basis of the remaining-cost table.

No direct L1/L2 miss counts or memory-bandwidth measurement were recorded in this configuration. Therefore this study does **not** establish that the solver is cache-optimal or memory-bandwidth-bound. The useful finding is the measured split between full-field preparation, scanning, and propagation; it does not justify replacing the bucket layout based on an assumed cache problem.

`hardware-command.json`, `counter-options.json`, `hardware-status.json`, `hardware.log`, `hardware-analysis.json`, `analyze_hardware.py`, and the compressed table export retain the command, configuration, completion, data and calculation. The full 1.4 GiB native trace remains local at `artifacts/lazy-gradient-api/cpu-counters.trace`; it is omitted from the evidence branch because it includes broader system metadata. The published export contains the launched process's relevant tables. `analyze_hardware.py` consumes the uncompressed `hardware-data.xml`.
