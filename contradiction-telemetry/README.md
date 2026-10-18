# Contradiction telemetry verification

Source: `79de5484410beee884f85ba293bb44d38f226c55`.
Integration base: `3d896cbb95293da6d24caddd247a62c9ca99d77f`.

The optional observer records six movement-query counters, field-capture snapshots, separate future stall outcomes and non-overlapping live totals. Production coefficients, scheduling, save format and simulation revision are unchanged. The paired research comparison cannot emit augmented models into production.

## Python validation

Linux x86_64 (therig), Python 3.14.4, isolated pinned environment in `training-environment.lock`. No runtime dependencies were added. All 53 tests passed, including baseline integer C++/Python parity with undefined-behavior checks, dataset/cohort integrity, exact-row grouped comparisons, unknown-capture/format coverage, immutable distributed export and separate stall/planning outcomes. The source hash manifest matches the committed files.

```sh
python -m unittest -v tools.test_resource_gradient_model tools.test_resource_gradient_dataset tools.test_resource_gradient_contradictions tools.test_resource_gradient_experiments tools.test_resource_gradient_distributed tools.test_resource_gradient_tournament
```

The two independent reviews addressed a format disappearing after eligibility filtering and instrumentation arguments changing hot production calls. Final C++ uses the original production interfaces with separate observed/classified template specializations.

## Pending qualification

Native release build, focused engine tests, per-tick old/new and probe-off/on parity, save continuation, disabled/enabled CPU overhead and selector microbenchmark remain pending. These Python results do not establish engine behavior, performance or cross-platform parity. A new remote-only 128-game collection must wait for that qualification. The superseded campaign is draining separately; no old rows are relabelled or imputed for augmented fitting. No model improvement or production CPU saving is claimed.
