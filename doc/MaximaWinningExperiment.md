# Maxima winning-probability experiment

## September 8 user-authorized retired-data cleanup

The user requested deletion of old tournament data to recover disk space.
Obsolete local tournament-results folders and large pre-v21 output artifacts
were deleted, along with retired ablation/wave/older-win run data on all four
active cluster hosts. Small provenance and summary records remain; historical
claims below about complete retained raw v19 evidence describe the state before
this cleanup. The old `RETIRED.json` guard remains in place. Retired idle workers
were stopped only after confirming they had no active game children.

The complete v21 run, source/build artifacts, current qualification evidence,
sequential policy, save/load investigation and separate farming optimization
were preserved. Cleanup records are under
`output/maxima-live-controls-v21/cleanup-20260908`. The Mac's available space
increased from 1.2 GiB to approximately 104 GiB; current v21 games continued.

## September 8 corrected-engine restart: v21

All 1,000 v19 confirmation pairs have finished. The final batch has 1,034
execution receipts, including repeats; all are collected and validated locally.
The engine is retired without another significance look. `RETIRED.json` records
this in `output/maxima-repairs-confirmation-v19-resumed`, and both obsolete
inference controllers now refuse to restart. Remote raw evidence is retained;
verified lossless compression is complete on all four old hosts.

The replacement is `/home/bradley/glob2-win-continuation-20260908`, with local
records in `output/maxima-live-controls-v21` and the new controller in
`output/maxima-repairs-confirmation-v21`. Protocol:
`ecadf1aa37469c333300a06b11d3a8b01a37f0f85bd4af941db1e9782e3c3dbb`.
Read the two `STATUS.json` files and controller process records for live state.

This freeze includes PR174's reserved-meal/final-food defeat correction and the
pending-construction save/load correction. The latter preserves the ordered
`Game::buildProjects` queue, which previously disappeared on reload despite its
reserved footprint surviving. The original Castor case (seed 1342283299,
checkpoint 2137) now matches through tick 5000, including all subsequent orders.
The user requested fixing this defect before restart: **continuation remains a
required gate**, with no uninterrupted-only exception. The original failed v20
evidence is preserved and its commissioner is stopped.

The portable build was produced on TheRig inside an Ubuntu 24.04 container with
an 8 GiB memory limit. Native pending-construction, hungry-defeat (including an
old-code negative control), trapped-unit, swarm, clearing-gradient and repairs
fixtures pass, as do 45 Python harness tests. Routing and extended continuations
pass. Before controls, every participating host must pass the same 40 no-orders
and checkpoint-continuation cases with identical boundary/terminal signatures;
the real worker must also pass the exact 200000-tick cutoff fixture.

The next batch uses pharaoh-dev-2 (3 slots), pharaoh-dev-3 (3 slots),
TheRig (30 slots), and devlaptop (12 slots), for **48 slots**. The user requested
spare CPU on devlaptop: the v21 transport runs its fleet commands under
`taskset -c 0-11`, leaving logical CPUs 12–15 (two physical cores) unused by
simulation workers. Measured topology confirms exactly 12 workers. The existing
pharaoh-dev-1 audit quarantine remains.

Devlaptop recovered after scoped cleanup of this task's diagnostic script,
which had spawned thousands of Python processes and exhausted memory. The
script is disabled; all old game results were preserved. This was a diagnostic
tooling failure. Devlaptop subsequently passed all 40 no-orders and continuation
cases with identical signatures to the other three hosts. The v21 direct SSH
configuration avoids routing healthy-host traffic through devlaptop.

The 200 fresh AI-off control pairs (422 executions including repeats) retain
their original three-host assignments and 36 slots. The local controller was
replaced without interrupting remote engines; existing manifest, assignments,
allocation and qualification hashes were checked unchanged. The fourth host
joins the next newly prepared batch, after controls pass. See `CAP_HANDOFF.json`,
`DEVLAPTOP_REJOIN.json`, and `ACTIVATION_PLAN.json` in the v21 control directory.
Only accepted controls allow repairs confirmation.
The six adjusted looks and 21000-pair maximum remain unchanged. Controls use
namespace 9; confirmation uses offset 100000. No old outcomes are pooled, no
defaults change automatically, and actual power under the corrected engine is
not known from the old pilot. The separate farming optimization is excluded.

## September 8 scheduling correction: idle hosts may advance

Fresh controls passed. During the first confirmation batch, 499 of 500 pairs
were complete while one healthy Oazis game on pharaoh-dev-2 remained CPU-bound.
The global batch barrier left 47 of 48 slots idle. The operational controller
now lets a host finish its assigned jobs and begin the next 500-pair batch,
provided that batch stays within the next registered significance checkpoint.
It never overlaps simulation slots on the same host or dispatches beyond a
checkpoint before its decision. Active future queues receive health pulses,
including during archival. Existing manifests, results and engines were
preserved, and the frozen engine and statistical policy did not change.

Tests cover every look boundary, controls, incomplete assignments, busy hosts,
already-running future jobs and failure handling. Live verification restored
46 running games while the original slow game continued; devlaptop has exactly
12 engines pinned within CPUs 0–11. See `OVERLAP_HANDOFF.json` and
`MONITOR_REPORT.json` in `output/maxima-live-controls-v21`. The first registered
look remains at 1000 completed and validated pairs. The standalone farming
optimization still requires full-game equivalence and timing before a future
engine freeze.

## September 8 amendment: scheduled early stopping

The user authorized amending the active repairs confirmation before confirmation
effects were inspected, while preserving all completed and in-flight work.
`output/maxima-repairs-confirmation-v19-resumed/SEQUENTIAL_AMENDMENT.json`
is the registered analysis policy. It supersedes the fixed-final-analysis and
no-outcome-dependent-stopping instructions below for this confirmation only.
The immutable engine protocol and original `PLAN.json` remain historical records.

The historical amended coordinator was `controller-sequential.py` in that directory;
read `controller-process.json`, `SEQUENTIAL_ACTIVATED.json`, and `STATUS.json`.
Do not restart `controller.py` or any older confirmation controller.

