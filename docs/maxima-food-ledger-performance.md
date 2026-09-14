# Maxima food-ledger performance

The ledger previously checked its residual summed-area cache by summing every
map cell on every query. Evaluation queries every possible site, so the cache
check alone cost O(A²) for map area A. Placement queries repeated the same scan.

Evaluation now prepares the summed-area table once and stores it in the result
snapshot. Bounds read at most four wrapped rectangles without validation scans,
allocation, or rebuilding. Table construction and the all-sites pass are O(A).
Claim allocation and exact harvesting walks are unchanged.

Owning the table with the result also corrects stale reuse when supply moves
while the total stays unchanged. The old pointer/dimensions/total key could not
detect that change. This can correct Maxima's placement choices in that case;
it does not change core movement, harvesting rules, or other AIs. The table is
transient, not serialized; no save-format or replay/network layout changes are
introduced. Result copies now carry their own table (about 2 MiB at 512²).
Callers must treat evaluated residual data as read-only until reevaluation.

## Reproduction

```sh
scons -j4 release=1 server=0 build/src/glob2 maxima-food-ledger-test
build/src/MaximaFoodLedgerStandaloneTest
build/src/MaximaFoodLedgerStandaloneTest --benchmark
```

The opt-in benchmark reports process CPU milliseconds for evaluation and a
separate full-map pass of footprint bounds. Deterministic digests check that
the benchmark produces the same results across revisions. Compare the same
driver compiled with the same optimization flags against each revision's
`AIMaximaFoodLedger.cpp` and header. Timing has no CI pass/fail threshold.

## Validation

The extended standalone suite passes on macOS, including an independent
wrapped-grid oracle, equal-total supply relocation, copies, alternating
results, resize and reset. The old implementation fails the new equal-total
relocation regression. AddressSanitizer and UndefinedBehaviorSanitizer pass.
The real Maxima placement integration test passes.

The broader placement runner also invokes an unrelated fertility timing test;
its existing 100 ms wall-time assertion failed at 313 ms on this busy host.
This is recorded separately from the ledger's correctness checks.

Full-match before/after measurements, retained evidence and cross-platform
checks are being collected before merge.
