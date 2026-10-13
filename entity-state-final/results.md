# Reviewer evidence inventory and final layout report

Integrated source base: b4c463a439953059c9ac058902cff8a652e87f02. The seven-label
release screen uses source manifests based on candidate5f3f6bd37048dc0adb90ad87db95669875ab850c;
manifest hashes, exact commands and binary SHA-256 identify each actual experiment.
Builds use the recorded native release commands (`dev_fast=0 release=1`, ordinary
compilation) and the same pinned native dependencies. Exact commands and executable
hashes are in binaries.json; topology.txt records the host CPU/core mapping.
Selected: **compact-original** — audited compact BuildingStateRecord/Lifecycle
conversions, with baseline Building.h ordering. Whole-match qualification uses the
completed240-run matrix. Rendering and further comparisons are skipped at the
user's explicit direction; no claim of final rendering performance is made.
Earlier results against277510840 are superseded for current integration claims.

## Provenance and acceptance

Archive copies can inherit a Git-revision string from the surrounding checkout or
cached generated inputs. Embedded revision text alone does not identify their
production source. Use each `*-source.json` file's per-file hashes together with
`binaries.json` and exact command/results records. Publish those manifests and the
actual final tested commit/base SHA; do not attribute every binary to a single
embedded Git string. Experimental layout patches and diagnostic adapters belong
only in evidence, not the shipping tree.

User criterion: reduce data size provided total CPU does not regress. Inventory
microbenchmarks explain costs; they are not an independent rejection gate and
must not be summed into a fictitious whole-match estimate. Report absolute process
CPU including all threads, paired bounds, storage savings and each workload's
limitations. Preset component/whole tolerances describe practical uncertainty,
not permission to call a possible slowdown proven absence of regression. Final choice uses the completed ordinary, colony and shared-runtime whole-match
rows with one/four workers. The user waived rendering and further comparisons.
Independent confirmation and final-head performance remain unavailable, not assumed.

## Compiled building layouts

All seven records/live objects/views have natural alignment8 on this toolchain.
UnitState360→264, UnitView384→288 and live Unit440→344 save96bytes each wherever
unit compaction is enabled. Baseline and units-only retain the original building
layout. Four space-saving building candidates save16bytes/record/view/live object;
the padded control intentionally restores those sizes.

| Variant | State | View | Live | State stock | State demand | Live type | Live owner | Live materials pointer | Live reservations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline | 352 | 408 | 1960 | 120 | 180 | 648 | 656 | 664 | 368 |
| units | 352 | 408 | 1960 | 120 | 180 | 648 | 656 | 664 | 368 |
| compact-original | 336 | 392 | 1944 | 108 | 168 | 632 | 640 | 648 | 352 |
| compact-near-bindings | 336 | 392 | 1944 | 108 | 168 | 352 | 360 | 368 | 376 |
| stock-offsets-preserved | 336 | 392 | 1944 | 120 | 180 | 632 | 640 | 648 | 352 |
| stock-first-near-bindings | 336 | 392 | 1944 | 0 | 60 | 352 | 360 | 368 | 376 |
| compact-padded-control | 352 | 408 | 1960 | 108 | 168 | 648 | 656 | 664 | 368 |

Authoritative offsets are compiled standard-layout probes (`*-offsets.txt`);
live offsets use real allocated member addresses from the harness. Allocator usable
size and modulo64 address histograms are recorded per run. The sampled1960-byte
objects sometimes occupy1976 usable bytes; compact objects sampled1944 usable bytes.
Allocation/address distributions describe these fixtures and allocator, not a
portable cache guarantee. Neither matching size nor changed placement proves causality.

## Completed release screen

Ten rotated paired repetitions, private/shared modes, fixed baseline-derived counts,
fixed affinity and process CPU. Intervals are pointwise; they are not simultaneous
seven-layout coverage bounds. No independent final confirmation was run at the user's direction; selection
optimism remains a limitation rather than an already confirmed result. The140 completed rows have identical canonical
saves within each inventory mode across every variant/pair. The standalone layout
smoke has8rows/fourlabels; its subset coverage is explicit.
The screen's canonical comparisons cover all seven labels regardless of that
smaller standalone smoke inventory.

Changes below compare compact candidates with units-only, separating unit savings
from incremental building layout effects. Values are process-CPU percent estimate
[two-sided95% paired log-ratio bootstrap bounds].

