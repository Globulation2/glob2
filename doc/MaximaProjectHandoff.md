# Maxima optimization handoff — September 8, 2026

Start here when continuing under another ChatGPT/Codex account. The user asked
to wrap up, commit and push, while preserving the running tournament. The user
prefers action over repeated approvals, wants tournament work rather than PR
cleanup, and authorized rebasing onto mainline. Do not recreate the project.

## Latest direction: repairs retired; performance first; 100k cap

CURRENT — user authorized independent testing of ALL nine farming sub-switches.
Eight remaining switches are ACTIVE as separate 100-pair sizing pilots in
output/maxima-farming-sub-switches-pilot (800 comparisons, 927 unique games).
Remote output is /home/bradley/glob2-maxima-defense-fix/output/farming-sub-switches-pilot.
All nine behavior checks pass: 32 maintenance cases plus 48 access cases.
All other farming settings stay ON, including the master switch; identical ON
baselines are shared across comparisons, so cross-switch estimates are correlated.
Farm protection's completed 1000-pair confirmation remains separate evidence.
No old pilot outcomes pooled. Keep 100k cap, 48-slot allocation/devlaptop 12.
After all pilots complete, inspect pilot-budget-checkpoint.json and preregister
bounded confirmations for EVERY remaining switch, not only promising ones.
Treat pilots as inconclusive/descriptive, never as default-change authorization.
Inspect controller/status/errors and actively fix failures. Do not run duplicates.
Plan and controller snapshots: doc/maxima-handoff/farming-sub-switches.

CURRENT — completed September 9: corrected-engine farm-protection confirmation finished
all 1000 pairs / 2104 executions with zero missing pairs or game failures.
Farm protection ON won 530/1000 versus OFF 306/1000: +22.4 percentage points.
Predeclared multiplicity-adjusted interval: +14.34 to +30.99 points (alpha .04/53).
This confirms a helpful effect under this protocol, including its population-based
100k cutoff decisions; it is not exclusively natural victory detection.
Average throughput over confirmation was 18.90 games/minute.
Final report: output/maxima-defense-fixed-confirmation/CONFIRMATION_RESULT.json.
The completion reporter initially called pilot sizing for a confirmation stage;
fixed in 65a4383e8 (candidate) / 3d86c1ed2 (live), six fleet tests pass.
Recovered the final report from existing receipts, reran zero games, verified all
four remote queues complete and stopped idle workers. Old error is archived as
RESOLVED_REPORTING_ERROR.json. No defaults changed, no sample extension.
This campaign is COMPLETE, not stalled. Never restart it. Preserve final evidence.
The next project step is a separately predeclared experiment for another consequential
switch (farming.enabled is an exploratory lead), with appropriate behavior gates,
qualified freeze and no pooling of this confirmation. The standing direction is
to advance this project autonomously; do not interpret completion as a crash.

Previous operational states below are historical and superseded.

CURRENT (supersedes recovery/paused notes below): crash FIXED and fresh controls
ACTIVE in output/maxima-defense-fixed-controls, on all 48 slots.
Root on four hosts: /home/bradley/glob2-maxima-defense-fix.
ASan found a heap-buffer-overflow in compute_defense_flag_positioning: a building
origin crossed the map seam and indexed buildingGID with a negative coordinate.
Commit b7a9d33af wraps both read/write indexes. Regression reproduces old-code
ASan overflow and passes fixed combat suite; commit 2a175c656 also initializes
a lazy guard gradient in the existing fixture. Marshal alignment fix is included.
Both failed executions replayed twice without crashes and with identical signatures.
Four-host 40-case continuation, routing, farm behavior and 100k cutoff all passed.
Evidence and pipeline snapshots: doc/maxima-handoff/defense-fix.

output/maxima-defense-fix/continue-confirmation.py is already waiting for accepted
controls and will launch output/maxima-defense-fixed-confirmation: fixed 1000
pairs, one final analysis at alpha .04/53, no default changes or old-data pooling.
Do not launch duplicate controllers. Check STATUS/error/log files and actual
processes. The old faulty-engine confirmation is RETIRED on every host, not to
be resumed. Its completed receipts and both crash records are preserved.

Historical notes follow; the current state above takes precedence.