Analyze complete prefixes at 1,000, 2,000, 4,000, 8,000, 14,000 and 21,000 pairs.
Allocate respectively 2%, 3%, 5%, 10%, 20% and 60% of the existing hypothesis
alpha (0.04/53) to the existing exact paired interval at those looks. By the
union bound, all six intervals simultaneously cover with error at most 0.04/53,
under the original sampling assumptions; independence between looks is not
required. Stop on an adjusted interval excluding zero in either direction,
or at the 21,000-pair maximum. There is no futility rule or unscheduled inference.
Do not substitute an ordinary 95% interval or the original unspent final alpha.

Each look occurs after all jobs and deterministic repeats in its last 500-pair
batch finish and their evidence is archived, before the next batch is prepared.
Existing scenarios, completed receipts, remote queues and in-flight games are
retained. Only the local coordinator was replaced; no game is killed or rerun.
Identity and complete-prefix checks reject missing, duplicate or reordered data.
The pilot remains excluded. Significant direction does not establish a two-point
minimum benefit; report that distinction and the simultaneous adjusted interval.
The raw effect estimate at early stopping can exaggerate magnitude. No defaults
change automatically, and any necessary combination check remains required.

Qualification used synthetic outcomes only: five statistical unit tests, two
controller safety tests, and 20,000 simulated campaigns per parameter setting.
Across the pilot disagreement range, the lowest 99% lower confidence bound on
power for a two-point effect was 96.96%, above the original 90% target. Simulation
supports operating characteristics; the exact intervals and union bound provide
the error-control argument. Evidence is in `SEQUENTIAL_QUALIFICATION.json`;
the helper and tests are `tools/maxima_sequential_confirmation.py` and
`test/MaximaSequentialConfirmationTest.py`. The controller uses a hashed copy of
the helper and checks the original statistics implementation hash at startup.

Monitoring must honor `look-*.json` and `CONFIRMATION_RESULT.json`. A successful
scheduled early stop is completion, not an interrupted 21,000-pair queue to resume.
Otherwise keep the same 50 slots, host quarantine, 200k game limit and audit
recovery policy. Notify on a stopping decision, completion, failure or required
action; do not introduce extra significance checks.

## Active stage: repairs confirmation (September 8)

Victory-detection status checked September 8: the frozen v19 sources contain the
trapped-exit starvation repair and `swarmHasRecoveryExit` terrain check. They do
not contain the newer reserved-meal/final-food useful-unit correction described
in open PR #174 (issue #110). Its frequency and effect on this confirmation are
not measured. A population cutoff does not correct premature natural losses;
account for this limitation before adopting any winning-probability conclusion
as a default change. PR #180 (trapped-unit starvation) and PR #181 (clearing
gradient bounds) are also still open upstream, although their underlying fixes
are already in the tournament freeze. Open issue #107 concerns prestige display
or victory configuration and is not a confirmed tournament failure. No new
engine patch or scoring change was applied during this status inspection.

All 53 switch pilots are complete: 5,300 paired comparisons and 5,454 distinct
execution identities including repeats. Repeat checks passed, and all results
and measured budgets are consolidated in
`output/maxima-extra-pilots-v19/ALL_SWITCH_PILOTS.json` and `.md`.
The screening sequence has finished; do not restart its completed game queues.

Repairs OFF remains the strongest exploratory lead (62/100 wins versus 51/100 ON),
followed by adaptive inn staffing OFF (60/100) and defensive siting OFF (58/100).
The selected next allocation resumes the previously approved **21,000-pair repairs
confirmation** on the four healthy hosts / 50 slots, approximately 53 hours by
pilot-measured cost. Inn staffing would require 21,000 pairs / approximately
51 hours; defensive siting 31,500 / approximately 82 hours. Those are later
candidates, not additional dispatch authorizations inferred from significance.

The active controller is
`output/maxima-repairs-confirmation-v19-resumed/controller.py` with process and
progress records beside it. Remote output is
`/home/bradley/glob2-win-population-20260907/output/repairs-confirmation-v19-resumed`.
`SELECTION.json` records the full-screening choice and measured allocation.
The original `maxima-repairs-confirmation-v19` remains paused as historical evidence;
do not start its old controller. The resumed controller excludes pharaoh-dev-1
and uses the existing qualified freeze, fresh confirmation scenarios, fixed
21,000 pairs, alpha 0.04/53, and no outcome-dependent early stopping. It checks
repeats during 500-pair operational batches, archives completed evidence with
verified lossless compression, and withholds effect estimates until the full
fixed sample finishes. No defaults change automatically. First-batch identities
were checked to be disjoint from the pilot scenarios, and live dispatch was
verified on all 50 slots.

The three-hour heartbeat must now monitor this confirmation controller, recover
existing audits before considering reruns, preserve the host quarantine, and
continue toward trustworthy confirmation and any necessary combination check.

## Current execution priority: screen all switches before confirmation

On September 7 the user selected broad screening before expensive confirmation.
The original v19 12-switch pilot is complete (1,200 pairs, 1,313 execution
identities, repeat checks passed). Repairs OFF remains an exploratory lead,
not a default change. The repairs confirmation controller was stopped before
any confirmation games were queued; `output/maxima-repairs-confirmation-v19/PAUSED.json`
records the pause. Do not resume it until the full pilot comparison has informed
a confirmation allocation.

All **53 switches now have native behavioral qualification**. Eight supplemental
fixtures cover the other 41 switches in 368 cases: economy/staffing 4/36,
military/economy policy 5/60, director 11/88, reconnaissance 4/40, tactics 3/48,
farm maintenance 5/32, farm access 5/48, and placement 4/16. They exercise paired
ON/OFF decisions, selection or orders, with parent/eligibility negative controls.
Selection fixtures deliberately control candidate ranking or observed world
features at the real decision boundary; they establish behavioral sensitivity,
not the prevalence or competitive benefit of that behavior in real matches.
The tests link unchanged v19 engine objects. No game source, scoring, defaults,
or benchmark changed. Receipts, native logs, binaries, source snapshots and
included-fixture provenance are under `output/maxima-extra-pilots-v19/evidence`;
`audit-matrix.json` maps all 53 switches to evidence.

