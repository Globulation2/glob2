# Cached hiring study evidence

This branch contains review evidence only. Implementation: `codex/cached-hiring-study`.

## Corrected eight-player Nicowar match screen

Runtime source e3e56387f; current PR c2fa28a10 adds benchmark documentation only. Live material consumer remains an isolated, non-shipped harness. Attractions/flying-carrier searches remain original. Existing lazy building fields are initialized by a point query; no full-field bootstrap.

Twelve256x256maps (river/islands/swamp ×713/2017/4093/8191), eight Nicowar players, unmodified starting stocks/food/demand, 16384tick openings,4096warmup, n4. Three rotated paired repetitions/map give geometric means: elapsed+0.6%,CPU+6.0%,p95+2.2%. Five maps are faster. Starvation counts188→218; completed constructions1383→1379; population5383→5433. Counts are unique cases, not repeats; they do not establish exposure-adjusted mortality or construction-time gates. Cached policy loses team5 on river2017 and team0 on swamp4093 while avoiding baseline team0 loss on swamp8191.

Post-screen selection: extend islands713 and swamp8191 to10pairs each (seven additional pairs). Islands713 elapsed−15.2% (paired95%CI−17.9–−12.0%),CPU approximately flat; construction146→153,starvation7→5. Swamp8191 elapsed−9.9% (−10.9–−8.9%),CPU−1.7%; construction116→117,starvation43→20. These confirm specific fixed cases, not a general rollout. Broad behavior/safety acceptance remains unestablished; normal game unchanged.

Separate diagnostic owner hiring reductions57–62%, with315–465ms additional capture/preparation/submission per12288measuredticks. Compact per-tick world traces and published-list digests match serial/pool1/4participants on three8192tick cases; original/shadow match three normal native baselines. All final repetitions have stable final checksums/economy for their own policy. MacOSarm64 only; no full-match, cross-platform, live save/replay/network or footage claim.

Files: `ai-match-findings.json`, `ai-match-confirmation.json`, `ai-match-diagnostics.json`, `ai-match-provenance.json`, `ai-match-verification.json`, `ai-match-evidence.tar.gz`, `ai-match-manifest.json`.

The archive contains maps/hashes, isolated sources/generators, exact compile/link argv, commands, accepted raw runs, phase diagnostics, compact traces and superseded attempts. Giant disk-failed detailed dumps and executables remain local; hashes and complete native world traces are retained. Earlier eager-field figures and disk-truncated traces are explicitly superseded/excluded. Older narrow live-fixture performance used the same eager bootstrap; do not generalize that overhead attribution.

Reproduction: use matching release objects from e3e56387f, extract the archive at repository root, run `python3 artifacts/hiring-study/ai-matches/build_trial.py`, then `verify.py`, `measure.py`, `diagnose.py`, `confirm.py`, `summarize.py` and `summarize_confirmation.py` in that directory. Archive already contains output directories; preserve/move them before rerunning scripts, which require fresh directories. `build-eager-superseded.py` exists only to reproduce the excluded mistake. Exact normal compiler/link commands are archived; normal production executable contains no live flag.


- `findings.json`: final paired timing, ordinary-workload results, shadow coverage/agreement and limitations.
- `callgrind-findings.json`: instruction attribution, supplier audit and isolated profiling controls.
- `final-test-results.xml`: 128 passing focused engine tests before master integration.
- `provenance.json`: tested revisions, source correspondence, hashes and integration limitations.
- `measurements-manifest.json`: per-file checksums for the measurement/validation archive.
- `callgrind-evidence.tar.gz`: raw profiles, annotations, commands, packages and paired profiling-control runs.

Download both measurement archive parts into one directory, then reconstruct and extract:

```sh
cat measurements-and-validation.tar.gz.part01 measurements-and-validation.tar.gz.part02 > measurements-and-validation.tar.gz
shasum -a 256 measurements-and-validation.tar.gz
tar -xzf measurements-and-validation.tar.gz
```

Compare the archive hash with `provenance.json`. The archive includes exact commands,
raw repetitions, inputs, traces, replay files, saves, logs and metadata. Identical
files are represented by tar hard links. Executable hashes are retained; executables
are available locally rather than uploaded. Historical performance measurements
apply to the source tree committed as `59ce0a239`, before integrating newer master
changes. These results do not establish live hiring gains, gameplay delay or final
cross-platform compatibility. Original hiring remains authoritative.

## Master-integrated head

`integration-provenance.json`, `integration-test-results.xml` and
`integration-validation.tar.gz` retain the refreshed release build, 152 passing
tests and 24 scheduled plus 18 disabled verification runs at the integrated PR
head. `integration-manifest.json` hashes every result file. Large checksum traces
are omitted from this second archive; their bytes match traces already retained
in the measurement archive, as established by the verification records.
Historical timing ratios were not rerun after integrating master.
`callgrind-original-sources.tar.gz` retains original planner/owner sources from
the profiling container, before its isolated controls.