RECOVERY ACTIVE: user explicitly requires continued execution and autonomous fixes.
recovery-controller.py in output/maxima-farm-protection-confirmation-100k is
running untouched pending jobs on all 48 slots. Known failed execution is still
needs_investigation, never retried or replaced. Original STOP records are archived.
Final inference is blocked. This controller stops on new faults and collects
remaining results without publishing a result. Its process record is recovery-process.json.
Concurrent sanitizer diagnostic: TheRig Docker maxima-crash-sanitizer-full,
output/crash-77cfd-asan-full/gdb.log under the same frozen root. Sanitized binary
was built in glob2-maxima-mainline-0e9092a79/build-sanitize. Two original-binary
gdb diagnostics reached 100k but diverged from the failed run at tick 48332;
never substitute these diagnostic outcomes. A third ASLR-enabled run is in
output/crash-77cfd-aslr. UBSan found misaligned Marshaling.h writes, fixed locally
in candidate with memcpy and verified at offsets 0–7, but not yet established
as this crash cause and not deployed. Continue diagnosis while work runs.


ATTENTION: confirmation is PAUSED after a SIGSEGV on TheRig at approx tick 73937,
job 77cfd89e3edcc3d22814278348dae0c4d1b9d398a2cc493872c2b652672c296e.
All other active games finished; 1508 executions and 601 pairs are collected.
One job needs investigation; pending jobs remain undispatched. No final analysis.
An isolated debugger reproduction is running on TheRig in
/home/bradley/glob2-maxima-mainline-100k-final/output/crash-77cfd-debug.
Read gdb.log and DONE.json before further action. Diagnostic execution is not
a tournament retry/result. Keep fleet stop guards until the crash is understood.


CURRENT: independent fixed 1000-pair farm-protection confirmation is active in
output/maxima-farm-protection-confirmation-100k, with 2098 executions including
repeats, 100k cap and unchanged 48-slot limits. PLAN.json was saved before dispatch.
One final analysis only, alpha 0.04/53; no outcome-based extension, no pilot pooling,
no automatic default changes. CONFIRMATION_RESULT.json is the formal report.
This bounded campaign tests replication of a large effect; it does not promise
90% power for a 2-point effect (the generic pilot sizing suggested 31500 for that).

The completed pilot found 51 ON wins vs 19 OFF, with 35 ON-only and 3 OFF-only
outcomes: +32 percentage points, exploratory interval [0.1447,0.4840].
It completed 210 executions in about 19 minutes, ~11 games/min, without failures.


Completed farm-protection pilot in output/maxima-farm-protection-pilot-100k.
100 predeclared pairs, 210 executions including repeats, 100k cap, 48 slots with
devlaptop 12 on CPUs 0–11. Remote output: /home/bradley/glob2-maxima-mainline-100k-final/output/farm-protection-pilot-100k.
Analyze after all pairs/repeats complete. This is exploratory sizing, not confirmation;
no default changes or automatic next-stage dispatch. Read pilot-report.json and
pilot-budget-checkpoint.json at completion. Existing controller handles collection.

Fresh controls PASSED: 200/200 pairs, 418 executions, no failures, about 24 minutes
(17.4 games/min). Baseline won 121 versus 10 with focal AI off; the predeclared
acceptance interval for the difference was [0.363,0.747]. These are control results,
not evidence of farm-protection benefit. No old outcomes pooled.


Completed controls records: output/maxima-mainline-controls-100k, 200 pairs
and 418 executions including repeats. Controller process record lives there;
remote output is /home/bradley/glob2-maxima-mainline-100k-final/output/fresh-controls-100k.
Verified 48 active workers: devlaptop 12 on CPUs 0–11, pharaoh-dev-2/3 three each,
TheRig 30. Read STATUS.json, controller.log and CONTROLLER_ERROR.json if present.
All gate evidence is stored with the campaign; no prior control outcomes reused.
Completion writes controls-budget-checkpoint.json; require accepted=true before
planning/dispatching the next switch. Automatic next-stage dispatch is disabled.
Snapshots are in doc/maxima-handoff/controls-100k.


