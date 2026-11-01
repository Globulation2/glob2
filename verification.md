# Standard c=2 outcome-aware tournament score

The primary continuous tournament metric is now `log_time_outcome_v1`: c=2 log-time integration over the fixed configured horizon, followed by1/0 credit for declared winners/losers. Probability-based early stops receive full predicted credit and remain explicitly labelled. The950 default and final-probability cap adjudication are unchanged; caps add no terminal tail.

Standard reanalysis, experiment entry points, pooled leaderboards and custom-reader documentation use the new score. Report schema2 carries metric/coefficient/horizon, scored coverage and actual/predicted/cap sources. Pairing and aggregation keep formats, horizons, models and stopping rules separate. Raw engine AUC/mean remain legacy diagnostics. Workers retain a versioned sufficient log integral before dropping histories, so new compact results score identically to full results. Old compact records lacking it remain unavailable.

## Validation

-139 affected Python tests pass, including independent numerical quadrature, earlier-win credit, predicted stopping, shared/allied winners, partial/mixed/legacy coverage, invalid caches and repeated pair labels across horizons/formats.
-310 documentation files checked, zero errors.
-Real main-binary CLI/save integration passes, including full/compact score identity and save-continuation score equality.
-A real probability-stop fixture ends at5122 with horizon6144; credit is1/0 and tagged `probability_stop`. Threshold501 makes the fixture fast; normal tournament default950 is unchanged.
-All672 retained game records/2240 team histories match the independent c=2 formula within3.34e-16. Full and compact scores match exactly. Existing main and secondary reports were not rewritten.
-Actual reanalysis and pooled-leaderboard entry points produce schema2 adjusted scores and explicit coverage/provenance.

This patch is offline tooling and documentation; it changes no engine computation, save bytes, sim revision, replay/network gates or runtime dependencies. Tests ran on Linux/Python3.12; other OS coverage is unavailable. The fixture analyses are validation, not new AI-effectiveness measurements.

## Evidence

- [Commands, source hashes and limits](verification.json)
- [Affected tests](final-affected-tests.log)
- [Retained-data comparison](retained-validation.json)
- [Real early-stop credit](early-stop/score-validation.json)
- [CLI integration](cli/output/verification.json)
- [Reanalysis](final-reanalysis/report.json)
- [Pooled leaderboard](final-leaderboard.json)
- [Exact patch](source.patch)