The four-switch `economy-01` screening pilot is complete (400 pairs, 505
execution identities). The 37-switch `screen-01` pilot is executing next. The local
`output/maxima-extra-pilots-v19/pilot-sequence.py` controller queues subsequent
qualified groups after each prior wave reaches `stage_complete`. Its
`sequence-process.json` and `sequence-state.json` identify the active controller
and wave. Per-wave `STATUS.json` and `supervisor-process.json` identify progress
and the supervisor. Each switch receives 100 paired scenarios on the same
predeclared pilot benchmark; byte-identical baseline execution identities are
reused from the original completed pilot. This saves simulations without treating
shared outcomes as independent observations across switches. Pilot observations
are never pooled into confirmation. The sequence dispatches pilots only.

Use the existing fleet, currently **four active hosts and 50 simulation slots**.
Pharaoh-dev-1 is quarantined: it produced inconsistent audit and manifest reads
under both Python 3.14.4 and an isolated Python 3.12.3 runtime. The latter had
initially passed ten manifest reads and three audit recoveries, but later failed
too. Do not restore that host merely by selecting Python 3.12. The exact machine,
runtime or hardware cause remains unisolated.

`screen-01/HOST_QUARANTINE.json` records reassignment of 162 unstarted jobs to
the healthy hosts. Completed identities were retained. All 51 newly simulated
completed audits on the quarantined host passed independent controller validation
(`screen-01/quarantined-audits/PASS.json`); four other completed identities were
reused baselines. The failed audit was recovered without rerunning its engine.
`runtime-policy.py` blocks new dispatch to the quarantined host and supplies its
archived completion metadata for result transfer. Keep that policy during restarts.
The operator adapter also validates pulse responses and makes exception messages
nonempty, preventing a transient probe failure from crashing the frozen supervisor.
No engine or scoring changes were made.

Use `.venv-optimizer/bin/python` without resolving its symlink for all local
controllers, launchers and reporting; system Python lacks the statistical packages.
On September 8 UTC, strong-opponent pilot jobs were released early through the
idempotent start API to avoid leaving healthy hosts idle behind the last weak
opponent games. The fixed sample, assignments, game limits and repeat checks remain
unchanged; existing games finish and each host retains its queue ordering.

A later local disk-full error interrupted result compression and stopped the
supervisor. At the September 8 03:36 UTC check, 64 GiB was free again. The original
JSON receipts and remote queues were intact, so dispatch resumed without reruns
or deletions. The operator adapter now retries ENOSPC after 30 seconds; it reloads
existing receipts and never substitutes new games. See `screen-01/disk-recovery.json`
and the current `supervisor-process.json`. Recheck local and remote free space
on subsequent monitoring runs.

The three-hour monitoring heartbeat remains active. Verify live processes and
remote queues, preserve failed-run evidence, recover existing completed audits
where possible, and keep screening moving. After all pilots finish, consolidate
all 53 effects and measured costs, select confirmation candidates, and retain
family-wide error control and independent confirmation samples. Any confirmed
game bug still needs a separate PR with regression, reproduction steps and saves;
PRs #180 and #181 are already assigned to another agent.

## Current protocol: 200k with population adjudication (September 7)

The user approved restarting with natural victory first, then a population
comparison at exactly 200,000 ticks. This supersedes historical cap/scoring
policies below. New campaign: `output/maxima-live-controls-v19`, Linux root
`/home/bradley/glob2-win-population-20260907`, remote output
`output/win-controls-v19-20260907`. V18 dispatch is retired; its already running
games may finish within 200k, but its outcomes are not pooled into v19.

The audit counts current unit objects directly, including units inside buildings,
instead of using periodically sampled TeamStats. A formal focal win/loss takes
precedence. Otherwise, an exact cutoff receipt compares surviving, non-eliminated
sides: each opponent separately in FFA and combined allied population in 2v2.
A sole highest-population side wins. Equal highest populations are a draw.
All unit types count equally. Missing/corrupt receipts remain execution errors.

The estimand is the probability of a **sole tournament win**: wins score 1;
losses and explicit draws score 0. This preserves the binary paired inference;
it is not a half-point standings-score analysis. Each execution retains
`natural_outcome`, `decision_reason`, and (where used) cutoff populations and
leaders. Reports distinguish natural wins/losses, population decisions and draws.
The engine's victory flags are never altered by tournament adjudication.

Fresh controls use namespace 8, scenario offset 20000. All subsequent stages
use that offset to avoid prior pilots. The same 200-pair AI-off control must pass
under the new scoring, then the 12 behaviorally validated switches enter pilots.
The 40 targeted Python tests cover scoring and existing integrity/statistics.
Native qualification compares old/new trajectories across all 20 cells and both
AI-off arms, checks the exact deadline, and repeats no-orders/continuation checks
across the five hosts. Only the population audit changes inside the engine.

Prioritize running the tournament. Investigate actual victory-condition defects
afterward; confirmed bugs should be isolated into separate PRs with saved states,
reproduction steps, and regression tests for another agent to complete.

The final v19 freeze is
`9922ea13125ed2b4c0a4ae786bd8d30e5ff4e5ce0a0cd8346932a0e68208c462`.
Qualification passed: 40 targeted Python checks, 40 matched old/new native
trajectories, all five hosts' 40 no-orders/continuation cases (240 matching
boundary receipts per host), and three real-worker deadline cases. The known
long case stopped at exactly 200k with populations 41/43/33/123 and awarded
a population win while retaining its null natural outcome.

The initial qualification comparator compared wall-clock `microseconds` in
profiling messages and correctly failed equality. The corrected comparator
normalizes only that timing field and compares all remaining decisions/orders.
The initial candidate protocol and proofs are preserved. Final freeze changes
only that qualification script relative to the tested candidate; engine bytes,
scoring, maps and all other runtime hashes are identical. Explicit provenance
is in v19's `candidate-equivalence.json` and `evidence/qualification-provenance.json`.