The next campaign must use a 100,000-tick cap, as requested by the user.
The candidate protocol, checkpoint interval and cutoff qualification now use
100,000; all 45 Python harness tests pass. Retired runs retain their old limit.
The new freeze and native 100k cutoff gate passed on therig.local in
/home/bradley/glob2-maxima-mainline-100k-final, log native-100k-cutoff.log,
proof output/qualification-100k/tick-limit/PASS.json. The same Linux binary is
reused because the cap change touches only harness code, not C++ source.
Read the proof before dispatch. The first attempt exercised 100k in its standard
fixture but failed an outdated assumption that a historical long game must still
reach the cap (it now ends naturally at 32002). Final source a0cf15c69 requires
an actual cutoff witness across the fixtures and passed from a fresh freeze.
The standard fixture ended at exactly 100000 with a saved checkpoint and population
scoring; the other fixtures ended naturally at 28674 and 32002.
Evidence: doc/maxima-handoff/mainline-validation/NATIVE_100K_PASS.json.

The user stopped repairs confirmation as a low-priority, inconclusive result.
Do not restart it or report proven equivalence. The controller was stopped and
all four hosts have STOP_DISPATCH/RETIRED guards; games already running may
finish. `output/maxima-mainline-transition/collect-retired.py` collects their
receipts without granting dispatch authority. Read DRAIN_STATUS.json for live
counts. No defaults changed and no new switch campaign is authorized to start
until mainline performance work is assessed.

A fresh fetch confirmed the candidate already includes current origin/master
753531310. The isolated Linux build and all 40 Linux continuation cases passed.
The 10-scenario timing comparison passed: median tick throughput is 1.4386x
the old engine, with 9/10 cases faster. PERFORMANCE_RESULT.json preserves all
case timings. This is a bounded microbenchmark, not cluster games/minute. Its initial
launcher used a nonexistent top-level remote protocol path; this was corrected
to batch-01000/protocol.json before any timing cases ran.
Remote pipeline: /home/bradley/glob2-maxima-mainline-0e9092a79/qualify-performance.sh
on therig.local; log: qualify-performance.log. Benchmark output:
output/performance/RESULT.json. Timing uses alternating engine order, CPU 31,
same maps/seeds/settings and a 20,000-tick cap. It measures ticks/second, not
whole-game cluster throughput. Do not deploy unless qualification passes.
Fleet-wide qualification and fresh controls remain required before new inference.
The four-host 100k freeze qualification passed all 40 cases on every host, with
identical signatures. It was controlled by
output/maxima-mainline-transition/fleet-qualify.py. Read fleet-qualify.log and
FLEET_PASS.json there. Each host must pass 40 cases and matching signature hashes.
Only one qualification process runs on devlaptop, pinned to CPU 0.
Repairs is fully drained: 1221 completed pairs, zero running or uncertain jobs.
The collector exited normally; retain its receipts and retirement guards.
The extra routing/continuation suite passed on TheRig. The farming fixture needed
two removed updateLocalResources calls adapted to updateGlobalGradient, then all
32 farming behavior cases passed against the frozen Linux objects. Proofs are
in doc/maxima-handoff/mainline-validation. Original frozen test files were not
changed remotely; run-farm-fixture.py compiled a temporary adapted fixture.
Next: assemble honest gate provenance (including statistical-sensitivity test
evidence) and start 200 fresh control pairs with 100k cap. Fresh controls have now been dispatched; see the active campaign above.

Old exploratory pilots suggested larger effects for farming.enabled and
farming.farm_protection_enabled. They are leads for the next plan, not confirmed
results, and must not be pooled with new-engine outcomes. The three-hour monitor
was updated to respect retirement and prioritize this performance transition.

## Current workspaces and branches

- Live checkout: `/Users/bradley/glob2`, branch `codex/ai-maxima-testing`.
- Rebased candidate: `/Users/bradley/glob2-maxima-mainline-update`, branch
  `codex/maxima-mainline-update`, pushed at `559c65bc3`. All 32 original commits rebased onto
  `origin/master` at `753531310a9872839f6702a62e69653b9e3776e5`.
