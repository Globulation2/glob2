# Single-observation engine handoff

Implementation branch: `codex/single-observation`.
Implementation commit: 7bf4759d861512a0378560cda12b36fb1429c9a2.
Integrated base: `e7572da6be15566bf20086f40a73a0f4b7c2a83d`.
The user explicitly stopped local completion and requested a draft PR for another agent on another machine. Do not treat this as validated or ready to merge.

## Implemented

`Game::observeCompletedWorld` publishes due map/hiring/statistics/targeting results, captures once, and admits authoritative work from that handle. Engine AI admission takes the same handle; due AI command delivery remains separate. `advanceLiveSimulation` advances without preparation/capture. Direct callers use `Game::step` or explicit observe/order/advance phases. Removed `prepareReadBoundary`, old Game::syncStep, optional runtime snapshot fallbacks and ack-driven invalidation. Graphical sessions freeze optional presentation data at the boundary; camera views reuse it. Waiting-player status moved to the client channel. Real paused edits use observation revisions.

Zero-delay AI/statistics now mean next boundary; longer delays remain. Restore preserves original pending outputs/deadlines. Format166 saves map publication/submission markers; legacy164 AND165 migration tests exist, including immediate resave. Save floor58 unchanged. SIM_REVISION64, replay floor166. Upstream master already used format165/sim63 for checksum changes, so these numbers were advanced after integration. Architecture guides include the timing table.

Lifecycle fixes include preserving same-tick delivery after unpause and establishing observation after turn reloadDone. Focused regression tests cover these and actual core materialization counters.

## Critical unfinished work

1. **Golden match record and trace still contain master values. They MUST be regenerated for sim64/format166.** No final integrated runtime result exists. Earlier sim63 refactor golden evidence is superseded.
2. Complete the final native engine/unit/CLI build. The user stopped it during compilation, with no compiler errors observed. Rebuild on your machine; do not copy binaries/objects across toolchains.
3. Run focused engine suites: WorldSnapshot, ClientChannels, SimulationReadPhase, SharedWorkerLifecycle, AIOrderScheduler, AIPipeline, TeamStatsPipeline, HiringPipeline, TurretTargeting, EngineSession, TurnEngineHarness, gradient/resource growth, save/replay/version suites. Run unit suite. See helper final_native.py for exact filters and fixture update invocation; adapt its macOS build directory to your platform. Under severe shared host load three earlier broad tests hit120s limits; the final runner uses600s. These were timeouts, not diagnosed failures.
4. Run validate_midgame.py with release/native binaries against both included v164 fixtures. It checks exact per-tick world checksums for1 vs8 compute participants, normalizes to current format, checkpoints at a nonperiodic boundary, resumes and compares full/detailed records. It gzip-compresses large detailed sidecars after comparison. Compare native and Docker world.checksums for matching cases. Windows/x86/Web/mobile coverage remains unverified.
5. Build Linux release with symbols and run profile_final.py --with-12k after source identity and clean build provenance agree. Adapt container setup/name on another machine. It preserves a small stripped reference, instruments ONLY container EngineRun, compares clean/instrumented/Callgrind final checksums, measures identical old windows and restores source in finally. No new profile has run yet.
6. Inspect profile by exclusive source-file/line costs and code, including macro features and micro operations. Callgrind3.18.1 function labels/inclusive attribution are unreliable here; do not trust them. Parser/classifier helpers included. Recheck final code/docs/diff, update any exposed API doubles, and attach final evidence before readiness.

Routine native build: `PYTHONPATH=. python3 tools/dev_build.py engine-tests unit-tests <platform-build-dir>/src/glob2 -j3`.
Golden update: `python3 test/run_tests.py --build-dir <build> --no-display --timeout600 --binary engine --update-fixtures --filter 'TurnEngineHarness/the committed*' -j1` (spell --timeout and600 as separate arguments). Rebuild afterward to embed final fixture provenance.
Midgame driver: `python3 artifacts/single-observation/validate_midgame.py <glob2-binary> artifacts/single-observation/fixtures <evidence-output>`.

