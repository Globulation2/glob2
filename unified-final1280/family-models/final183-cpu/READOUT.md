# Final production evaluator CPU panel

The final ordinary release executable uses the selected four family experts and
combined fallback at threshold16 with a demanded-field age cap of1024. This panel
measures the final common evaluator, rather than reusing prototype timings.

| Arm | CPU ms/measured tick | Sim CPU s | Process CPU s | Wall s | Measured ticks | Gross ticks/s | Measured ticks/s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| master_reference | 0.148469 | 9.730071 | 12.243968 | 12.760646 | 65536 | 5296.3 | 5135.8 |
| shared12_cap_reference | 0.124618 | 8.166948 | 10.644274 | 11.221448 | 65536 | 6022.8 | 5840.2 |
| final183_experts | 0.125534 | 8.227003 | 10.723254 | 11.926450 | 65536 | 5666.7 | 5495.0 |

Each arm advanced67,584 ticks across eight processes, including256settling ticks
per process;65,536ticks contributed to the simulation CPU metric. Four
counterbalanced repeats used each of the same two saved worlds.

Final versus master: -15.45% simulation CPU/tick; paired-repeat spread -16.58% to -12.30%. Advanced wall throughput +6.99%; total process CPU -12.42%.

Final versus capped shared12 reference: +0.74% CPU/tick, paired spread +0.03% to +2.33%.

The complete PR is compared with master; this does not isolate model causality.
The shared reference also differs in cleanup and evaluator implementation. These
are two previously examined worlds and four repeats, not a population confidence
interval. Background process, load, thermal and power observations are retained in
each command/measurement; no uncontended physical-core claim is made. Every
repeated arm/world pair ended with identical checksums. No probe, work statistics,
checksum tracing or fixed-interval research override was enabled in timed runs.

PLAN.json records exact source/binary/input identities and commands. STATUS.json
contains every process measurement, and SUMMARY.json retains paired spreads.
Quality findings remain in the separate observer-on panels; timings do not erase
mixed reachability or direction-cost results.