- Separate farming optimization: `/Users/bradley/glob2-maxima-farming-performance`,
  branch `codex/maxima-farming-performance`, pushed at `f9fe9a600`; not deployed. Its investigation is
  `doc/MaximaFarmingPerformanceInvestigation.md` there. It showed synthetic
  porosity speedups and differential equivalence, not whole-game speedups.
  The handoff rerun passed 12,000 differential cases; an attempted standalone
  rerun could not run because its expected binary was absent.
- Other agents own pending-construction PR cleanup. Leave their worktrees alone.
- Repository: https://github.com/Globulation2/glob2 . Mainline is `master`.

## Retired tournament: historical configuration and draining evidence

Protocol v21:
`ecadf1aa37469c333300a06b11d3a8b01a37f0f85bd4af941db1e9782e3c3dbb`.

Local controller/results: `output/maxima-repairs-confirmation-v21`.
Qualification/transport: `output/maxima-live-controls-v21`.
Remote root on all active hosts:
`/home/bradley/glob2-win-continuation-20260908`.
Remote runs: `output/repairs-confirmation-v21` under that root.

Read live `STATUS.json`, `controller-process.json`, `CONTROLLER_ERROR.json` if
present, and `controller.log` before taking any action. At handoff preparation,
1,046 pairs were complete, batch 01000 was running with 48 games. Controller PID
was 8389, caffeinate PID 8390. PIDs and status are snapshots, not restart commands.

Allocation:

| Host | Simulation slots |
| --- | ---: |
| devlaptop.local | 12, pinned within CPUs 0–11 |
| pharaoh-dev-2.local | 3 |
| pharaoh-dev-3.local | 3 |
| therig.local | 30 |

Devlaptop CPUs 12–15 must remain unused by tournament games (two physical cores).
`runtime-policy.py` enforces the cap by prefixing its remote fleet commands with
`taskset -c 0-11`; changing allocations.json alone does not cap actual workers.
pharaoh-dev-1 remains quarantined for an older audit inconsistency.
SSH uses the direct v21 ssh-config, not a proxy through devlaptop.

All 200 fresh control pairs passed. Repairs ON versus OFF reached its first
registered look at 1,000 pairs: ON won 504, OFF 501, with 65 ON-only versus 62
OFF-only wins. Paired difference +0.003; adjusted interval approximately
[-0.06599, +0.07359]. No significant effect; decision was continue.

The six registered looks are 1,000, 2,000, 4,000, 8,000, 14,000 and 21,000 pairs.
Stop only under the saved adjusted-look policy. No automatic default changes.
The completed first look and policy are copied into the handoff snapshot.

The controller permits idle hosts to start the next 500-pair batch only within
the current registered look. It never starts that next batch on a host still
running its previous assignment. It pulses the next queue during archival.
This fixed a global barrier that left 47 slots idle behind one slow game.
Afterward, 1,047 executions completed in about 118 minutes (8.9 games/minute),
with 11.9/min in the first hour and 14.8/min in the first 15 minutes. The long
checkpoint tail again reduced completion rate. These are games including
repeats, not independent pairs. Old 60–70/min throughput is not yet recovered.

## Rebased candidate: status and remaining work

The rebase is complete. A release build and 45 Python tournament tests passed
before the final telemetry correction. Native trapped-unit, clearing-flag,
swarm-survival, pending-construction and team-stat save checks passed.
Pending-construction coverage now also tests binary/text 16-bit gradient values
and absent lazy fields. Do not claim the candidate is deployed or fully qualified.

Integration changes preserve our defeat and pending-construction fixes, restore
the audit hash backend removed as unused upstream, adapt saved map/building
execution state to lazy 16-bit fields across every swim class, and remove the
obsolete Castor resourcesCluster checkpoint field. Save format is now 103.
Mainline v89 saves remain loadable; experimental v90–102 saves are rejected and
must use their original engine. Do not load v21 checkpoints into this candidate.

The first 40-case no-orders/continuation attempt passed two cases, then failed
case 02 (Numbi opponent, FFA4, Archipelago, seed 1342203282). The state at the
checkpoint tick 2137 matched, but the first world divergence was tick 2604.
Root cause found: checkpoint runs implicitly enable observer telemetry.
Mainline changed resourceAvailable to lazily create a gradient. Observer telemetry
therefore created extra simulation caches only on the resumed side, changing
round-robin gradient updates and later game behavior.