| Variant | Mode | Capture | Inventory read | Inventory update |
| --- | --- | ---: | ---: | ---: |
| compact-original | private | -4.50 [-6.93, -2.30] | +12.31 [+11.40, +13.01] | +18.43 [+1.79, +53.69] |
| compact-near-bindings | private | -2.32 [-5.10, +1.39] | +11.64 [+9.54, +14.19] | -5.30 [-11.24, +0.57] |
| stock-offsets-preserved | private | -2.50 [-4.60, +0.41] | +13.05 [+11.36, +15.31] | +1.32 [-2.49, +7.24] |
| stock-first-near-bindings | private | -2.45 [-4.44, -0.22] | +14.18 [+12.62, +15.86] | -0.86 [-3.06, +2.05] |
| compact-padded-control | private | -1.31 [-3.18, +0.82] | +12.16 [+11.15, +13.36] | +1.35 [-1.21, +4.70] |
| compact-original | shared | -3.05 [-5.29, -1.59] | +11.00 [+7.68, +13.67] | +1.09 [-1.06, +3.68] |
| compact-near-bindings | shared | +0.17 [-2.77, +3.11] | +10.25 [+8.06, +12.01] | +0.91 [-0.53, +2.93] |
| stock-offsets-preserved | shared | -0.20 [-4.07, +4.04] | +12.90 [+9.75, +15.36] | +1.17 [-1.81, +4.68] |
| stock-first-near-bindings | shared | -0.31 [-3.91, +3.50] | +13.88 [+10.55, +16.44] | +3.47 [-0.08, +7.33] |
| compact-padded-control | shared | +3.09 [-0.63, +7.09] | +12.08 [+9.31, +14.31] | +1.67 [-0.59, +5.32] |

Private read Callgrind retired instructions are5,997,701 for every label. Native
read CPU is roughly10–14% higher in the compact layouts versus units-only. Nearby
bindings and stock-first designs reduce simulated private-read D1 misses by about13%,
while native CPU remains worse. Cache simulation does not predict or establish the
cause of this slowdown; this evidence rejects a simple “fewer simulated misses
must be faster” explanation. Hardware effects, generated instruction displacements,
code placement, frequency/host conditions and allocation placement remain possible
contributors rather than demonstrated explanations.

## Compatibility and existing baseline failures

Explicit serialized widths/order, ability IDs, enum gaps/sentinels and checksum
order stay unchanged. Narrow loads validate wide values before conversion. The save
floor remains58 and format remains158. Performance-base SIM_REVISION53 and final
incoming-master SIM_REVISION54 are master behavior changes, not representation
version bumps. Never remove accepted historical continuations
merely because they appear derived. No arbitrary content limits, movement adoption,
GUI/headless representation split or new exceptional-material path is introduced.

Four focused failures reproduce on the current native baseline; their exact commands,
binary hash and logs are in baseline-four-results.json. These are unresolved base
limitations, not silently counted as candidate passes:
- JavaScriptSimulation: JavaScript original fixture executes, resumes and replays 256 ticks (exit1).
- JavaScriptSimulation: JavaScript economic planners and map survey execute, resume and replay 256 ticks (exit1).
- LegacyRoundTripSave: a format-143 save with live round-trip fields plays on greedily [golden][save-format] (exit1).
- UnitCatalog: both header forms retain embedded unit properties and experiment gates (exit1).

The two JavaScript cases disagree with stored expected traces; the legacy143 roundtrip
fails its resumed-checkpoint comparison; the UnitCatalog case throws an oversized
collection error. Retain logs and distinguish baseline failures from new regressions.
Cross-platform execution is still a separate requirement; Linux/GCC layout evidence
is not proof of Windows/macOS/browser/mobile simulation equivalence.

## Completed whole-match process CPU and wall results

240 runs: four variants × three fixtures × one/four workers × ten rotated pairs.
Each run warms1024ticks then measures16384ticks. CPU includes all process threads.
The table below reports arithmetic mean absolute costs in microseconds/tick and
paired geometric-mean percentage changes with pointwise two-sided95% bootstrap
intervals. The absolute mean ratio and paired log estimate need not be identical.

