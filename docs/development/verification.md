# PR verification and CI

Select checks from the actual risks, publish reviewable evidence, and distinguish local qualification from hosted CI results.

## CI timing and retained revisions

Pull requests cancel superseded revisions and run the [fast gate](#fast-gate); the
full development matrix runs on them only when labelled `ci:full`.
Master pushes run the fast gate, which also saves the compiler and asset caches
pull requests restore. The full development matrix runs on master every four
hours in its own concurrency group without publication operations, and skips
expensive jobs when a successful full run already covers its exact revision and
policy. Running it on every master push kept the hosted runners busy for hours and
left fast-gate runs queued.
Release packaging first runs the full development matrix on the exact candidate
revision through `build.yml`'s `revision` workflow-call input.

The selector keeps the complete commit history and branch/tag refs, while its
checkout uses `blob:none` to fetch historical file contents only when needed.
The selected revision's complete working tree remains available to every check.

The selector runs shared build-system contracts in a dedicated
`artifacts/build-contract-venv`, created with the pinned asset encoder's Python.
It installs `test/build_system/requirements.txt` there, including the asset
requirements and SCons, without modifying the shared encoder environment.

CI measurements batch completed runs hourly using trusted default-branch code and
inert artifacts. Cancellations do not create measurement workflows, and cheap-only
observations are excluded. Requested `ci:full` verification on drafts is included.
Batches retain attempt identities to avoid remeasuring
completed runs and attempt at most ten new measurements per batch. `feedback.json` reports p90 after ten successful matching-inventory
PR samples, with explicit gaps when there are fewer samples. Metrics report queue delay, active execution time, idle
gaps, aggregate runner minutes, feedback time and cache observations separately.
Per-job queue timestamps are estimates, not proof of runner saturation. Overlapping
jobs count once in wall execution time and separately in runner minutes.
Compare ten successful runs with matching event and exact coverage inventory using
`python3 .github/scripts/ci_run_metrics.py --before before.json --after after.json`.
Report workload reductions separately from execution savings. The fast gate's
objective is feedback within 15 minutes on a warm compiler cache; full
verification and releases may take longer. Cheap-contract feedback is not engine verification feedback. Missing
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
Full verification keeps complete primary and secondary generator sweeps; on pull
requests the sweep leaves out the 512-tile cells, which scheduled master runs cover. Narrower
selections (a manual browser-only dispatch, for example) preserve the original
commands and four-way shard ownership. Full native inventory audits
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

## Native localization captures

The manual `build.yml` input `desktop_localization=true` adds the production
translation preview harness to the existing primary Linux, Mac and Windows release builds and
uploads their native PNG/JSON evidence. `ci-macos.yml` accepts the same optional
input for a Mac-only run. All defaults are false; normal native targets,
regression cases and the full master inventory remain unchanged. Dependencies
and game objects use the existing compiler/cache paths. This mode adds the
harness and captures, plus the actual macOS app package described below.

For a focused Windows run independent of the main build workflow, dispatch
`windows-generator-diagnostics.yml` with an explicit final committed revision:

```sh
gh workflow run windows-generator-diagnostics.yml --ref <branch-containing-workflow> \
  -f revision=<final-commit-sha> -F desktop_localization=true
```

This optional mode uses the existing Windows release build and dependencies,
adds the preview harness and production client, checks client startup, and runs
the `FontKerning` and `SystemLanguage` engine suites plus the `UILayout`,
`BidiText` and `TextMetrics` unit suites. It replaces the unrelated generator teardown
execution for that dispatch and uploads `localization-windows` with native
captures, provenance and focused JUnit results. Display-tagged cases remain
unsupported by the Windows test runner; the dedicated capture helper uses SDL's
software renderer. With the input omitted or false, the generator diagnostics
and their inventory are unchanged. This focused result is not a complete Windows
CI checkpoint. Dispatch only after all intended source changes are committed;
the workflow ref must contain this optional mode, while `revision` selects the
exact source checkout to build.

`tools/capture_localization_platform.py` derives its full screen input from the
registered Thai game/online catalogs and retains source, executable, font and
catalog provenance. It captures main/settings/lobby/player settings, gameplay
and the introduction at desktop and explicit 390×844 touch presentation. Four
additional editor views check the localized new-campaign name and an authored
campaign saved and loaded through the real campaign format. Its title, mission
names, map paths and prerequisite identities remain unchanged; native word-break
hints are excluded from newly authored default names. These views use ignored,
isolated profiles and add no shipped campaign fixtures. Four script-editor views also
exercise the actual JavaScript validator with valid and invalid source; technical
diagnostic payloads remain unchanged. Gameplay captures cover all five statistics
topics and their actual history dialogs. Phone statistics panels are explicitly
marked component-only: they use the real session data and production bounds but
do not demonstrate navigation through the touch lens. Captures include a bounded
76-native/20-online (864 measured cases) sample matrix at 10, 13 and 20 pixels and
1×/1.5×/2× raster scale.
Review the actual PNGs before accepting visual quality. These are native-platform
CPU/HarfBuzz/software-SDL captures, not physical-screen, GPU or mobile-OS tests.
The same helper can capture a separately built Linux release harness; keep each
platform and build configuration's evidence distinct. Fixture profiles remain
under the output's `profiles/` and must not be published as review evidence.

The optional macOS localization mode builds the existing ad hoc `bundle`
target in the initial SCons invocation. Its package check verifies actual Thai
and English catalogs, font files and notices, the localized permission purpose,
the signature seal, and packaged CLI startup without inherited build-library
paths. It uses no Apple credentials or notarization. The package report
accompanies the localization captures.

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

## Fast gate

`.github/workflows/ci-fast.yml` runs on every pull request and master push. It is the
check meant to gate merges, with a 15-minute target on a warm compiler cache:

- `contracts`: build-system, runner and tooling contracts, the sim-version check,
  private RNG ownership, header layering, documentation links and vendor
  verification;
- `linux-fast`: a GCC 13 `tools/dev_build.py perf=1` build of both test binaries (with
  a warm cache it costs the same as the dev tier, and the tests run several times faster) and the
  headless test tier (`test/run_tests.py --tier headless`); master pushes save the compiler and encoded-asset caches that pull
  requests restore;
- `master-health`: the master-red rule. A full master run (`build.yml`, push or
  schedule) is judged only by its gating jobs, listed in `GATING_JOBS` in
  `.github/scripts/ci_master_health.py`: the native build and test jobs on each
  platform, Android and the browser build and tests. A failed or timed-out gating
  job makes the run red; a cancelled or missing one leaves it undecided, and
  coverage, sanitizer and evidence jobs never decide it. When the newest decided
  run is red and master has had no green decided run for more than 24 hours, the
  next merge repairs or reverts the breakage and carries the `ci:repair` label.
  The rule is advisory (`ENFORCED = False`: a warning, never a failure) until
  master has stayed green for about two weeks; enabling it is its own pull request.

Its duration and cache hit rates are tracked like any other CI metric; compare ten
runs before claiming a change made it faster.

## Performance lane

`.github/workflows/perf.yml` measures performance and never blocks a merge. Daily,
on demand and on pull requests labelled `perf`, it builds the perf-confirm tier
(`perf=1 opt=3`), runs the benchmark tooling contracts and every `[benchmark]`
native case one at a time, and uploads their per-case durations as the
`perf-trend-<run>` artifact (kept 90 days) for comparing runs over time. A labelled
pull request also builds its merge base with `tools/dev_baseline.py` and adds a
paired `tools/perf_ab.py` table for the benchmark set to the job summary. Quote a
performance claim from a local `perf_ab.py` run of the benchmark that matters;
the lane catches drift and gives evidence, not a merge verdict. Correctness tiers
never run `[benchmark]` cases, and correctness tests assert no wall-clock or CPU
times: a timing that matters is a `[benchmark]` case.

## Hosted verification and regression detection

### CI run cancellation

Workflow `concurrency` groups decide which runs replace or cancel others:

- A newer push to a pull request or a non-default branch cancels that branch's
  in-progress fast gate and full-matrix runs.
- Master pushes, scheduled runs (group `nightly`), manual dispatches (group
  `manual`) and release candidates are never cancelled once started. Within each
  group the newest pending run replaces older pending ones, so not every master
  revision gets a full run.
- `.github/workflows/cancel-superseded.yml` covers what concurrency cannot: runs
  of other CI workflows for a superseded pull request head, and every CI run of a
  closed or merged pull request. It only touches the CI workflows listed in
  `.github/scripts/ci_cancel_superseded.py`, never release, publication or
  deployment runs, and leaves the default branch alone unless a manual dispatch
  asks for it (keeping the newest run of each workflow).
- A scheduled run whose revision already has a decided full run, one that passed
  or failed with complete selection evidence, runs in `nightly-reused` mode and
  skips the expensive jobs. Running the same code again would only queue behind
  other work; cancelled runs decide nothing, so their revision is run again.
  Dispatch `build.yml` manually to repeat a revision on purpose.
- Every job has a `timeout-minutes`, so a hung job ends instead of holding a runner
  until GitHub cancels it.

Pull requests, draft or ready, run the [fast gate](#fast-gate). The full
development matrix in `build.yml` runs on master every six hours,
release candidates (through its `revision` workflow-call input) and manual
dispatches. It does not run on ordinary pull requests: every check in it builds
the game, so a cheaper subset still pays for the builds that dominate its cost.

To run the full matrix on a pull request, add the `ci:full` label.
`.github/workflows/ci-full-label.yml` then calls `build.yml` for the PR head
revision, and runs it again for each later push while the label stays. Adding
another label does not start or cancel a run; removing `ci:full` stops later runs.
A `ci:full` run is verification of that revision, never a master checkpoint.

Inside `build.yml`, `.github/scripts/ci_changed_paths.py` writes
`ci-selection.json` with its mode (`full`, `affected`, `cheap-contracts` or
`nightly-reused`), changed paths, policy and inventory identities, exact matrix
inputs and the selected and prerequisite checks. Master, nightly, dispatched and
called runs select `full`; a manual browser-only dispatch selects the affected
browser, transport and deployment checks. `ci_policy.py` keeps the path rules that
tell cheap tooling changes from engine changes and choose affected boundaries:
simulation, save and AI paths add platform and browser evidence, networking adds
transport, server and deployment checks, and shared build inputs, shared headers
and unknown paths select everything. The cheap `music` job covers `tools/music/`;
files inside a soundtrack set, `content/data/zik/<set>/`, are engine inputs and
`content/data/zik/SConscript` is build infrastructure. The repeated native
compatibility inventory is maintained in `test/ci-compatibility.json`.

`Relevant checks passed` checks both top-level job results and the exact current
run attempt's paginated jobs API. Every selected leaf and prerequisite must match
one completed successful job. Missing, ambiguous, failed, cancelled or
unexpectedly skipped checks fail the gate, and unselected workflow jobs must be
skipped. Native runners retain their eligible and assigned engine-case inventories.

The scheduled full matrix covers master's newest revision every six hours. Full master CI detects regressions
asynchronously; the fast gate's `master-health` job applies the master-red rule
described above. Retain failure artifacts, prioritize diagnosis and repair,
document verification in repair PRs and confirm recovery with subsequent full
master runs.

For focused macOS qualification, dispatch `.github/workflows/ci-macos.yml`
with an exact `revision` and `coverage_profile=full` (or `compatibility` for the
selected compatibility inventory). It runs the same native build, regression,
continuation, scripting and strict generator evidence steps as the reusable
workflow, with a 120-minute job ceiling. This is macOS evidence, not a full
development checkpoint.

Nightly is a fallback with a separate concurrency group. Expensive nightly jobs
are skipped only when a successful full hosted run already covers the exact master
SHA under the current coverage policy and its evidence is available. Missing,
expired, mismatched or inaccessible evidence triggers the full matrix. A skipped
nightly is not a new full checkpoint. Manual and release verification retain their
existing behavior. Local evidence never substitutes for a hosted full checkpoint.
