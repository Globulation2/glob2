# PR verification and CI

Select checks from the actual risks, publish reviewable evidence, and distinguish local qualification from hosted CI results.

## CI timing and retained revisions

Pull requests cancel superseded revisions and run cheap contracts by default.
Master finishes active full verification and keeps the newest pending push; all
retained master pushes select the full development matrix. Nightly fallback runs
at 06:00 UTC in a separate concurrency group without publication operations, and
skips expensive jobs only with matching available successful full evidence for
its exact revision and policy.
Release packaging first runs the full development matrix on the exact candidate
revision through `build.yml`'s `revision` workflow-call input.

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

GCC 13 builds its client and applicable transport programs once. Runtime consumers
reuse same-run artifacts with matching source/configuration provenance. Compiler
build jobs publish programs before separate CLI/scripting regressions; native and
browser consumers can start without waiting for those regressions. Golden-only
changes build once per selected platform and distribute programs to sweep consumers.
Primary generator sweeps are complete; secondary platforms retain golden rows and
telemetry equivalence under enabled tiers. Native inventory audits require every
selected engine case to belong to exactly one shard.

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

Draft and ready PRs run the existing cheap contracts by default. Changes under `tools/music/` also run the cheap `music` job (the pipeline's
Python unit tests in a venv from `requirements.txt`), on PRs without `ci:run` too; it
gates no engine verification. Community converter changes also select platform and
stack verification; shared WASM exports/build inputs select full verification
when hosted checks are requested. The music job includes a small C++ portable-file
round trip using libopusfile. Files inside a soundtrack set, `data/zik/<set>/`, select
the native and browser checks when hosted verification is requested, while
`data/zik/SConscript` stays on full CI. Becoming ready
starts no expensive jobs. `ci:run` requests hosted affected checks; `ci:full`
requests the complete development matrix, even in draft. `ci:windows`, `ci:android`
and `ci:browsers` expand requested coverage but do not start verification alone.
Label changes re-evaluate selection; removing the request labels restores
cheap-only selection. The aggregate summary rejects missing, failed, cancelled
and unexpectedly skipped selected jobs, while clearly distinguishing cheap-only
success from engine verification or acceptance of PR evidence.

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

## Tiered pull-request coverage rollout

`.github/scripts/ci_policy.py` records proposed and effective selection, reasons,
changed paths, policy identity and selected command inventory in `ci-selection.json`.
The observation also identifies `cheap-contracts`, `affected`, `full` or
`nightly-reused` verification and the reused run ID when applicable. Native runners
retain eligible/assigned case inventories. Cheap-only PR runs do not enter ordinary
verification performance cohorts; requesting tests on a draft does not exclude
actual verification from those cohorts.

For explicitly requested affected PR verification, primary Linux keeps the
complete applicable native suite. Simulation/save/AI changes add older-GCC and
Windows compatibility cases plus native/browser per-tick and scripting comparisons.
Presentation changes retain software/WebGL and Firefox/WebKit coverage. Network
changes retain transport/server/deployment checks. Android changes retain arm64
builds and x86_64 emulator smoke. Shared headers, dependency/build configuration
and unknown paths select the full development matrix.
`test/ci-compatibility.json` owns repeated native compatibility cases; add suites
there when introducing a portability boundary.

Affected-PR tier reductions remain disabled until a full hosted master/nightly
matrix validates the current policy. `CI_TIERED_COVERAGE_ENABLED=true` activates
them with matching available full evidence; `CI_TIER_BASELINE_RUN_ID` may specify
a preferred baseline. Missing evidence or setting the flag false restores
conservative affected-PR coverage. These settings never reduce master coverage or
start expensive PR checks without an explicit request.