## Evidence status

Final integrated static checks passed: check_entity_random.py, check_docs.py (309documents/77links), git diff --check. Final native and Linux release builds were intentionally interrupted by user; neither final link nor final runtime tests/profiles completed.

PRE-INTEGRATION ONLY (dd493+refactor165/sim63, sourceSHA5e9132c79760d6d11daf1dfe8233ccf19497c31abade064c2df314ad68df7cd3): full native builds passed; retained targeted62tests passed0failed; broad preliminary coverage had34passes and3timeouts120s. These do not validate the integrated PR commit. XML/logs included.

Retained preintegration native24k run:1153steps,1153actual core entity materializations,85,182,808entity bytes versus old2307captures/170,502,064entity bytes. Serial1024step run likewise1024cores/75,570,648entity bytes. These are promising early data, not final integrated performance claims.

Map-copy/buffer metrics are confounded by scheduling AND diagnostic settings. Heavy per-tick checksum sidecars join future work, releasing snapshot leases early; serial no-sidecar vs old sidecar is not comparable. Old native1153steps map135,441,408bytes/buffers230 versus old Callgrind107,712,512/buffers188 despite identical checksums. New preintegration native143,816,704/buffers225. See map-copy-caveat.json. No map pool algorithm change or lifecycle fix justified by those unmatched numbers.

Old profiling: Ubuntu22.04 aarch64, GCC11.4, Valgrind3.18.1, release -O3 -g, no fast/PCH/unity, Docker6CPUs/8GiB,8participants/7workers, CPU bucket gradients. Stock Playground128x128, seed713,8NicowarFFA. Saves at12000 and24000 (7alive at24000). Measure12128..13152 and24128..25152,1024ticks each, run through end+1, NO telemetry sidecar. Owner old24k2,137,401,920Ir; all threads5,637,051,772. Owner old12k2,094,535,599Ir. Original evidence zip and analysis JSON included. New before/after Ir comparison will ALSO include intervening upstream BitArray/string_view/checksum optimizations; do not attribute all difference to snapshot refactor.

Owner still runs cold route/material field builds, forbidden/escape updates, deeper-than-prepared building search, full-array completion and overflow fallback. With8participants deferred callbacks stay on workers; owner waiting is not owner instructions. Distinguish feature responsibility, operation type, ownership and wait/memory dimensions.

## Environment and transport

Extract `single-observation-handoff.tar.gz` into the repository root. It populates ignored artifacts/single-observation helpers, fixtures and evidence, plus artifacts/sim-callgrind original baseline evidence. Nothing in the bundle belongs in the production PR commit.

Original local worktree /Users/bradley/.codex/worktrees/single-observation/glob2. Dedicated container glob2-single-observation-callgrind has matching source and partially built release; stopped. Its old source-identity.json is stale; source-identity-current.json is correct for precommit dirty source. All active builds/waiters were stopped at user request. Shared old glob2-hiring-callgrind is inactive; do not build there. Old original baseline binaries had redundant debug sections stripped for disk headroom; raw Callgrind evidence retained. No Docker image or compiler outputs are transported.

Dedicated Docker prior build environment: Ubuntu22arm64, GCC11.4, Valgrind3.18.1, tool-only local base glob2-teamstats-callgrind:ubuntu22-arm64 (not publicly published), SDL3 prefix /opt/glob2-sdl, pinned repo dependency prefix, Python venv /opt/asset-tools with Pillow12.2.0 and system-site-packages. Command: PYTHONPATH=/work PATH=/opt/asset-tools/bin:$PATH CCACHE=0 CCACHE_DISABLE=1 GLOB2_SDL3_PREFIX=/opt/glob2-sdl /opt/asset-tools/bin/python /usr/bin/scons release=1 retain_symbols=1 linker=auto -j2. Recreate dependencies using repository build guides on the new machine; do not assume the local image exists.

Recovery stash retained locally: bd9ce8362fd279517bd7daed3ea3bd0674354b0a (pre-integration snapshot; not needed to checkout PR). Temporary probe instrumentation never modified host production source.
