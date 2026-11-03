# Engine performance PR validation evidence

This branch contains review evidence for [PR #1367](https://github.com/Globulation2/glob2/pull/1367). It is deliberately separate from the feature branch and maintained documentation.

## Provenance and review

`final/provenance.json` records source revisions, compiler, host, and build commands. `review.md` records the independent subagent review scope, findings, fixes, and limitations. Final Linux validation uses the feature integrated with master `de58ea77c3` (format 194 / simulation revision 122). The feature itself does not change simulation rules or version gates.

## Collection cost

`callgrind/` contains raw before/after profiles, annotated functions, invocation arguments, and game results for the accepted optimized wall-time implementation, before the final PR-review lifecycle/presentation fixes. The baseline source is `0939f2ddfde46a9ab511ab9367ed59ec5560d842`. Both variants advance the same frozen input for 512 ticks, serial execution, CPU gradients, and no export. Inputs are under `inputs/`. Compare `summary: Ir` in raw profiles and the result checksums/workload counters.

| Fixture | Baseline instructions | Optimized instructions | Added instructions |
| --- | ---: | ---: | ---: |
| Early, ticks 0–512 | 963,164,721 | 972,122,402 | 0.93% |
| Later, ticks 16,384–16,896 | 1,915,463,289 | 1,927,307,950 | 0.62% |

These are instruction counts, not CPU cycles or a certified 2% throughput result. Callgrind excludes kernel execution. Native elapsed-time comparisons were noisy and showed larger differences; representative idle-device throughput acceptance remains unverified. These accepted profiles also predate upstream's gameplay changes and are not relabelled as a performance comparison against the newer master.

`prior-profile/history-comparison.json` and raw history profiles isolate the stationary-slot history optimization: 397,600,259 to 109,854,411 instructions, with equal retained totals/coverage. This is a synthetic history comparison, not a whole-game speedup.

## Memory

`final/memory.json` reports 12,532,912 bytes of history (67 slots) and 4,172,256 bytes for four collectors, both foreground/decorative executors with five total timing banks, and a 384 KiB chart/GPU reserve: **16,705,168 bytes**, under 16,777,216 bytes. The native menu lifecycle regression checks the retained executor configuration as well as this bound. Optional detailed tracing has its existing separate diagnostic allocation.

## Coverage limits

Linux native execution and the browser unit suite are covered. Browser GPU tests use doubles; they do not certify physical hardware. Mobile, other desktop operating systems, real browser GPU execution, physical GPU timing accuracy/driver behavior, and the 2% collection-throughput target are not certified by this evidence. Software-renderer screenshots demonstrate real live/results UI behavior; display tests also exercise available Mesa/GL paths.

## Final validation

Final PR revision: `4de09bb0fd` (runtime validation on `ddaeb01588`; the later change only corrects the UI test's render-before-input sequence). Both Linux development and release builds passed. **215 unique native cases passed** across the headless/display reports and one corrected display retry. The original failed report is retained: the test tapped before the plot had been drawn, so its font-dependent hit bounds were not populated. `final/native-summary.json` reconciles the reports; initial matrix status 1 is not hidden.

The browser suite passed 184/184, and export reader round trips passed 3/3. Focused collector/executor checks passed 67 cases/139,274 assertions. Native coverage includes collector/histogram/history behavior, concurrency/causality, real menu lifecycle memory, teardown with diagnostics off, charts including enlarged text, save/load/backward compatibility, replay boundaries, and network setup/version contracts. See the exact filters in `final/run-native.sh` and the passing case names in JUnit.

`final/simulation-verification.json` records matched 64-tick serial/threaded curves at starting ticks 0,8192,16384 and two 64-tick save continuations. `simulation/` contains the replay/checksum streams, outputs and saves. The integrated current multiplayer fixture verified 842 checksums. Linux Mesa display tests passed; these are not a substitute for physical GPU/device coverage.

`screenshots/` contains fresh captures from a real four-AI saved game running the integrated release, at 1280×900 and 640×720. These captures demonstrate layout and availability; their frame times were recorded on a busy validation machine and are not performance benchmarks.
