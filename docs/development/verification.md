# PR verification and CI

Select checks from the actual risks, publish reviewable evidence, and distinguish local qualification from hosted CI results.

## CI timing and retained revisions

Pull requests cancel superseded revisions and run cheap contracts plus a predictive
subset for ordinary engine changes. The default ranking budget is 12% of eligible
logical checks; prerequisites and checks without history can increase execution.
Master finishes active full verification and keeps the newest pending push; all
retained master pushes select the full development matrix. Nightly fallback runs
at 06:00 UTC in a separate concurrency group without publication operations, and
skips expensive jobs only with matching available successful full evidence for
its exact revision and policy.
Release packaging first runs the full development matrix on the exact candidate
revision through `build.yml`'s `revision` workflow-call input.

The selector runs shared build-system contracts in a dedicated
`artifacts/build-contract-venv`, created with the pinned asset encoder's Python.
It installs `test/build_system/requirements.txt` there, including the asset
requirements and SCons, without modifying the shared encoder environment.

CI measurements batch completed runs hourly using trusted default-branch code and
inert artifacts. Cancellations do not create measurement workflows, and cheap-only
observations are excluded. Explicitly requested verification on drafts is included.
Batches retain attempt identities to avoid remeasuring
completed runs and attempt at most ten new measurements per batch. `feedback.json` reports p90 after ten successful matching-inventory
PR samples, with explicit gaps when there are fewer samples. Metrics report queue delay, active execution time, idle
gaps, aggregate runner minutes, feedback time and cache observations separately.
Per-job queue timestamps are estimates, not proof of runner saturation. Overlapping
jobs count once in wall execution time and separately in runner minutes.
Compare ten successful runs with matching event and exact coverage inventory using
`python3 .github/scripts/ci_run_metrics.py --before before.json --after after.json`.
Report workload reductions separately from execution savings. The initial service
objective for explicitly requested affected-PR verification is p90 feedback below
15 minutes and queue delay below two minutes; full verification and releases may
take longer. Cheap-contract feedback is not engine verification feedback. Missing
samples and missing inventories cannot establish improvements.

## Linux execution dependencies

The Linux runner bootstrap replaces the runner image's HTTP Azure Ubuntu mirror
with Ubuntu's HTTPS archive before installing dependencies. It retains immutable
indexes, signature verification, package hashes, bounded network timeouts and the
existing installation timeout. This avoids slow mirror transfers consuming the
whole installation budget.

GCC 13 builds its client and applicable transport programs once. Runtime consumers
reuse same-run artifacts with matching source/configuration provenance. Compiler
build jobs publish programs before separate CLI/scripting regressions; native and
browser consumers can start without waiting for those regressions. Golden-only
changes build once per selected platform and distribute programs to sweep consumers.
Full verification keeps complete primary and secondary generator sweeps. Predictive
PR runs may select individual generator groups or native shards while preserving
their original commands and four-way shard ownership. Full native inventory audits
require every eligible engine case to belong to exactly one shard.

Linux display shards and native coverage set `vm.max_map_count=1048576` for software Vulkan.
Lavapipe maps descriptor allocations individually, and large-map captures exceed
the older 262144-map runner limit. Mesa 25.2.8 can crash when those allocations
fail. This runner setting preserves the full capture inventory and matches the
limit used by the validated local development host; it does not change shipped code.

The Linux program archive includes the generated renderer shader bundle alongside
the executables and runtime assets so consumers can run without rebuilding.
Linux native builds and coverage compile the pinned build-only glslang tool to
include Vulkan shader assets. The test runner selects that bundle for the native
SDL GPU differential cases; the compiler is not part of the program archive.

Browser CI and development archives include launcher-relative scripts, including
`presentation-surface.js`, and the serial/threaded shader preload packages.
Downstream browser and packaging jobs consume this complete runtime closure.
The WebAssembly build job allows up to 120 minutes for the complete client and
test builds; reaching the deadline still fails verification.

Chromium renderer-switch checks run separately from the five general browser
shards. Serial cases use three shards with one worker per shard; per-test
sharding keeps a slow source file from occupying one whole job. Threaded cases
retain their own single-worker job. Serial functional budgets allow slow software
GL frames while retaining all screen, context, recording and live-game assertions.
These budgets and shard boundaries do not reduce the selected browser inventory.