## Demand-driven refresh update

`demand-findings.json` records results at implementation commit `10a52665c`: rank
only buildings with observed staffing vacancies, including route/supplier capture
skips and defense-only attack capture. `demand-validation.tar.gz` retains exact
commands, ten paired repetitions in two distinct windows, shadow outputs, build
logs, 154 passing tests and 42 verification runs. `demand-manifest.json` hashes
all retained results. Large checksum traces match the historical archives and
are represented here by their hashes. This optimization reduces immediate
reference-trajectory coverage to 41–42%; seven-tick controlled wakeup is not a
live gameplay p95 result. Original hiring remains authoritative.

## Vacancy-sized shortlist controls

Implementation `2cfb2cdbed2b25fda0e7efd091075b4df6c060f2`, over CPU-gradient
base `26715e41b`. `quota-findings.json` contains all measured modes, ten paired
repetitions, confidence intervals, private wait-model results and limitations.
`quota-provenance.json` records exact source/binary correspondence.
`quota-test-results.xml` records 156 passing focused cases.
`quota-validation.tar.gz` contains release executables and hashes, exact commands,
raw opportunities, phase telemetry, controlled input save, scripts, traces, replays,
final saves and verification results. Identical files are tar hard links. Verify
extracted files against `quota-manifest.json` (`archived_files`). Large detailed
replay field dumps remain local except one controlled case; all per-tick world
checksums, replay bytes and final saves are included, with comparison digests and
exact regeneration commands. `quota-implementation.patch` is the
full prototype change over the integrated base.

Published suggestions default to job vacancies plus two extras. The optional
team projection publishes six entries for four vacancies in these controls, but
uses a wider private buffer (aggregate team vacancies plus backups) to distinguish
workers preferred by peers. This is limited to one eligible material per building,
no attraction roles, and aggregate team demand within the configured ceiling.
It is not a generally validated live hiring policy. CPU/elapsed results measure
planner refreshes; the separate direct fill control includes subscriptions and
swaps. Five-tick modeled p95 waits are frozen opportunity-model evidence, not
live gameplay evidence. Retained reference trajectories can have empty outputs
because authoritative hiring fills observed demand before capture.

## Final Callgrind optimization delta

Implementation `dda12cd3be7d7e21296da109a996a99921c78f71`, versus
pre-optimization production planner `2cfb2cdbe`. Benchmark-only helper
`a9f16cde0` is identical in both executables. `planner-final-findings.json`
contains ten alternating paired release samples per control, confidence intervals,
all nine enabled/disabled whole-engine workloads and Linux arm64 instruction totals.
`planner-final-provenance.json` records source, executable and archive hashes.
`planner-final-tests.xml` records the 156 passing focused cases.
`planner-final-evidence.tar.gz` contains raw samples (including warmups/outliers),
exact commands, all per-tick world checksums, replay bytes, final saves, controlled
input, verification comparisons, Callgrind raw profiles, source and self annotations,
and profiling scripts. Verify extracted files using `planner-final-manifest.json`.
Executables remain local with published hashes; build/source links are retained.
Large detailed replay field dumps are omitted except one controlled example.

Complete warmed-rank profiles exclude one warm rank per team. The initial
shortlist-only probe is not used in complete-rank comparisons. Inclusive Callgrind
annotations have recursive/context artifacts; only self instruction counts and
raw total counts support attribution. The final native timing binaries have no
profiling hooks. The profiling container and all builds/tests were stopped before
timing. Planner CPU/elapsed reductions do not establish a live match speedup:
original hiring still runs, and completed-boundary demand can already be filled.

## Net hot-path measurement with isolated live material hiring

`live-hiring-findings.json`, `live-hiring-provenance.json`,
`live-hiring-normal-tests.xml`, `live-hiring-evidence.tar.gz` and
`live-hiring-manifest.json` retain the real-simulation consumer measurements.
The archive contains the isolated replacement source/generator, exact compilation
and linkage, 32 determinism runs, final helper checks, raw rotated paired timing,
separate diagnostics, normal156-case validation and excluded host-loaded attempts.
No live game option, persistence, replay or network support was added. Main source
`e3e56387f` only fixes scheduled planner options to honor pruning's measured default.

Owner-side population scans are removed and controlled vacancy cohorts all fill
with six-tick p95 delay. The hiring-stress scenario improves elapsed31% and CPU5%,
but aborts trips every64ticks and completes no deliveries in the measured window.
The quieter productive service case regresses elapsed24%,CPU20%,p95ticks34%,
and deliveries7.3%. Stress p95ticks also regress44%. Trial gates fail; this is
evidence of an owner-path benefit and unresolved capture/latency/economy costs,
not live-policy qualification or a general match speedup.
