# Initial lean12 prospective comparison

Controlled reference97bc and candidate2d4 use the same master components, pinned ordinary-release binaries, shared generated initial states, probe-disabled ABBA timing and separate diagnostic windows. See summary binding and exact artifact-only runner.

Three smaller setups are near CPU parity. The512² 8v8 result is severely confounded by concurrent fleet admission and shared physical-core/cache/memory pressure: reference repeats have identical final checksum434937025 yet measured CPU rises191→298seconds; candidate repeats have identical final checksum1798113084 yet CPU falls451→343seconds. CPU2 shares its physical core with CPU18, which was not reserved. Load rose from about2 to30. Raw candidate timings are higher, but these observations cannot isolate a causal policy regression or saving; no stable promotion follows. A repeat with physical-core reservation and hardware counters is being prepared alongside collection.

Diagnostic repeats are separate arm trajectories. Three maps yielded no movement-direction labels in the chosen windows. The512 map has772 reference queries/72 extra-cost versus180 candidate queries/36 extra-cost, so neither raw totals nor flagged-count reductions establish improved direction quality. Stalls and planning outcomes are separate in summary; no full-game extrapolation or statistical confidence claim is made.
