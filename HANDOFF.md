# Delayed gradients: takeover handoff

Implementation head: `e130ee8d0b06e078cb9909b74f763e9c85bd2f33` on `codex/delayed-gradient-publication`.
Integrated master/base: `e7572da6be15566bf20086f40a73a0f4b7c2a83d` (fetch on October 15 found no newer master). Initial design/Callgrind baseline: `dd493d7e71d21e5ab6cbaee0fe168a37e076860a`.

The user explicitly stopped local completion and requested a draft PR so another agent can finish on another machine. The native build process tree and the dedicated Docker container were stopped. No further local tests should be started in this chat. Other agents' worktrees/containers were left alone. There are no uncommitted source changes; an unrelated user-owned `.playwright-mcp/` directory was preserved and excluded.

## Implemented scope

Complete delayed publication for building, material/natural/market, consumer supplier, guard/clear, forbidden/escape fields, and air destination guidance. Existing pipelines/executor, FIFO capture/admission, immutable complete outputs, fixed deadlines, explicit Pending consumers, soft-dirty follow-up and hard identity validation, save continuation and kernel-entry instrumentation. Private clearing choices enumerate all alternatives; air shared fields propagate deterministic goal labels. Immediate air A*, local steering and hazardous-terrain escape remain owner work. The attached air-plan.md is the full supplied air design.

Start with docs/architecture/simulation.md and the feature diff. Main new components: src/field/AirGradient.h; src/map/gradient/AirGradientRuntime.{h,cpp}, AirGradientTypes.h, AirDestinationSelection.cpp, FieldReadiness.h, GradientExecutionAudit.h. New runtime suites: AirGradientPublication and DelayedGradientPublication. Existing building/area/material/consumer suites were adapted, along with hiring, movement, snapshots and save harnesses.

VERSION_MINOR=166, FILE_FORMAT_VERSION_DELAYED_GRADIENTS=166, SIM_REVISION=64, replay minimum=166. MINIMUM_VERSION_MINOR stays58. These are one coherent unlanded simulation change. Check current master before landing; version collisions may need updating. **The golden match record/trace has NOT been regenerated. The sim-revision contract will remain incomplete until that happens.**

Air logical memory accounting covers runtime-owned generations under the documented scoped owner-lease contract. Arbitrarily retained external leases remain safe but can exceed that budget; future long-lived worker consumers need an explicit logical retirement protocol. Preserve this disclosure or improve that contract deliberately.

## Actual verification so far

- Refreshed Linux ARM64 unit binary at implementation head:39 JUnit cases passed, zero failed (5 AirGradient,24 GradientPipeline including audit lifetime stress,9 replay appearance/step-counter,1 provenance). See linux-unit-final-run.log, linux-unit-final.xml and linux-unit-final/.
- Earlier native macOS ARM64 AirGradient+GradientPipeline:28 cases,217155 assertions passed; this predates the final audit stress and test-only GCC11 allocation fix. Native replay appearance/step-counter:9 cases passed.
- Audit lifetime standalone AddressSanitizer smoke:128 scope-retirement/worker-record races passed without diagnostics; source/log are in mac-publication/.
- Private RNG contract passed; sim revision checker unit tests9 passed; diff whitespace check passed. Actual golden contract is pending.
- Independent static review found no remaining blocking issue in inspected code, including13f5d8268. See independent-review.md. This review did not run engine tests.
- Neither full engine build linked before user cancellation. Engine runtime suites, zero-owner assertions, binary/text continuation, participant/platform trajectory comparison and CLI mid-game tails are therefore **not executed/established**.
- One Linux compile failure was fixed: libstdc++11 lacks make_shared<T[]> in a new test fixture. Final e130ee8d0 uses supported shared_ptr array allocation. The restarted build passed that harness compilation and continued into production objects before cancellation.
- A broader native unit sweep was stopped after an untagged Maxima.Farming CPU-time assertion was discovered (user asked to skip performance evaluations). Do not rerun this untagged timing case as correctness evidence. An unchanged ImageAsset 16-bit PNG rounding test failed twice (isolated log retained); baseline/master reproduction was not done, so do not call it a proven master regression.
- No new release profile, wall-clock evaluation or speedup claim. baseline-callgrind/ contains existing release evidence only, with macro/micro/thread perspectives and caveats. It predates later master improvements.