## Historical campaign record


This protocol replaces the exploratory score/opportunity campaigns. Existing
campaign evidence is retained, never pooled with this experiment. No AI default
has changed. The **v12 archive contains 425 completed executions (199 complete
pairs) and one reboot-interrupted execution**, preserved as `needs_investigation`.
The **v13 candidate stopped qualification with two late continuation failures**.
A corrected **v14 completed controls but failed the sensitivity gate** at `output/maxima-live-controls-v14`
and `/home/bradley/glob2-win-clearing-20260906/output/win-controls-v14-20260906`.
The latest **v15 candidate passed execution qualification**, locally at
`output/maxima-live-controls-v15` and remotely at
`/home/bradley/glob2-win-recovery-20260907/output/win-controls-v15-20260907`.
It cannot launch controls or pilots while the control-design choice is pending.
Save versions 100–101 preserve elimination flags, construction state, cached
resource requests and the construction cooldown. Maxima defense scheduling now
uses persisted execution state instead of a diagnostic timestamp. The new worker
runs each game continuously to its formal outcome or 720,000-tick cap, saving
checkpoints every 180,000 ticks. Automatic checkpoint resumption is disabled.

The active three-hour follow-up, `finish-maxima-tournament`, checks live queues,
repairs blockers and advances the experiment until trustworthy results are
available. No confirmatory results are available yet.

## September 7 control checkpoint and investigation

V14 passed all qualification gates, then completed 200/200 pairs and 432/432
executions in approximately 2.9 hours (56.73 engine hours). No execution or
deterministic-repeat failure occurred. There were 38 unresolved executions,
including repeats; 34 distinct nonrepeat games reached the cap. Twenty-six
pairs contained an unresolved arm. Among fully observed discordant pairs,
baseline won alone in 15 and attack-disabled won alone in 8. The required
stratified control interval was [-0.2871, 0.3571], so acceptance failed. The
coordinator correctly stopped before pilots. Do not rerun the final analysis
with extended outcomes, loosen the bound, or treat this as a successful control.

`controls-budget-checkpoint.json` preserves the complete report. Fourteen separate
capped-game extensions completed under `cap-investigation` on devlaptop and
TheRig: one resolved at tick 992834; thirteen remained unresolved at 1440000.
There were no diagnostic errors. Results are collected locally under v14's
`cap-extension-results`; original control outcomes remain unchanged.
Native inspection of all 34 capped nonrepeat
saves found only one trapped unit, in a team with many other surviving units.
It also found empty teams kept alive by cached swarm accessibility, including
a swarm with no actual exit and production timeout -560673.

A candidate repair checks actual terrain around a swarm when recovery matters,
ignores temporary mobile-unit occupancy, and accepts exactly one birth's food
(matching the production rule). `SwarmSurvivalTest` fails on the old engine and
passes with the repair. A paired real-save replay stays unresolved under v14
but declares the winner at tick 720002 with the repair, from an identical
checkpoint boundary. All four existing native suites and 28 Python tests also
pass. Evidence is in `output/maxima-swarm-recovery-diagnosis`. V15 freezes this
repair under protocol
`c8e334e4c2ba4a6bf15a62cbba10d894e680476ba1d745f088d451a4cf4335e7`.
Its fresh build passed all five native suites, the continuous-worker fixture,
and the eight reactive-defense cases. Five-host routing/continuation checks
and 27 fresh late scenarios (25 historical plus two swarm cases) all passed.
The persistent `watch-qualification-only.py` collects and validates the evidence,
then stops. It has no inference launcher and creates no control/pilot manifest.
See `NO_INFERENTIAL_DISPATCH.json` and `qualification-watcher-process.json`.

Supplemental behavioral qualification against the unchanged v15 engine now
validates `teamplay.enabled` and `teamplay.defense_enabled` through 16 combinations
of tactics/parent/child switches and hostile-threat eligibility, checking actual
relief missions, allied targets and flag orders. Four additional cases validate
`military.preemptive_defense_enabled` through trained-warrior eligibility, choke
selection, desired guard tiles and management orders. Sources and linked objects
are hashed in `evidence/teamplay-behavior.json` and
`evidence/preemptive-behavior.json`. `SUPPLEMENTAL_BEHAVIOR.json` records all seven
validated switches; the earlier audit matrix is preserved separately. These are
behavioral checks, not pilot samples or win-rate claims. Forty-six switches
remain unvalidated. The engine and statistical gates are unchanged.

The attack-disabled control's warrior-raiding parent gate was verified in code;
adding `raiding.enabled=false` would not repair the failed sensitivity result.
A user decision is pending on preparing a new known-degraded no-orders control
with fresh seeds, the same benchmark and confidence requirement, versus retaining
the attack-disabled control and proposing a larger sample. Neither revision has
been dispatched. The old attack-disabled result must remain visible either way.
V14's `control-design-planning.json` illustrates the cost of retaining the bound:
assuming fully resolved outcomes and a true 3.5-point benefit, a conservative
90%-power sufficient size is 19300 balanced pairs, about 103 ideal fleet hours
at the measured runtime. This is not a registered allocation or an estimate of
the actual effect; the unresolved pairs prevent that interpretation.

## Clearing-flag pathfinding repair

V13 completed all 25 late comparisons: 23 passed and two configurations of the
same scenario diverged after checkpoint 91,074. The first differing unit action
was at tick 91,092. Both games had identical serialized state; rebuilding the
clearing flag's gradient incorrectly treated prune trees as clearing targets in
the loaded game. Local and global gradient builders indexed the five-element
`clearingResources` array with fruit IDs 5–7, reading alignment padding beyond
the array. This is a bounds defect, not an omitted save field.