| Fixture | Workers | Baseline CPU µs/tick | Selected CPU µs/tick | CPU change [95%] | Baseline wall µs/tick | Selected wall µs/tick | Wall change [95%] |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ordinary | 1 | 238.823 | 236.979 | -0.83 [-2.69, +1.13] | 238.857 | 236.990 | -0.84 [-2.70, +1.13] |
| normal-colonies | 1 | 1363.093 | 1371.937 | +0.63 [-3.24, +4.90] | 1363.403 | 1372.523 | +0.66 [-3.21, +4.91] |
| shared-runtime | 1 | 433.763 | 433.122 | -0.10 [-2.20, +2.57] | 433.959 | 433.313 | -0.10 [-2.20, +2.57] |
| ordinary | 4 | 354.413 | 347.497 | -2.00 [-6.43, +2.35] | 159.236 | 155.319 | -2.45 [-4.63, -0.19] |
| normal-colonies | 4 | 1642.067 | 1653.092 | +0.73 [-2.12, +3.80] | 702.335 | 699.983 | -0.27 [-3.51, +3.29] |
| shared-runtime | 4 | 651.645 | 629.899 | -3.63 [-7.06, -0.63] | 340.293 | 334.590 | -1.86 [-6.31, +2.52] |

Selected CPU estimates span−3.63% to+0.73%. Only shared-runtime/four-worker CPU
shows a negative interval excluding zero (−3.63%,95%−7.06…−0.63). Ordinary/one
worker's upper bound is+1.13%; other selected rows permit roughly+2.35…+4.90%
slowdown, exceeding the predeclared2% whole-process uncertainty bound. This matrix
therefore establishes storage reduction and reports mixed/inconclusive CPU effects;
it does **not** prove general speedup or universal absence of regression.

The nearby-bindings variant regresses ordinary/four-worker CPU by+3.79%
(95%+0.43…+8.03), despite favorable simulated cache counts. Compact-original's
corresponding estimate is−2.00% (95%−6.43…+2.35); it retains baseline live member
ordering and avoids the additional pointer/reservation reorder. Stock-first and
stock-offset-preserved remain exploratory screen layouts, not whole-match-qualified
shipping choices. The padded control offers no building byte saving and is omitted.

## Integrated head and limits

Final integrated source is5750ba72963a494e1f61873ca3663175cc7f203f, including current
master35b84d4ffd774742ada41d33c39ab1a50551f113. **Performance comparisons were built
against b4c463a439953059c9ac058902cff8a652e87f02**, using the published5f3f6bd-based
source manifests and binary hashes. The later incoming changes overlap worker/
statistics execution and advances SIM_REVISION53→54; timings have not been repeated on5750ba7296 at the user's
request. Do not label earlier numbers as a final-head benchmark. Final integration
functional checks belong to the parent's PR validation record.

Rendering performance/screenshots on the final head are skipped at user request.
No new native comparisons, independent confirmation or platform runs were started
for this finalization. The240 completed whole rows and prior screening data remain
available without suppressing unfavorable candidates or uncertainty.

## Evidence index

- protocol.json: predeclared paired design, practical bounds, worker/affinity rules.
- binaries.json and seven *-source.json files: executable/source provenance.
- seven *-offsets.txt files: compiled state/live/view size and state offsets.
- screen/comparison.json plus run logs/canonical saves:140 native screen rows.
- whole-comparison/comparison.json:240 whole runs, exact commands, absolute CPU/wall,
  per-pair executable/input hashes, saved outcomes and confidence summaries.
- profile-summary.json, Callgrind/Cachegrind logs and dumps: attribution diagnostics.
- layout-smoke/results.json: standalone allocation/snapshot/canonical smoke.
- baseline-four-results.json and baseline-failure-0…3.log: reproduced baseline failures.
- ../layout-candidates/: unselected definitions and experiment rationale.
- field-decision-inventory.md: current field-level width/compatibility decisions.
- review-report-input-hashes.json: exact report input snapshot.

Publish these evidence paths on an immutable evidence commit; local paths alone
are insufficient reviewer access. Record final integration tests separately with
5750ba7296 and master35b84d4ff; preserve the performance-base distinction.

## Final review cleanup

Selected production consists of compact state/validated loads and baseline live
building ordering. Architecture/persistence guides should describe that exact
choice. Keep unselected layouts, padding, source generators and temporary baseline
adapters only on the evidence branch. Retain useful logical/snapshot tests and
ordinary width-safe editor APIs in production. Before merge, record final-head
functional results, existing baseline failures, platform omissions and immutable
review evidence links. Rendering and new comparisons are explicitly waived rather
than silently presented as passed.