ELF dependency collection records runtime package owners and rejects unresolved or
unowned libraries. Generator, CLI and browser consumers install runtime libraries
rather than compiler/header environments. Enable `CI_RUNTIME_PACKAGES_ENABLED=true`
for ordinary engine shards only after `Validate clean Linux runtime images` passes
both Ubuntu container images. Until then engine shards retain their prior package
installation; the clean-image dispatch also exercises CLI/scripting consumers.

Pinned SDL3, WebAssembly and Android dependency prefixes are cached independently
from application objects. Restored prefixes require matching inputs and complete
file hashes; invalid content rebuilds. Android tooling and Playwright installations
use pinned tool/package identities. Default-branch jobs publish shared caches;
ordinary PRs restore them. The cleanup sweep removes closed-PR caches and retains
three generations per master compiler/object family. Cache timings and quota
pressure must be measured before expanding retained cache data.

## Reviewed native shard timing profiles

Native runners optionally accept `--timing-profile` and `--auxiliary-jobs`.
Nonempty profiles assign jobs longest first to the least-loaded shard, with label
and shard-number tie breaks; unknown cases use the median recorded duration.
Empty or omitted profiles preserve alphabetical slicing and existing auxiliary
ownership. Unit tests and auxiliary groups participate in the same load plan,
retaining their original commands, flags and timeouts. Every selected engine job
runs exactly once. Auxiliary artifacts follow their assigned group.

Successful job durations are retained alongside JUnit results. To propose updated
weights, gather observations and run
`python3 test/build_ci_timing_profile.py observations --family ubuntu-24.04 --output test/ci-timings/ubuntu-24.04.json`.
Only platform-matched jobs with ten successful samples enter a profile. Review the
resulting diff before shipping it; weights never change during a run. Profiles
remain empty until measurements are available, rather than using invented data.

## Local and VM PR verification

Relevant local or VM testing is the standard PR verification path. Maintainers
accept evidence directly, including their own evidence; hosted CI success is not
a merge prerequisite. Choose tests from the change's actual risks and justify
coverage and omissions rather than reproducing the CI matrix. A VM supplies
coverage for its actual OS, architecture and configuration; testing on one platform
does not establish another platform's compatibility.

Record evidence in a PR comment using this template, with links accessible to
reviewers. Store generated files under ignored `artifacts/` and temporary narratives
under `docs/.work/`, then upload or attach evidence for review; local paths alone
are insufficient. Keep secrets out of uploaded logs.

```markdown
Local / VM verification

- Tested commit SHA:
- Base revision and integration state (PR head or merge with base):
- Environment: OS, architecture, VM/container image if applicable, compiler/runtime versions:
- Dependencies, build configuration and flags:
- Coverage rationale: changed behavior and risks addressed:
- Exact build/test commands and results (including counts and exit status):
- Omitted checks and why; limitations:
- Evidence: accessible links to logs and applicable checksums, saves, replays or screenshots:
- Maintainer acceptance: sufficient evidence for this revision, accepted by <name>:
```

Evidence must describe the tested source and binaries; reuse built artifacts only
when source, compiler, flags and dependency inputs match. Refresh evidence when
later edits affect tested behavior, dependencies or integration. Fetch current
master before final validation and resolve actual conflicts. Unrelated master
advancement alone does not invalidate evidence; changes in the same components,
dependencies or CI configuration require renewed integration assessment.

Focused coverage does not waive affected simulation determinism, save/load,
replay/network, platform compatibility or simulation-version requirements in
`AGENTS.md`. Local and hosted results may jointly supply that coverage. Hosted
checks may be pending or unavailable when merging; known failures introduced by
the PR still require resolution.

## Hosted verification and regression detection

Draft and ready PRs run cheap contracts and, for ordinary engine changes, a
predictive subset of hosted checks. Documentation and other cheap-only changes
remain cheap. The cheap `music` job covers changes under `tools/music/`, including
its Python tests and a small C++ portable-file round trip using libopusfile; this
job does not establish engine verification. Files inside a soundtrack set,
`data/zik/<set>/`, qualify for engine prediction; `data/zik/SConscript` selects full
verification as build infrastructure.