V14 restricts those lookups to the five basic resource types. The new native
`ClearingFlagGradientTest` varies object padding while checking both gradients,
all fruit types, basic resource switches, empty tiles and swimming variants.
It fails against v13 and passes with the bounds repair. V13 STOP and comparison
artifacts remain immutable. V14 must pass fresh qualification before controls;
none of the old or diagnostic outcomes are pooled into the experiment.
Both previously failing scenarios now match at the checkpoint, terminal state
and full continuation order stream. Evidence is in
`output/maxima-live-controls-v14/clearing-diagnosis.json`. Commit `d72e1cca`
contains the repair. All four native fixtures and 28 Python tests passed, as did
the continuous-worker and eight reactive-defense fixtures. The new protocol is
`a4eb930e297b67ea5e618bb8ea5754a5ed986bf753f78a3dff87a58519d9bf8d`.
Five-host qualification and all 25 fresh late scenarios passed. V14's
`commission-when-ready.py` validated the complete evidence and launched controls;
the coordinator later stopped at the failed control gate described above.

## Trapped-unit outcome repair

Inspection of 52 capped nonrepeat games found trapped units in 32 games; 29
contained an alive team whose remaining useful units were all trapped. In the
inspected cases the buildings belonged to the units' own teams. Wood and wheat
blocked exits after indoor service completed. `Unit::handleMedical` suspended
hunger indefinitely while waiting to exit; food/heal buildings or MED_FREE units
then kept the elimination rule from declaring loss.

The candidate resumes normal hunger and starvation while an exit is blocked,
preserves active feeding/training protection, and cleans up an indoor death
without clearing a map tile occupied by another unit. Native regressions cover
wood/wheat blockage, eventual loss/winner detection, rescue, active-service
protection and save/load. Four real-save paired replays compare identical
continuation lengths: old engine outcomes remain unresolved; fixed engine
outcomes are wins at ticks 728642, 729922, 729922 and 728866. These are diagnostic
regressions only, never additions to the original controls.

Evidence: `output/maxima-trapped-unit-diagnosis/SUMMARY.json` and
`paired-real-save-results.json`. The fresh v13 protocol is
`bf43c29a4c8adf9dec1e60afad07a80e7559a37246399b56467337f523d0724b`.
V13 qualification replays all 25 historical failure scenarios from fresh starts.
If corrected elimination ends one before its historical checkpoint, it explicitly
records and checks a checkpoint 512 ticks before that earlier terminal outcome.
Existing v12 results remain immutable and are not pooled into v13. The persistent
`output/maxima-live-controls-v13/commission-when-ready.py` checks every 30 seconds,
collects all qualification proofs, then commissions fresh controls. Its process
record and log are in that directory. It refuses dispatch on qualification STOP.
If an old game still occupies a host, a deferred launcher waits for its completion
before starting that host's new workers. It never kills or duplicates an old game.
TheRig rebooted during the first v13 long qualification attempt; its incomplete
artifacts are preserved as `long-continuation-interrupted-reboot`. The second
attempt completed with the two failures described above. Pharaoh 1's interrupted
matrix was also archived and successfully rerun; all 40 v13 matrix cases passed.
The cause of the reboot is not established. The v13 commissioner stopped on its
qualification STOP and remains stopped.

## Current qualification result

The previous v9 run stopped with 76 completed executions, 13 complete control
pairs and 25 failed jobs. Its evidence remains at `output/maxima-live-controls-v9`.
Candidates v10 and v11 isolated additional late continuation defects; their
STOP records remain intact. No samples from these runs are pooled into v12.
The current candidate is `output/maxima-live-controls-v12` locally and
`/home/bradley/glob2-win-restart-20260906/output/win-controls-v12-20260906` remotely.
Launch verification confirmed 53 live engines and five completed executions, with no STOP records. All 25 archived failure scenarios passed before dispatch. Twenty-four reuse
hashed uninterrupted reference fixtures; the cooldown case is regenerated with
the new save format. See `tools/qualify_maxima_long_continuation.py`.


The September 6 restart integrates the Numbi TeamStats continuation repair
(commit `a9634680`, cherry-picked from `918da1d5`). Additional native qualification
found and repaired:

- Live duplicate references in ordered building work lists rejected by the loader.
- Castor caches, projects and decision cadence omitted from saves (version 98).
- The team exchange-vision mask saved from the wrong member.
- Uninitialized Echo bookkeeping and two Maxima relief-plan fields.
- Echo area coordinates loaded in unspecified function-argument order.
- Echo's shared gradient queue, cached fields and update cadence omitted from
  saves (version 99).

Maxima remains benchmark ID 6; Cortex uses ID 7. Versions 89–94 remain rejected
because pre-rebase experimental layouts overlap upstream layouts. Archived
experiments retain their own binaries and source bundles. No AI default changed.

The current freeze is at
`/home/bradley/glob2-win-restart-20260906/output/win-controls-v12-20260906`
on all five Linux workers. The same portable Linux binary is deployed to every
worker. The local coordinator directory is `output/maxima-live-controls-v12`. Its protocol
ID is `f1fb39bf1550ac1daefee9a7bf2a7cb988dde45eacb3844c4b9f9740459a2c5a`.
Qualification passed on all five hosts, all 40 format/opponent cases and all 25 late failures.
Earlier `v1`–`v11` candidates are diagnostic evidence, not inferential samples.

Qualification includes 40 routing cases per host, identical executions across
hosts, four duel continuations per host, and a distributed 40-case continuation
matrix covering every opponent, format, and all six 2v2 sides. Matrix games run
20,000 ticks for duels and 7,000 otherwise, with a checkpoint at tick 2,137.
Both complete issued/dispatched order streams and boundary/terminal world, RNG,
and AI state must match. Native fixtures cover the repaired state and offensive
controls; 28 Python experiment, receipt and fleet tests pass.

`tactics.enabled`, `tactics.siege_enabled`, `explorer_campaign.enabled`, and
`defense.reactive.enabled` passed the original behavioral fixtures. V15's
supplemental fixtures add the three switches described above, leaving 46
registered and unvalidated. The attack-disabled control recipe disables
`tactics.enabled` and `explorer_campaign.enabled` for focal players, suppressing
warrior siege/raid and explorer campaigns while retaining defensive responses.
Full-game sensitivity acceptance remains required before the pilot.

