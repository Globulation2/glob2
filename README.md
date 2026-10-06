# Completed-tick observation phase verification

Production PR: https://github.com/Globulation2/glob2/pull/851
Tested commit: `325357ec2a5d72e8cca32beccf5ee2793d054abb`
Base: `7ec6fc1cc` (master fetched before final verification; no newer base changes).

Both independent subagent architecture/compatibility reviews found no remaining blockers. See reviews.md. No maintainer interactive playtest was performed. Moving periodic preparation after all world mutations can change routes and AI trajectories; version139/replay floor139/SIM21 gate that change. Save floor58 and wire layout/protocol58 remain unchanged.

## Verification

macOS ARM64 / Apple M3 / Apple Clang21 and Linux x86_64 / Ryzen7 6800H / GCC13.3 each completed the final focused registry run: 240 runner checks passed, zero failed, five display cases skipped. JUnit contains328 selected cases (the unit subset runs as one process). Exact commands are in linux_validate.sh and build logs; filters cover gradients, executor/phase, lifecycle, saves, replay/version gates, script/JS simulation, custom terrain, markets, guard crowding, live multiplayer stalls/pause/reload, legacy AI and continuation.

Build: `scons -j8 release=1 server=0 tests`, then client build with the same flags. A local one-line SConstruct bypass omitted runtime asset transcoding for these headless builds. It is absent from the production commit. build.py records this reversible bypass. Display tests fail without prepared sprite frames and are excluded from final relevant coverage; their exploratory failure logs are retained locally. Full graphics/UI/assets, Windows, Android and WebAssembly were not verified. SIMD portability beyond the two native architectures remains unverified. No thread sanitizer was run.

Linux uses existing SDL3 and recording prefixes named in linux_validate.sh and linux-machine.txt; macOS uses native Homebrew SDL3 dependencies. Dependency versions, compiler flags, binary hashes and source hashes are retained. The Linux lab is isolated from the user's original dirty checkout; its git snapshot differs in identity, but every production/test source file matches the committed source-hashes.json exactly.

Four retained saved checkpoints (game seed19; generated map seed1001, generator15; Oazis saved map) run1024 advanced ticks under four execution controls: compute threads1 or controller default2/4; gradient workers0 or2; AI compute disabled. Every per-tick checksum, replay byte and decompressed final save matches across all controls and both platforms. checks.json retains hashes and results. Representative inputs, sidecars, replays and final saves allow replay/reproduction; final-save bytes are identical across controls. A disk-full Oazis trace on Linux was discarded and rerun after compressing completed artifacts; it is not a verification result.

`python3 test/check_gradient_pipeline.py BINARY --output OUTPUT` passes on both platforms: workers0/1/2/4/8, delays1/3/8, every eight-tick pending-save offset resumed under0/1/2 workers, default settings and invalid-option rejection. Generated fixtures and pending-state continuation are included. Focused artifact archives omit bulky legacy AI continuation dumps; their pass logs/JUnit and committed source fixtures are retained. Full local archives are retained separately. A save written by the unmodified138 binary loads and advances128 ticks under139; older released saves also pass the selected compatibility fixtures.

Golden record/trace regenerated and verified. `python3 test/check_sim_revision.py --base origin/master` passes. Current/future replay acceptance and explicit137/138 rejection pass. LAN/online simVersion gates were audited and exercised by selected harnesses. The cheap hosted contracts pass; expensive hosted checks were not requested and their skips do not establish engine verification.

## Fresh final-production performance

Ten alternating master/candidate pairs per checkpoint,1024 advanced ticks per pair, one discarded full-window warmup pair. Run-only elapsed and process CPU from result.json. Two gradient workers, eight-tick delay, unchanged compute cap; no telemetry, replay or saves in timed region. timing.py and compressed raw rows retain commands/results. In-use workstations are noisy; the first small-Castor outlier is retained, with no hand-picked exclusions.

| Checkpoint | Paired median time reduction | Paired median CPU change |
|---|---:|---:|
| Small Castor64×64,2 AI |8.70%|-2.45%|
| Land Maxima256×256,4 AI |1.89%|+0.10%|
| Mixed512×512,4 AI |2.53%|+0.41%|
| Oazis256×256,11 AI |1.01%|+0.17%|

The baseline binary is the retained unmodified407c58c6b engine; the fetched7ec6fc1cc base differs only in32 localization catalogs, with unchanged engine/build code. Binary SHA is verified against the earlier baseline manifest. Candidate uses final production sources. See manifest.json for input and binary hashes.

This is modest, workload-specific evidence, not a universal speed guarantee. Earlier prototype results are retained separately: Linux showed mixed performance, including a small Maxima regression. Final Linux performance was not remeasured; final Linux correctness was verified. Increasing the compute cap is deliberately outside this change. Architecture/ownership barriers, rather than a claimed large speedup, are the main result.

## Reproduction

In a production checkout at the tested commit, build the release client with the dependencies/flags above. Copy verify.py into `artifacts/read-phase/verify.py`. Copy each `samples/NAME/input.game.gz` into `artifacts/critical-path-prep/NAME-fixture/final.game.gz` for the four checkpoint names above. Then run:

```sh
python3 artifacts/read-phase/verify.py build/darwin/client/release/src/glob2 artifacts/read-phase/reproduction
python3 test/check_gradient_pipeline.py build/darwin/client/release/src/glob2 --output artifacts/read-phase/reproduction-pipeline
```

On Linux use `build/linux/client/release/src/glob2` with the recorded runtime library paths. The verifier writes/compresses each trace and rejects any control mismatch. Compare its hashes with macos/checks.json or linux/checks.json. Paths in command records reflect the original isolated labs; substitute the matching retained input/output locations when reproducing. For performance, reconstruct the baseline407c58c6b and candidate release binaries as `artifacts/read-phase/glob2-master` and `glob2-candidate`, then run timing.py from that directory. Run it alone; correctness jobs and archive compression must not overlap timing.
