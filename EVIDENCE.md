# PR 390 retained evidence

`gradient-evidence.tar.gz` contains the original timing rows, manifests, checkpoint
inputs, scheduler/integration logs and a complete-game checksum trace. It covers
the headless prototype before default enablement, including the selected eight-tick,
one-worker configuration. `pipeline-results.md` records the measurements and limits.
Original absolute paths identify fixture hashes; extract and rewrite manifest paths
for reproduction with the benchmark tools in PR 390.

`default-validation.tar.gz` contains macOS validation for default enablement, pending
queue persistence, rejection checks and reference-trace generation metadata. The
committed cross-platform reference fixtures are in PR 390. Full-engine TSAN was
blocked before main by SDL; scheduler TSAN passes. CI reports platform verification.

The original performance baseline is the same delayed schedule with zero workers.
The previous immediate schedule can change game duration, and is not counted as a
threading speedup. Timing ran on an Apple M3 alongside other simulation workloads.