The coordinator supports persistent remote workers, topology-derived CPU
reservation, expiring dispatch leases, deterministic repeat validation, and
independent compressed result transfers. A failing receipt stops new dispatch
across the fleet without killing active engines. The local SSH config can route
four workers through devlaptop if the Mac's direct LAN route is unavailable.
No user-wide SSH configuration changes are required.

## Live run and recovery

- Local status: `output/maxima-live-controls-v12/STATUS.json` (updated every 30 seconds).
- Supervisor process record: `output/maxima-live-controls-v12/supervisor-process.json`.
- Qualification evidence, fixed manifest, allocation and launch verification live
  in that same directory. The manifest has 200 pairs and 426 executions including
  deterministic repeats. The first cohort has 100 Numbi/Castor pairs.
- Commissioning helpers (`commission-collect.py`, `commission-launch.py`) and
  `read-fleet-proof.py` are retained in the local run directory. Collection refuses
  incomplete qualification; launch refuses an existing supervisor record. The
  remote `frozen-source-and-binary.tar.gz` preserves the commissioned source and binary.
- Remote per-host queues and runs live beneath the freeze directory above.
  Workers reserve one physical core per machine: 3 slots on each Pharaoh, 14 on
  devlaptop and 30 on TheRig. Transfers and supervision run independently.
- The supervisor uses `.venv-optimizer/bin/python` and
  `MAXIMA_FLEET_SSH_CONFIG=/Users/bradley/glob2/output/maxima-live-controls-v12/ssh-config`.
  Keep the virtual-environment interpreter path; resolving its symlink launches
  the system interpreter without the experiment dependencies.
- Controls produce `controls-budget-checkpoint.json`. Acceptance starts
  `output/maxima-live-controls-v12-pilot` automatically, with 100 pairs each for
  the four validated switches. Pilot output includes its report and a measured
  confirmation budget. Confirmation requires that allocation checkpoint.
- A stopped or missing supervisor expires remote dispatch authority after 90
  seconds; active games survive. Inspect `STOP_DISPATCH.json`, queue errors and
  existing PIDs before restarting. Never retry an uncertain running job or
  overwrite a prior audit. The supervisor can resume collected receipts.

The Mac supervisor is kept awake during this run with `caffeinate -i -w <pid>`.
Manual sleep or shutting down the Mac can still pause coordination. The original
archived tournament evidence remains separate and has not been pooled.

## Authoritative artifacts and commands

Use `requirements-maxima-experiment.txt` in a dedicated environment. Build the
current source (including `MaximaExperimentAudit.cpp`); the isolated local audit
build is `build-validation/src/glob2`.

```sh
python3 tools/maxima_win_experiment.py --prepare \
  --binary build-validation/src/glob2 --output /absolute/new-campaign
python3 tools/qualify_maxima_win_experiment.py /absolute/new-campaign
.venv-optimizer/bin/python test/MaximaWinExperimentTest.py
.venv-optimizer/bin/python tools/maxima_win_statistics.py --sensitivity \
  --hypotheses 53 --output /absolute/sensitivity.json
```

Preparation writes a candidate `protocol.json`, compiled registry, defaults,
source consumer candidates, an explicitly unvalidated audit matrix, and 200
balanced control scenarios. The protocol hash covers the binary hash, runtime,
source, maps, analysis, registered hypotheses and distributions. A changed file
invalidates the candidate. Preparation is not a successful final freeze.
Qualification failures write `STOP_DISPATCH.json`; no full-game worker may
continue assigning new work while this or `RETIRED.json` exists.

The JSONL channel is requested with
`--maxima-audit /absolute/new-file.jsonl <JSON-identity-bundle>`. It records:

- Binary, protocol, scenario, initial-state and full-configuration identities
  supplied by the harness and verified against frozen files by the harness.
