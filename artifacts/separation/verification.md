# Independent market PR separation verification

- PR #257 tested/pushed head: `e4217aae1ab60fa6166fcc0aa4024e77327bc70d`.
- PR #258 tested/pushed head: `a6cc475d6b070832007d0138e9e61a31148bacd6`.
- Source base: master `cd6ab247ac0feb48a5c90000ae39b3c3060e9891`.
- Latest fetched master: `c90e5548e3d9db0a15deada05c6ecae73531a391`.
- #254 hiring head is unchanged: `2bdaf33460789a5f1630a0ec31b55d7a988e32e1`.

## Final scope and integration

#257 is independent on master and contains no per-delivery hiring release, idle-worker threshold/helper or GigRelease harness. Its new retained-worker test includes an idle majority and proves that the worker keeps its employer after depositing, then starts another market delivery. The existing in-flight exchange-building save path is preserved. #258 is based on `fix/market-round-trip` and adds only higher market levels and associated acceptance/save tests. #254 remains a separate draft with its existing changes-requested balance review. Neither PR was merged.

Original heads are preserved in remote tags `codex/archive/pr257-before-separation-20261004` and `codex/archive/pr258-before-separation-20261004`. The two branch updates were pushed atomically with explicit expected-head force-with-lease checks. PR bases and descriptions were updated; draft status retained.

Master advanced while validation ran through edge-scrolling UI controls and release packaging. Their changes do not alter market/hiring/map-gradient simulation logic. Reviewed the new diffs and checked both integrations with `git merge-tree --write-tree`; both merge cleanly. No rebase merely for unrelated advances. Newer UI controls and release packaging were not rebuilt as part of these source revisions.

## Environment

Ubuntu 26.04.1 LTS, Linux x86_64 (kernel 7.0.0-31-generic); GCC/G++ 15.2.0; SCons native optimized client (`release=1 server=0`), C++20 `-Wall -fPIC -O3 -s`, engine and unit-test binaries. Installed pinned dependencies: SDL3 3.4.16, SDL3_image 3.4.6, SDL3_ttf 3.2.2, SDL3_net 3.2.0. The recording dependency manifest is retained with FFmpeg 8.0, x264 and nv-codec-header source locks. Build provenance JSON files carry the actual compiler/link flags and source-tree identity for final binaries.

## Commands and results

`commands.sh` records the common build/golden/focused-test commands. Final builds use `GLOB2_SDL3_PREFIX=/home/bradley/glob2-verify/integrator/sdl3/prefix CCACHE=1 scons -j16 release=1 server=0 engine-tests unit-tests build/linux/client/release/src/glob2`. Runtime commands set `LD_LIBRARY_PATH` to that prefix's `lib` directory. Exact final logs/JUnit and binary provenance are in `257-final-*` and `258-final-*`.

Golden regeneration: `python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'` passed once on each candidate. After committing those fixtures, the final revisions were rebuilt to refresh embedded build provenance and the focused tests were rerun.

Focused coverage: MarketFetch, HiringBucket, InnSwap, LevelGate, ResourceFetchTarget, RoundTripHungerGate, gradient suites, SavegameSafety, TeamStatsSave (including format 84/88 legacy saves), ReplayStepCounter, MatchSetup and committed TurnEngine golden verification. Includes binary/text market fields and queued publications, worker next-delivery attachment, source selection, stock exhaustion, pending-gradient scheduling, old save fixtures, current save continuation, replay version bounds and simulation-key hashing. #258 also checks stable upgrade IDs and saved stock/types.

#257: 54 runner jobs passed, zero failures, one skipped display-only TeamStatsSave screenshot case (94 JUnit cases total). #258: 55 jobs passed, zero failures, same one display-only skip (95 JUnit cases). The final JUnit reports include the selected-case inventory.

`python3 test/check_sim_revision.py --base origin/master` passed for each final source state. Save format 134, save floor 58, replay floor 127 and protocol 54 remain; independent #257 uses SIM_REVISION 16, #258 uses 17 to distinguish them from former stacked versions 13–15.

For each final client: `glob2 --verify-match <absolute record> --map <absolute maps/FourSquares1.map.gz> --out <absolute final-verify directory> --profile <unique profile>` returns verified through tick 701. `cmp <final-verify/checksums.txt> <committed FourSquares1.verify-trace.txt>` succeeds. Both final replay files, verdicts, results and full per-tick traces are retained. This proves same-platform golden consistency, not cross-platform equivalence.

## Limits

No macOS, Windows, Android or browser build/checksum comparison was run. No human market playtest, fresh balance tournament or visual review was performed; the earlier stacked branch's balance results do not apply to this independent market implementation. #254's hiring balance review remains unresolved on that separate PR. #258 retains placeholder costs/hit points, level-1 art, no AI upgrade construction, and fruit-only inter-team exchange. The drafts are reviewable independent scopes, not claims of merge readiness.

Initial `build.log` preserves a failed test-only compile caused by calling nonexistent Map::clearGroundUnit. It was corrected to setGroundUnit(..., NOGUID); subsequent build and final tests supersede that failure. Earlier pre-golden provenance records are retained; only final-revision build/test records are claimed as final evidence.
