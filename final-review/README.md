# PR1197 final local verification

Tested head: `85d876266428fcb9a5fa7628b85bd13cf3b52b44`.
Integrated master: `ba3ff8d021d32de37d8b174b4dd5254b9998e1fd`.
Linux x86_64, kernel7.0.0-31-generic; GCC13.4.0; Node22.22.1. Pinned SDL3 prefix and actual linked-library hashes are in identity.json, build provenance and runtime-libraries.txt.gz. No runtime dependencies added.

Missing building fields initialize one tick after admission; existing fields refresh at the match delay (default8). FIFO waiting still precedes admission. The feature includes pending ground steering, primary-hire lazy preparation, historical save-layout handling, format175, SIM_REVISION77 and replay floor175. No experiment switches, observer logs or dated reports are in the PR branch.

## Results

- Final full release client and engine-tests build: exit0.
- Broad engine quick suite: **1583 passed,0 failed,197 skipped** (slow/display),1780 selected cases.
- Focused compatibility run without quick: **93 passed,0 failed,0 skipped**, including deadline ordering, executor capacity, worker counts, pending steering, hiring, saves, replay and hostile-order network-sim cases. This overlaps the broad suite; counts are not independent tests.
- Protocol: **8 files,39 tests passed**. Generated schema fixtures reproduced from the committed protocol source.
- Authentic published PR174 initial and tick7000 saves load and execute. One/eight total participants produce identical per-tick deep world traces for each256-tick interval.
- Format175 save/resume matches uninterrupted canonical snapshot checksums for every overlapping tick256 through512. Deep world checksums include the loaded MapHeader format174 versus175, so they differ by the documented constant metadata XOR131072; this is not treated as a simulation difference. The first naive raw-world comparison failure and its script are retained.
- Golden changes were regenerated only after checking that remaining movement, arrival, continuation and invariant assertions passed. Original pre-fix broad run:1554passed27failed197skipped. Final run resolves all27, plus2new historical-writer regression cases.
- Independent sub-agent reviewed full production diff and the final fixture/audit fixes; no remaining blocker found. This is automated independent review, not human playtesting.

## Commands

```sh
GLOB2_SDL3_PREFIX=/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix taskset -c 1-15 scons -j12 release=1 server=0 client_profile=full optimized_assets=0 CXX=g++-13 CC=gcc-13 engine-tests build/linux/client/release/client-full-direct-1111/src/glob2
python3 test/run_tests.py --build-dir build/linux/client/release/client-full-direct-1111 --binary engine --no-display --quick -j8 --junit artifacts/initial-delay-split/quick-final.xml --artifacts artifacts/initial-delay-split/quick-final
python3 test/run_tests.py --build-dir build/linux/client/release/client-full-direct-1111 --binary engine --no-display -j8 --filter 'InitialBuildingGradient/*' --filter 'Hiring*/*' --filter 'BuildingGradientInitialization/*' --filter 'GradientPipeline/*' --filter 'UnitContinuation/*' --filter 'SavegameSafety/*' --filter 'Replay*/*' --filter 'TurnEngineHarness/*' --junit artifacts/initial-delay-split/compatibility-final.xml --artifacts artifacts/initial-delay-split/compatibility-final
cd platform
npm ci --ignore-scripts --no-audit --no-fund
npm run fixtures
npx vitest run packages/protocol/test
```

Each authentic-save receipt records the exact command, binary hash,input hash and duration. Inputs are included in published174-inputs/. Commands used explicit GPUoff/CPU backend,participant counts1or8 and CPUs17–24. Replace local paths with the included files when reproducing. Logs and replays are gzip-compressed to keep evidence small; decompress replay files before use. identity.json and sha256.json record binary/dependency/evidence identity. Raw older campaign artifacts remain untouched locally.

## Coverage rationale and limits

Initialization timing and merged ground/air routing change simulation results, so coverage emphasizes publication deadlines, concurrency invariance, Pending decisions, save/load state, replay/network acceptance and updated simulation goldens. Save support floor58 remains; synthetic boundary tests and authentic174 inputs supplement the broader historical-save tests. New layout175 distinguishes draft174 from released master173.

The broad quick run skips197slow/display cases. The focused93case run removes quick filtering for the named compatibility suites. No full hosted matrix, Windows/macOS/ARM/browser per-tick comparison, physical GPU backend validation, rendered play or human-feel review was performed on this revision. Linux tests cannot establish cross-platform execution equivalence. No overall gameplay balance/performance improvement is claimed for this final integrated source; earlier tournaments and selected delay probes use earlier revisions and are not substituted for final-source validation.

Maintainer acceptance: user authorized merge after independent review and fixes in this chat. Evidence records the actual coverage and omissions; it does not claim unperformed platform or manual checks.