- Post-load/pre-decision and terminal receipts for every player, including actual
  implementation RTTI (also checking Nicowar's inner Echo implementation), teams,
  alliances, requested and actual complete Maxima settings, RNG and AI checksums.
- Initialization events, subsequent decision events, issued orders and orders
  dispatched through `Game::executeOrder`, with serialized order payloads.
- Checkpoint and terminal state, formal win/loss flags and termination category.

All records have a version and contiguous sequence. Missing, duplicated,
truncated, incorrectly routed or incorrectly configured receipts are rejected.
An existing audit file cannot be overwritten. The canonical world checksum
excludes the map file format version; the raw engine checksum and map-header
checksum are also recorded so this normalization is visible. Restoring an older
map into a current save changes storage version without itself changing world
state.

Decision events currently retain their original key/value payload. Generic
telemetry is not switch-specific eligibility or verified behavioral evidence.
`order_dispatched` means the order entered engine dispatch; it does not claim
that a rejected/irrelevant order changed the world. The three offensive switches have explicit native offensive-path, eligibility, and
parent fixtures. Reactive defense additionally passes eight paired on/off native
cases covering incursion/attacked-worker eligibility, inactive-world silence, and
positive flag staffing and threat coverage. The supplemental fixture and runner
are `test/MaximaAdditionalSwitchBehaviorTest.cpp` and
`tools/qualify_maxima_additional_behavior.py`. Their hashed proof is embedded in
`evidence/behavioral-extended.json`; the engine, frozen sources, registered
hypotheses and existing control comparisons are unchanged. Other switch-specific behavior still requires evidence; every
unsupported switch remains unvalidated.

## Benchmark and staged allocation

The benchmark is fixed at 20 cells, each weight 0.05: duel, FFA3, FFA4, FFA5, 2v2
crossed with Numbi (1), Castor (2), Nicowar (5), baseline Maxima (6). All opposing
players have the selected implementation. Both allied Maxima players receive
treatment. Mixed FFA lineups are excluded. Farming parents remain enabled while
testing children. The experimental reference has farming enabled; this reference
is explicit and does not modify shipped defaults.

The compiled map inventory is frozen. Maps are sampled uniformly within format
among compatible team counts (exactly four starts for 2v2). Non-team seats and
valid offsets are uniform; 2v2 samples uniformly across three partitions and two
sides. Controls have ten independent pairs per cell: first the 100 Numbi/Castor
pairs, then the 100 Nicowar/Maxima pairs. Difficulty is measured without dropping
floor/ceiling cells or revising weights. Pilot and confirmation scenarios sample
format and opponent uniformly and independently of outcomes. Seed namespaces for
qualification, controls, pilot, confirmation and combination do not overlap.

Required controls include identical configurations within/across hosts and at
save/load boundaries, combat-ready attack-disabled fixtures and full games, and
synthetic statistical sensitivity. Attack-disabled acceptance requires verified
suppression of specified offensive paths and a baseline advantage with a 95%
interval excluding zero. For the fixed balanced control allocation, the report
uses a conservative Hoeffding interval valid for independent heterogeneous paired
differences, including unresolved outcome bounds. This threshold and method must
remain frozen before games run. No attack-disabled recipe is presumed valid
merely because several settings are false.

After controls, report sensitivity, completion, cell win rates/disagreement,
floors/ceilings, long-game runtime and feasibility. Then run 100 fresh paired
scenarios per validated switch. Pilot data estimates variance/cost and never
makes confirmatory claims or excludes a switch because its effect looks small.
The pilot checkpoint reports simulated sample requirements, repeat/transfer/
long-game cost and ETA at the verified reserved capacity. Explicit measured
compute allocation is required before confirmation.

## Exact final inference

For iid paired binary wins, let `d = on_only + off_only`, `b = on_only`, and `n`
be the independent scenario count. Estimate `q=d/n`, `r=b/d` and
`Delta=q*(2*r-1)=(b-off_only)/n`. With per-hypothesis error `alpha=0.04/H`, construct
two-sided Clopper–Pearson intervals for q and conditional r, each with error
`alpha/2`. Evaluate all four corners of their Cartesian product under
`q*(2*r-1)` and take the extrema. Zero disagreements leaves r unrestricted and
q with a nonzero upper confidence limit; it is not certainty of no effect.

If m pairs have an unresolved arm, bound q over d through d+m disagreements and
r over every compatible assignment; an outer envelope uses CP lower(b,d+m) and
upper(b+m,d+m). This covers all completions within the same alpha allowance.
Capped games are never converted to draws or discarded. Formal losses/draws are
nonwins. Intermediate economic/military scores are only diagnostics.

Fixed-sample power simulations use this exact decision rule, both signs of a
true 0.02 effect and conservative 99% pilot disagreement limits. Sample sizing
requires a 99% Monte Carlo lower bound of at least 90% power across the pilot
q grid. No observed-power veto or significance-based early stop is allowed.
There is one immutable final analysis per switch. Registered H includes all
registered switches, including any remaining unsupported ones.

Results record helpful/harmful (zero excluded), magnitude above two points
(the whole interval beyond the relevant threshold), practical equivalence
(the whole interval inside ±0.02), or unresolved. Direction and equivalence may
both be supported for a small precisely estimated effect. Detecting a true
2-point effect is distinct from proving its magnitude exceeds 2 points.
Per-cell/format/opponent results remain descriptive, with no format veto.

`maxima_win_report.py` builds comparison manifests and deduplicates executions
only when scenario and complete player configurations match. Repeats cannot
increase n or substitute for an independent arm. Final publication requires all
scheduled execution/repeat receipts, while genuine capped outcomes may remain
null. A changed-data second final analysis is forbidden. Confirmed harmful-ON
switches with estimated costs of at least two points form the complete proposed
configuration. Its separate fresh test reserves alpha=0.01. Defaults remain
blocked until the complete combination passes; failure requires interaction
investigation. There is no automatic default-editing code.

## Fleet and retirement

Topology measured on the five existing hosts yields ceilings **3/3/3/14/30**,
53 concurrent simulation jobs total, reserving an entire physical core including
SMT siblings on each host. Each worker pins one engine to one allowed logical
CPU and forces common math-library thread counts to one. Persistent SQLite
queues retain claims, results and independent transfer states. Unknown/stale
host health cannot claim work; three failed minute probes disable dispatch and
three successful probes restore it. An uncertain running claim is investigated,
never automatically re-executed. No engine timeout kills or SSH-client kills
are used by the new worker.

Fleet rollout remains uncommissioned while qualification fails. Host workers
need a verified common artifact/spool arrangement and global stop propagation;
remote deployment/transfer orchestration and hourly independent-pair throughput
reports must be commissioned before full-game controls. Queue primitives alone
are not evidence of a qualified fleet.

The old optimizer controller was terminated without signaling its harvest child
or SSH clients. The stage finished, and `archive_maxima_campaign.py` sealed the
170,955 local evidence files in place (139,710,966,826 logical bytes). Original
remote evidence remains at its prior roots. The retirement marker and snapshot
entry-point guard prevent old-schedule restart. Its monitor policy explicitly
forbids further dispatch or pooling. Do not alter that old frozen evidence.

## Remaining release gates

- Repair and verify execution-relevant save/load state, including RNG and all AI
  implementations, within/across hosts and all seat/alliance arrangements.
- Complete per-switch eligible/ineligible/parent-disabled behavioral fixtures and
  exposure receipts; only promote supported switches in the audit matrix.
- Verify attack suppression through orders, attacks, damage and eliminations,
  then run the 200 full-game control pairs and deliver the first budget checkpoint.
- Freeze the repaired engine/runtime/opponents/maps/analysis together; commission
  persistent fleet operation and global stop/transfer/throughput monitoring.
- Run the sizing pilot and deliver measured compute allocation requirements;
  confirmation and combined-configuration inference remain future gated stages.


### Authorized full-AI-off positive control (v16, 2026-09-07)

The user selected a fresh control that disables the entire focal AI. The
experiment-only `--maxima-no-orders-player INDEX` bypasses `AI::getOrder`
before the implementation ticks or drains queued orders. Every focal player
is disabled in the off arm (both allies in 2v2); opponents remain active.
Autonomous world, unit and building simulation continues. This is not a
shipped strategy switch. The mode is recorded in player boundary receipts
and configuration identities. The harness reapplies it explicitly when
loading a checkpoint; the save format is unchanged. Receipts reject any
focal decision, issued order or dispatched order under this treatment.

The new fixed control uses 200 fresh matched pairs, ten per benchmark cell,
with seed namespace 6, separate from the earlier namespace 1 attack-disabled
control. The 95% stratified Hoeffding lower bound must still exceed zero;
unresolved games remain unresolved. The v14 failed result is preserved and
never pooled. Passing this control establishes gross measurement sensitivity,
not a benefit from tactics or power to detect a two-percentage-point effect.
Those questions still require behavioral validation, pilots and independent
confirmation. A failed full-AI-off control stops further inferential dispatch.

`tools/qualify_maxima_no_orders.py` checks normal versus disabled behavior,
active opponents, unchanged disabled AI serialized state, and exact
checkpoint continuation across all 20 cells and all six 2v2 sides (40 cases).
The existing native regression and fleet continuation gates also apply.

The v16 candidate is frozen as
`8c590c8a15604b038a6525375c4570509ee2c3193103fa87abd79b704041d776`,
with coordinator state in `output/maxima-live-controls-v16` and Linux root
`/home/bradley/glob2-win-no-orders-20260907`. All 32 Python tests and five
native suites passed. All five hosts passed the 40-case no-orders suite;
normal, disabled and resumed boundary/terminal signatures match across hosts
(`NO_ORDERS_QUALIFIED.json`). The seven existing behavioral switch fixtures
were rerun successfully against this freeze. The persistent
`commission-when-ready.py` waits for all 27 historical late-continuation
cases, validates the complete evidence, then commissions 200 fresh control
pairs and a supervised fleet run. It may advance only validated switches to
pilots if the full-AI-off statistical control passes. Read live status and
`commissioner.log` / `supervisor.log` to determine the current stage.

### v16 control accepted; pilots running (2026-09-07, 12:33 UTC check)

The full-AI-off control completed all 200 pairs / 420 executions and passed:
its stratified 95% acceptance interval is [0.102935, 0.637065]. There were
105 observed baseline wins and 24 observed disabled wins; 15 unresolved
pairs remain explicitly uncertain, with possible win-rate differences
[0.295, 0.445]. These are control results, not switch-optimization findings.
Total engine time was 29.62 hours, about 2.06 elapsed fleet hours.

The coordinator automatically started the seven-switch pilot: 700 paired
comparisons, 808 unique/repeated executions after permitted baseline reuse.
Live probes confirmed 53 running slots across all five hosts, no stop flags
and no execution failures. Pilot state is `output/maxima-live-controls-v16-pilot`.
The same supervisor PID is recorded in the parent campaign. Note the actual
execution path is `output/win-controls-v16-20260906-pilot` under the v16 Linux
root; the inherited date suffix is a directory label, while protocol hashes
identify the qualified v16 engine. Qualification artifacts remain under
`output/win-controls-v16-20260907`.

Supplemental `raiding.enabled` validation passed eight native cases against
unchanged frozen engine objects: paired on/off, tactics-parent disabled and
visible-worker-cluster eligibility, with raid authorization and flag orders.
Proof: `evidence/raiding-behavior.json`; total validated switches: 8/53.
The active pilot manifest remains unchanged. Raiding needs a subsequent
100-pair pilot batch. The fixture ran at nice 19 on simulation CPU 0 of
devlaptop; its small shared-CPU overhead is recorded in the pilot directory
for throughput planning. An initial fixture omitted opponent reconnaissance
and correctly failed raid eligibility; its failed evidence was preserved.

### User-selected 200k limit and v18 restart (2026-09-07)

The user requested a hard game limit, initially 100k, then selected **200,000
ticks** after reviewing the actual completion-time distribution. The v16
720k pilot was retired: dispatch stopped, identified active engines were
terminated, and interrupted runs are never interpreted as results. The v17
100k candidate ran qualification only and was retired without inference.
Do not restart either campaign or pool their outcomes into v18.

The current v18 protocol is
`64c5ccd66129cf30f58dbe9d2420610638c5a23afaeb3f7c64dac48e2758f7ed`,
coordinated from `output/maxima-live-controls-v18`, with Linux root
`/home/bradley/glob2-win-200k-20260907` and output
`output/win-controls-v18-20260907`. The worker ends at 200k, harvests its
checkpoint at that tick, and preserves unresolved outcomes. Command creation
rejects caps above the protocol limit. 33 Python checks passed. The same
byte-identical v16 engine, maps, defaults, and all engine source hashes allow
its qualification to carry forward with explicit equivalence provenance.
New real-worker tests verified the deadline, including a known archived game
that would win after 200k but remains unresolved at exactly 200k. That reused
case is qualification only. Fresh controls use namespace 7 plus offset 10000;
subsequent pilots also use the offset and cannot overlap the retired pilot.

The 200 fresh control pairs / 422 executions are running under this limit.
The v16 sensitivity result is **not** reused as v18 control acceptance.
Behavioral coverage is now 12/53: added allied pressure coordination,
amphibious preemptive defense, repairs and upgrades to the previous eight.
These passed 16, 8 and 16 native cases respectively against unchanged frozen
engine objects. All 12 qualify for the fresh pilot if the new control passes.

The population/resource investigation is recorded in
`output/maxima-long-populations/REPORT.md`, with population charts, maps,
parsed telemetry and 18 saved-state inspections. Castor resource enclosure
is confirmed in one duel; Numbi stagnation can occur despite open paths.
No blocked exiting units were found in the sampled saves. Do not equate
stagnation or resource enclosure with a proven win-condition defect. The
existing every-three-hour monitoring automation now preserves the 200k cap
and carries these findings forward.
