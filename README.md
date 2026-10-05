# Gradient preparation evidence for PR #806

Tested source: `b2850bb8bcee98cdc9d0b0898c522073bd0ebc61` in [PR #806](https://github.com/Globulation2/glob2/pull/806). The source branch integrates master through `71d7eee1b`; the newer fog-rendering-only `e444bebb0` was fetched and checked for overlap before final verification.

- [Performance report](performance-report.md): primary and separate confirmation tables, estimator, memory costs, conclusions and limits.
- [Environment and build provenance](environment.json): compiler, flags, dependency hashes and binary hashes.
- [Raw whole-game pairs](timing.jsonl), [preparation pairs](integrated-seed-pairs.jsonl), [paired summary](paired-summary.json), [confirmation summary](confirmation-summary.json).
- [Content-addressed evidence archive](gradient-preparation-evidence.tar.gz): fixture saves, per-tick traces, replay bytes, final saves, all final continuation comparisons, browser evidence, test logs, build logs, commands and benchmark/control sources. Rejected timing pairs are retained.

The control is the optimized direct kernel with cache dispatch and notifications disabled, linked against the same other objects. Reported gains are incremental cache gains, not a full PR-versus-master comparison. No production diagnostics or benchmark variants are included in the code PR.

Validation: 607 headless engine tests, 17 editor/touch/rendering cases, 102 complete continuation comparisons, 27 native/browser comparison cases, and 18 preparation scenarios. Source review findings were addressed before the tested commit. No simulation, save, replay or network version bump was needed.

The archive uses deduplicated blobs so identical outputs from worker configurations are retained once. To reconstruct named files and verify every SHA-256:

```sh
tar -xzf gradient-preparation-evidence.tar.gz
python3 unpack-evidence.py evidence restored
```

`restored/fixtures/` contains the nine generated inputs. The recorded commands retain their original checkout/dependency paths; substitute your checkout and local SDL prefix when reproducing. Check out the tested SHA and use the recorded compiler/flags/dependencies for comparable timing. `final-verification.json` maps every comparison to commands and expected hashes. `browser-compatibility/manifest.json` records native/Wasm hashes, fixtures and all browser results. `long-confirmation/` retains the separate longer pinned runs.

Native Windows, macOS, Android and ARM execution was not available. Browser checks ran on Linux. Dedicated market-heavy performance and manual gameplay feel checks were not run. Allocation-unavailable fallback was injected through internal cache state rather than exhausting the OS allocator. The performance report explicitly retains host-noise limits and the original adverse samples; it does not claim every game state is faster.