## Resume on the other machine

1. Checkout the PR branch and build with `python3 tools/dev_build.py unit-tests engine-tests <build-dir>/src/glob2 -jN`. Use that platform's actual build-dir, flags and pinned dependencies. Keep final performance work separate and honor the user's skip request.
2. Run unit AirGradient/*, GradientPipeline/*, ReplayAppearance/* and ReplayStepCounter/* with test/run_tests.py. Run engine AirGradientPublication/* and DelayedGradientPublication/* with --exclude-tag benchmark --no-display --timeout600. They write participant traces, kernel counts and binary/text saves; retain those artifacts.
3. Run the relevant ground inventory in linux-ground/command.txt (add GuardAreaBalance/* and --timeout600). Run changed consumer/snapshot/compatibility inventory in mac-consumers/command.txt, adapted to the platform build directory. Establish actual selected-case inventories; don't assume unmatched filter names ran. All benchmark/display cases are excluded. Preserve real failures and fix bugs before updating expected results.
4. Regenerate the main golden using the engine filter `TurnEngineHarness/the committed*` with --update-fixtures. It updates test/fixtures/multiplayer/FourSquares1.g2mr and FourSquares1.verify-trace.txt. Then verify normally. MarketsV2's three golden traces and GuardAreaBalance's golden may also require intentional trajectory updates; inspect differences after core behavior/continuation checks pass. Run remaining nongolden TurnEngineHarness and MatchSetup acceptance tests. Run `python3 test/check_sim_revision.py --base origin/master` after fixture regeneration.
5. midgame runner/inputs are in mac-midgame/. run_continuations.py runs sequential256-tick tails of the original tick12000 and24000 Playground saves at participants1/2/4/8, compares per-tick checksums, records source/input/binary hashes and exact commands. It was prepared, not run. Env overrides: GLOB2_CONTINUATION_ROOT, _BINARY, _SAVES, _OUTPUT, _TAIL, _BUILD, _BASE and CXX. Use _BASE if the source snapshot lacks origin/master. --ticks is an absolute final tick, not a tail length. CLI isolates profiles beneath output directories. Don't publish generated profile/preferences files or use result timing fields as performance evidence.
6. Compare per-tick/full-state trace artifacts across participants and actual platforms. Retain/save-check warm,cold,queued,computing,complete-unpublished and superseded states. Worker configurations must show zero owner kernel entries for covered families; serial execution is the exception. Air A*/terrain safety counters remain separate.
7. Fetch current master, resolve actual overlaps, commit fixes/fixtures, rebuild/revalidate the final revision as necessary, and refresh the PR evidence comment. Keep the PR draft. Windows/x86/mobile/browser/release readiness and maintainer playtest remain unverified.

## Environments and stopped local builds

macOS ARM64, Apple clang21/SDK27, normal fast development -O0 -g1, no PCH/unity. Native build log:mac-build-stopped.log. Dedicated Linux Docker:Ubuntu22.04 ARM64/GCC11.4/Python3.10/SCons4.0, normal fast development -O0 -g1. Build log:linux-build-stopped.log. Container glob2-delayed-gradients is stopped and recoverable locally; /work tracked tree exactly matched implementation head (verified7a66926ab87b405d5f18fd4adb634daa1a8ad5b5). It has a shallow source snapshot; use the GitHub branch for complete history on the other machine.

Do not touch glob2-hiring-callgrind or other chats' containers. An earlier source-copy collision was restored and the affected chat was notified with user authorization. Always suppress macOS tar metadata when transferring files: COPYFILE_DISABLE=1 tar --no-xattrs.