`ci:run` overrides prediction with affected hosted checks; applicable native
coverage keeps the complete primary GCC 13 suite. `ci:full` requests the full
development matrix, even in draft. `ci:windows`, `ci:android` and `ci:browsers` add
those checks when engine verification is selected. These platform labels alone do
not turn cheap-only changes into engine verification. Becoming ready and label
changes re-evaluate the same selection policy. Removing `ci:run` or `ci:full`
restores the ordinary predictive or cheap-only policy.

The default model is an ExtraTrees failure-ranking model with shared job history,
branch/master context and changed-path summaries. Its score is halved for jobs
whose latest global outcome was failure, trading some persistent-failure coverage
for observed pass-to-fail detection. Scores are ranking signals, not calibrated
failure probabilities or risk guarantees. Global history can cross branches;
pass-to-fail transitions are not proof that a PR caused a regression.

Every retained master push runs the full development matrix regardless of tier
settings. Active runs finish and only the newest pending push remains. Full master
CI detects regressions asynchronously; existing master failures do not restrict
PR merges. Retain failure artifacts, prioritize diagnosis and repair, document
verification in repair PRs and confirm recovery with subsequent full master runs.
Do not require master to become green before other PRs merge.

For focused macOS qualification, dispatch `.github/workflows/ci-macos.yml`
with an exact `revision` and `coverage_profile=full` (or `compatibility` for the
selected compatibility inventory). It runs the same native build, regression,
continuation, scripting and strict generator evidence steps as the reusable
workflow, with a 90-minute job ceiling. This is macOS evidence, not a full
development checkpoint.

Nightly is a fallback with a separate concurrency group. Expensive nightly jobs
are skipped only when a successful full hosted run already covers the exact master
SHA under the current coverage policy and its evidence is available. Missing,
expired, mismatched or inaccessible evidence triggers the full matrix. A skipped
nightly is not a new full checkpoint. Manual and release verification retain their
existing behavior. Local evidence never substitutes for a hosted full checkpoint.

## Predictive pull-request checks

The executable check inventory is defined in
`.github/scripts/ci_check_inventory.py`. It includes native build/regression jobs,
four-way native shards, generator groups, browser jobs and other platform checks.
The model ranks this inventory rather than individual engine assertions. The
compound native/browser equivalence comparison belongs to full/affected coverage;
it is not an independently ranked check with inexpensive prerequisites.

Ordinary PRs select the highest ranked `ceil(budget * eligible_checks)` checks.
Required producers are added before scheduling. Every new check without matching
completed history runs to establish an observation, even if that exceeds the
budget. CI/build/test-infrastructure changes and an unavailable PR diff select
full verification. The selection artifact and summary record the ranked checks,
added prerequisites, unknown checks, model identity, fallback reasons and final
execution inventory. An unselected check is unobserved, not an inferred success.

Repository variables provide these controls:

| Variable | Default | Effect |
| --- | --- | --- |
| `CI_PR_CHECK_BUDGET` | `0.12` | Fraction of eligible logical checks ranked for ordinary engine PRs; valid range `(0, 1]`. |
| `CI_PR_FULL_AUDIT_PERCENT` | `0` | Percentage of PR run/revision identities assigned full coverage by deterministic sampling; valid range `[0, 100]`. |
| `CI_PREDICTIVE_ENABLED` | `true` | Set to `false` to request full PR coverage. |

`ci:full` is the per-PR full-coverage override; `ci:run` requests affected coverage.
These controls do not reduce master coverage. The previous tier-rollout variables
no longer activate predictive selection or gate its budget.

The budget counts logical checks, not runner minutes. A selected browser check can
require a WebAssembly build and native transport build; native shards require
compiled programs. Compilation, setup, aggregate checks, unconditional cheap
contracts and unknown-check warmup can dominate wall time or runner cost. Report
ranked and executed counts separately, and measure queue time, wall execution,
runner minutes and time to result independently before claiming savings. A 12%
ranking budget does not mean an 88% reduction in either duration or cost.

### Model, history and refresh