The final correction introduces Map::cachedResourceDistance, which reads only
an existing field, and uses it in observer telemetry. It does not allocate or
refresh caches. Temporary diagnostic instrumentation in MaximaExperimentAudit.cpp
was removed. The final release build with this correction passed. The fresh 40-case
continuation rerun subsequently passed all 40 cases, and all 45 Python tournament
tests passed again. Committed evidence: `doc/maxima-handoff/mainline-validation/`.
The earlier failing qualification outputs are retained as evidence and must not
be treated as a qualified final binary.

Final build was launched with:

```sh
cd /Users/bradley/glob2-maxima-mainline-update
scons --build=build-mainline -j4 release=1 server=0 build-mainline/src/glob2
```

The build completed successfully; its log is
`/tmp/maxima-mainline-final-observer-build.log`. The following qualification
commands have now completed successfully (do not rerun into these directories):

```sh
python3 tools/maxima_win_experiment.py --prepare \
  --binary build-mainline/src/glob2 --output output/mainline-handoff-qualification
python3 tools/qualify_maxima_no_orders.py output/mainline-handoff-qualification \
  --output output/mainline-handoff-qualification/no-orders
```

Use a fresh output directory if that one exists. Expect all 40 cases to pass;
do not silently waive continuation. Linux portable build and fleet qualification
are still required before switching future matches to the new engine. The user
explicitly accepts mainline pathfinding changes and wants progress, not another
debate about whether to rebase. Keep both arms of each pair on the same engine,
retain existing results, and record the engine/version boundary. No live binary
or protocol has been replaced in this task.

## Cleanup and evidence

The user explicitly authorized deleting obsolete tournament data. Removed the
old local tournament-results folders, large pre-v21 output artifacts, and about
372 GiB of old cluster run data. Mac free space rose from 1.2 GiB to roughly
104 GiB. Small summaries and provenance remain; old raw games do not.
`output/maxima-live-controls-v21/cleanup-20260908` records exactly what happened.
The v19 RETIRED.json guard remains because the current controller reads it.
Do not restart old runs or claim their full raw evidence is still retained.
All four v21 frozen inventories passed verification after cleanup.

Earlier diagnostic tooling on devlaptop spawned thousands of Python processes
and exhausted memory. The offending remote `bisect.py` was disabled and the
processes cleaned up. Never import/run from that old diagnostic directory.
Devlaptop recovered and passed all 40 qualification cases before rejoining.

## Continue after account transfer

1. Read this file and the live STATUS/process records. Inspect errors and worker
   health; preserve active engines and never rerun uncertain jobs automatically.
2. The Mac 40-case continuation and 45 Python tests passed. An isolated Linux
   build is running on therig.local in Docker container
   `maxima-mainline-build-0e9092a79`, limited to two CPUs and 8 GiB. Its root is
   `/home/bradley/glob2-maxima-mainline-0e9092a79`; read `build-portable.log`
   and inspect the container exit code. Then qualify the Linux binary and fleet
   and run a bounded performance comparison. No new engine is deployed.
3. Follow the latest user direction when making the recorded engine transition.
   Preserve the 12-slot devlaptop cap and registered significance checkpoints.
4. A three-hour heartbeat named **Advance Maxima optimization**, automation ID
   `advance-maxima-optimization`, is attached to task
   `01a0811a-2bad-7d61-b55d-53c18858d0d6`. Its local configuration is
   `/Users/bradley/.codex/automations/advance-maxima-optimization/automation.toml`.
   Check its presence after switching accounts; avoid duplicate monitors.

`doc/maxima-handoff/runtime/` contains a small committed snapshot of operational
scripts, protocol, gates, policy, first look and process records. These scripts
belong at their original `output/...` locations; do not run them in the snapshot
folder. Never overwrite newer live state with the snapshot. Actual per-game
receipts, build binaries, and SSH credentials are not stored in Git. They remain
on this Mac and the cluster; Git alone is not a complete run-data backup.

The local optimizer interpreter is `/Users/bradley/glob2/.venv-optimizer/bin/python`.
Use that absolute path without resolving its symlink for controller subprocesses.
Do not modify unrelated untracked `artifacts/`.
