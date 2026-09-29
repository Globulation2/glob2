# Maxima coordinate wrapping: review evidence

This evidence accompanies the two-file optimization on `codex/nongradient-profile`.
The source revisions are recorded in `revisions.json`: baseline `508942f082ed22e699792cb82b95a5feca65385d`, candidate `ef1f90ed1f0b0ed9557d90b3a75aef5e2531631a`.
It is deliberately separate from the production branch.

## Final measurements

See `performance.md` for medians, ranges and process timings, and `timing/results.json` for every sample, including discarded warmups. Individual directories retain commands, outputs and result JSON. Metadata records compiler, hardware and binary hashes.

Both versions use release builds (`scons release=1 server=0`). Measurements use the current master defaults: one periodic-gradient worker, eight-tick publication delay. Therig runs are pinned to two separate physical cores (logical CPUs 8 and 10). Each workload advances exactly 1,000 ticks from its retained save. One warmup per binary is discarded; three measured runs per binary alternate pair order. No profiling or checksum capture is enabled during timing. `run_s` is engine simulation elapsed time including worker drain, excluding setup; wall time includes process startup and save loading. CPU time includes the simulation and worker threads.

These are small paired samples, not confidence intervals or a claim of universal speedup. The no-Maxima workload is a noise control. Saved games contain their initial state, seed, AI configuration and random state; `scenarios.json` provides readable seeds and rosters, while `cases.json` records input hashes and tick boundaries.

## Correctness

Run `python3 check_evidence.py` to check bundled input/reference hashes and all equivalence manifests, and `python3 summarize.py` to regenerate the performance table.

The `validation/` manifests record full per-tick trace and AI order-stream hashes. Both Linux machines’ baseline/candidate binaries and the macOS ARM64 candidate agree across 11 large-game states (256 ticks each) plus a version-115 historical save (512 ticks). The Linux reference directories include compressed traces and extracted replay orders; other variants retain their hashes and result JSON to avoid duplicating identical traces. Three Maxima regression suites pass on macOS and native Linux: placement, implementation integration and lifecycle. Logs are included.

`continuation/results.json` compares baseline and candidate after loading new checkpoints. All four baseline/candidate resumed traces match for 128 ticks. Islands, even ground and late even ground also match uninterrupted execution. For the mixed continents game, both versions reproduce an existing uninterrupted-versus-reloaded mismatch at tick 12,171. It is not introduced by this patch. Replay headers are not used for equivalence because repeated baseline runs can serialize different header bytes; recorded orders and per-tick simulation state are compared instead.

The optimization changes neither save fields nor version gates. Windows and interactive play were not tested. No AI policy, pacing or gameplay change is intended.

## Reproduce

Check out and build each source revision in separate source directories (or save each release executable before rebuilding). Keep the game data from the matching checkout available. Set `E` to this evidence directory and `ROOT` to a source checkout. Use absolute binary paths.

```sh
python3 "$E/run.py" timing --baseline /path/to/baseline --candidate /path/to/candidate \
  --root "$ROOT" --output /path/to/new-timing-directory --cpus 8,10 --repeats 3
python3 "$E/run.py" verify --baseline /path/to/baseline --candidate /path/to/candidate \
  --root "$ROOT" --output /path/to/new-verification-directory --jobs 3
python3 "$E/continuation.py" --root "$ROOT" --verification /path/to/new-verification-directory \
  --baseline /path/to/baseline --candidate /path/to/candidate --output /path/to/new-continuation-directory
python3 test/maxima/run_maxima_implementation_regressions.py \
  --build-dir build/linux/client/release --reuse-built-objects \
  --test MaximaPlacementStandaloneTest --test MaximaImplementationIntegrationTest --test MaximaLifecycleTest
```

For macOS verification omit `--baseline` if comparing its candidate hashes against Linux baseline/candidate results; use `build/darwin/client/release` for regression objects. Omit `--cpus` on macOS. Output directories must be new. Recorded `command.json` paths identify the original runs; reproduction uses the bundled inputs, not those machine-specific paths.

## Profiling origin

The `profiling/` directory contains three flat perf reports and their aggregate from the initial investigation on older master `de84ffc189659f249b6cce91dd3335baa6f2b5c5`. Summing sample periods across the three games attributed 10.07% to Maxima placement's normalizeX/normalizeY (8.99% with equal weight per game). These are discovery profiles, not profiles of the final master revision. The final performance claims come exclusively from `timing/` on the revisions above. The aggregate contains per-profile symbol counts, allowing the merge to be checked without retaining bulky raw perf recordings. `merge_profiles.py` documents the original perf-report/aggregation procedure.

An experimental fruit-collection optimization was rejected and is absent from the production diff. A separate baseline Nicowar crash excluded that roster from the study; no crashing runs are represented as performance results.