PR prediction uses Python's standard library and a validated inert JSON forest;
it never loads an executable pickle. The committed fallback assets are
`.github/ci/model.json.gz`, `.github/ci/history.json.gz` and
`.github/ci/model-manifest.json`. The selector first attempts to restore the latest
available `ci-predictor` artifact produced by successful, trusted master runs of
`.github/workflows/ci-model.yml`. Repository identity, master branch, successful
workflow run, source SHA, archive members and SHA256 checksums are verified.
Fork and PR-produced artifacts are not trusted model updates. If restoration fails, the committed seed is used. A missing or
invalid model falls back to branch/recent-failure ranking; missing history makes
checks unknown and runs them for warmup. The selection report records fallbacks.

The two compressed files are runtime inputs: the forest ranks checks, and the
history supplies their features and prevents every check from starting unknown.
The manifest records both file hashes. Together they provide fallback state when
no trusted refreshed bundle is available. The initial model is a bootstrap fit to
completed observed jobs; it was not published by the refresh guard described
below. It used raw historical job names before canonical inventory alias
normalization; current refreshes use the canonical alias view. Its metadata
records those distinctions.

To replace the committed fallback, restore a verified successful master bundle
with `ci_history.py bundle` and confirm that restoration succeeded. Review its model
identity and training metadata, then replace both compressed files together and
copy its `manifest.json` to `.github/ci/model-manifest.json` in the same change.
Normal refreshes update trusted Actions artifacts without modifying these files.

The refresh workflow checks out trusted master, uses read-only Actions/content
permissions, collects completed master build observations, and runs after master
push builds with a weekly/manual fallback. It performs no commits or pushes.
Every refresh also checks the most recent 100 completed master runs for missing
attempts, covering pending refreshes coalesced by GitHub concurrency. An explicitly
triggering run is included even when outside that bounded backfill window. Only
completed success/failure jobs enter history; missing, skipped and cancelled jobs
do not become passing examples. Aggregate and audit jobs are excluded because
they summarize other checks rather than supply independent test outcomes.
Renames may use reviewed job-name aliases from the inventory. Source paths currently describe
the head commit's first-parent diff rather than the entire PR diff.

The trainer uses the same feature constructor as prediction. Job outcomes must
complete strictly before a prediction timestamp to become history, and all labels
for a training run must complete before its fit cutoff. Known-history training
rows receive equal total weight per run. The last chronological fifth forms a
publication guard against the branch-history baseline, measuring failing-run,
observed pass-to-fail-run and individual failing-job recall at 10%, 12%, 25% and
50% budgets. The guard declines sparse evidence, a mixed-objective decrease over
three percentage points, or a failing-run recall decrease over five percentage
points at either the deployed 12% budget or the 25% reference budget. It reports
whether validation used PR observations or pooled events. This guard uses
observed inventories, including historical selection bias; it cannot establish what unrun checks would have done.

When the guard rejects an update, the refresh retains the previous model and
publishes fresh completed history with the rejection reason in `manifest.json`.
An accepted model is refit on all labels available before its cutoff and exported
with feature scales, tree arrays, model identity and training metadata. Training
uses the pinned development-only dependencies in
`.github/scripts/requirements-ci-model.txt`; PR inference requires none of them.

### Selection evidence and completion gate

`.github/scripts/ci_changed_paths.py` writes `ci-selection.json`, identifying
`predictive`, `cheap-contracts`, `affected`, `full` or `nightly-reused` mode. The
artifact includes changed paths, policy/inventory identities, exact matrix inputs
and the selected/dependency check inventory. A reused nightly records its source
run and does not create a new full checkpoint. Native runners retain their
eligible and assigned engine-case inventories; partial shard selection does not
claim full native coverage.

`Relevant checks passed` checks both top-level job results and the exact current
run attempt's paginated jobs API. Every selected leaf and prerequisite must match
one completed successful job. Missing, ambiguous, failed, cancelled or
unexpectedly skipped checks fail the gate. Unselected workflow jobs must be
skipped. Gate success establishes completion of the selected inventory, not full
suite success or acceptance of local evidence.

For explicitly requested affected coverage, `ci_policy.py` retains the boundary
rules: applicable native changes keep the primary suite; simulation/save/AI and
presentation boundaries add relevant platform/browser evidence; networking adds
transport/server/deployment checks. Shared build inputs, shared headers and
unknown affected-policy paths request full coverage. The repeated native
compatibility inventory is maintained in `test/ci-compatibility.json`.
