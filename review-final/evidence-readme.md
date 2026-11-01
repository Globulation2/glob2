# Win probability review and verification

Source: `8a398d3a08807f2179b9e08109aa6be0462cb9ce`.
Integrated base: `80596f1477a1e3736d3b60287a62b6702e13c546`.
PR: https://github.com/Globulation2/glob2/pull/1326

Two independent sub-agent reviews covered engine ownership, persistence and charts,
and Python framework statistics, calibration and documentation. Findings and fixes
are recorded in `review-final/engine-review.md` and `review-framework/verification.json`.
The reviewed Python file hashes still match the final source. The native producer
and runner provenance both match the clean source revision.

## Results

- Native build passed. 53 selected native cases and two display cases passed.
- Full CLI smoke suite: all 18 cases passed, including identical match results and
  checksum traces with compute sizes 1, 2, 4, 8 and auto, experimental catalogs,
  replay/save persistence, default thresholds, invalid inputs and generated help.
- Bounded real-game continuity: 1024 ticks, resume at 512; all 512 resumed per-tick
  checksum records, probability histories and exact integrated areas matched.
- Real-engine compact-result reanalysis produced two nonempty subject/opponent
  probability scores alongside W/L/D, with model, threshold, area and normalized mean.
  This opening-game contract check is not an AI-effectiveness benchmark.
- Tournament suite 54 passed; model suite 26 passed with two optional fitting skips.
  Documentation check 310 documents/exit0; strict translation audit 0 structural errors.
- Simulation revision 109 golden record regenerated and checked against the base.
  Save 190 retains minimum 58, and replay floor 187 remains supported. Formats 188/189
  retain their released Maxima layouts; direct boundary regressions cover them.

Exact commands, results, producer provenance and retained input/output data are in
`review-final/final-verification.json`, JUnit reports, logs and `inventory.json`.
Chart images use a synthetic 16-team statistics fixture at 640x480 and 1024x768;
portrait/landscape touch images verify the live picker excludes post-game probability.
PNG copies of touch BMPs preserve decoded pixels exactly.

## Limits and superseded diagnostics

Broad Python discovery had 112/114 passes and two worker deadlines under shared-host
load. The collection deadline also failed on the unchanged base; reconnect passed
in isolation. Original and control logs are retained. Optional NumPy/SciPy fitting
tests were skipped. This is a Linux fast-development build, not release-performance
or cross-platform determinism evidence. Windows/browser CI was requested and remains
queued; mobile and release behavior were not run locally. The bounded 1024-tick
continuity check replaces the earlier longer debug diagnostic; broad touch gameplay
and 32-phase sampling cases were not repeated at this revision.

The first continuity diagnostic reused a map generated with an unreleased draft
format 188 before master reserved 188/189 for Maxima. Its rejection is recorded in
`review-final/continuity.log.gz`. Current-format 190 map generation and both continuity
runs passed; use `reviewed-continuity-v190/verification.json`. The initial diagnostic
reanalysis produced an empty report for that invalid input; its script now requires
completed input and nonempty scores, and `review-final/reanalyze-v190.log.gz` records
the passing rerun. Superseded results are not counted as successful measurements.

Evidence is bounded to 50 MiB before publishing and 16 MiB per file. Logs, replays and
checksum sidecars are losslessly gzip-compressed; original byte hashes are in the
inventory. No binaries or dependency prefixes are included. The evidence branch is
separate from the source PR and is not intended for merging.
