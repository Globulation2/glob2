# Terrain rollout verification

Tested commit `211b42e6cd97567e3c87cdbb0ed2f1ae3166388b` (identical verified precommit source fingerprints; builds/tests preceded the commit).

Worktree based on `ddfa81a405e0c47442e1787ccdf7d4e95ca57d6e`; fetched master `ddfa81a405e0c47442e1787ccdf7d4e95ca57d6e`. The source files and catalog fingerprints are in `tested-inputs.json`; all source PNG hashes are in each material's provenance.

## Result

All 29 catalog materials use 8×8 tile blocks and four independently seeded interiors with shared boundary geometry. The catalog references 16,640 native frames and matching HD frames. The 25 static recipes reproduced exactly on Linux x86-64: 6,400 native + 6,400 HD hashes. Animated assets were generated in that same pinned Linux environment; their complete committed frames, loops, source hashes and boundary strips were validated. The maintained production guide and generated runtime provenance inventory are updated.

## Environment

- macOS arm64, Apple LLVM21, local SDL renderer and programmable GL.
- Linux x86-64/glibc2.41 generation in Docker image `glob2-terrain-eight:local`; Python3.14, Pillow12.2.0, NumPy2.4.6, SciPy1.17.1.
- Native fast development client + engine/unit test builds; release diagnostic is recorded separately when available. Fast build timings do not establish release performance.

## Checks

- `terrain-complete.xml`: 38 cases, 0 failures.
- `terrain-gpu-final.xml`: 1 cases, 0 failures.
- `streaming-final-sheets.xml`: 16 cases, 0 failures.
- `validate-all.log`: all29 materials, every native/HD phase, registered hashes, feature/style constraints, shared layout strips, distinct interiors and slope-aware seam checks pass.
- `static-reproduction-summary.json`: all25 static recipes reproduce exactly. Two read-only partitions were used; the head was intentionally stopped after its13 assigned materials. Source digest changes during import were comments/default worker-count changes, with no changed rendering math. No mismatched pixels.
- `installed-contracts.log` and corrected seam regression logs: installed-frame synthesis contracts, broad-wave acceptance and injected boundary-stripe rejection. This installed-frame run is separate from fresh static synthesis.
- Focused catalog schema tests:10 passed, including UTF-8 block-group limits. Full atlas rebuild/check cases were not repeated; native runtime asset export compiled the complete catalog successfully.
- `provenance-inventory.log`: regenerated strict runtime inventory, then `runtime_provenance.py --check` passed for every source/output hash.
- Normal release (`-O3`, `dev_fast=0`, no PCH/unity) unit build passed. `release-streaming-functional.xml`:3/3 passed. `release-cadence.xml` and its `water-hd-cadence.txt`: all256 current HD frames ready throughout the measured second loop; at most512 requested HD frames resident. This is an isolated loading-cadence diagnostic, not a game-FPS measurement.
- `client-final-build.log` + `client-smoke.log`: native client/engine build passed and client `--help` exited successfully using an isolated user profile.

## Runtime behavior checked

The actual4096-frame water bank starts with native images and zero eager HD frames. Requested source windows retain overlapping frames, eventually decode all512 requested endpoint frames, evict to zero, and preserve native fallback. Source images stay CPU-backed until directly drawn; source mip chains and unused atlases are not prepared. Direct native/HD drawing remains valid on SDL and programmable GL. CPU terrain jobs snapshot only their group recipes and keep the4MiB reservation limit; pages and retained snapshots survive eviction, scrolling, failures, edits and animation changes.

Timed cadence probes are opt-in diagnostics, with no hardware-dependent CI readiness threshold. They record bounded residency and progress; no game-FPS or release-readiness claim is inferred from fast-build measurements.

## Shipping compression check

`release-hd-seams.json` checks decoded optimized WebP output over all29 materials/65phases:4,160 cell boundaries and2,080 mixed-layout joins. All mixed-layout joins pass. Six very dark void-hole cell boundaries marginally exceed the conservative PNG slope threshold after Q90 compression (maximum excess0.0804 luma levels out of255; boundary mean below0.85). All other compressed terrain boundaries pass. This sub-level codec noise is recorded rather than misreported as exact pixel continuity; source PNG borders remain exact. No wholesale grid/framing pattern was observed in the gallery.

## Coverage limits

No simulation, map rules, saves or replay formats changed. Windows, browser, Android and iOS builds were not run. No full-game release performance measurement was performed. The current user editor/game session was not restarted or modified.

## Review evidence

`gallery.png` shows four different block interiors joined in a2×2 arrangement for each of all29 materials. Individual `<name>-mixed-blocks.png` previews are beside it. Native GPU composition comparison artifacts live under `artifacts/tests/TerrainPresentation/`.

## Commands

```
PYTHONPATH="$PWD" python3 tools/dev_build.py build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2 engine-tests
PYTHONPATH="$PWD" python3 tools/dev_build.py unit-tests
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'TerrainMaterials/*' --display-jobs 2 --timeout 180
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter '*GPU terrain pages match*' --timeout 300
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'SpriteSheets/*' --display-jobs 1
artifacts/terrain/venv/bin/python tools/artwork/validate_material.py --all
artifacts/terrain/venv/bin/python tools/artwork/runtime_provenance.py --check
```

## Published review files

- [client-final-build.log](client-final-build.log)
- [client-smoke.log](client-smoke.log)
- [debug-water-hd-cadence.txt](debug-water-hd-cadence.txt)
- [gallery.png](gallery.png)
- [installed-contracts.log](installed-contracts.log)
- [installed-seam-regression.log](installed-seam-regression.log)
- [provenance-inventory.log](provenance-inventory.log)
- [release-cadence.log](release-cadence.log)
- [release-cadence.xml](release-cadence.xml)
- [release-hd-seams.json](release-hd-seams.json)
- [release-streaming-functional.log](release-streaming-functional.log)
- [release-streaming-functional.xml](release-streaming-functional.xml)
- [release-unit-build.log](release-unit-build.log)
- [release-water-hd-cadence.txt](release-water-hd-cadence.txt)
- [static-check-results.json](static-check-results.json)
- [static-check-tail-results.json](static-check-tail-results.json)
- [static-reproduction-summary.json](static-reproduction-summary.json)
- [streaming-final-cadence.xml](streaming-final-cadence.xml)
- [streaming-final-sheets.log](streaming-final-sheets.log)
- [streaming-final-sheets.xml](streaming-final-sheets.xml)
- [terrain-complete.log](terrain-complete.log)
- [terrain-complete.xml](terrain-complete.xml)
- [terrain-gpu-final.log](terrain-gpu-final.log)
- [terrain-gpu-final.xml](terrain-gpu-final.xml)
- [tested-inputs.json](tested-inputs.json)
- [validate-all.log](validate-all.log)
- [validator-contracts.log](validator-contracts.log)
