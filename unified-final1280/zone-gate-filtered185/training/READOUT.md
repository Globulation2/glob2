# Accepted retrospective Guard exclusion and refit

The original dataset remains1,280 planned slots:500normal completions and780
censored games. This is a row-level derivative, not a replacement game tally.

The selected approximation removes49,925 eligible Guard episodes owned by exact
Cabino/Castor/Cortex/Econo/Nicowar/Numbi seats. All1,280 initialmapbytes and actual
map-only/no-external-order receipts were verified. The initial procedural source
starts area masks at zero; only Maxima and Warrush paint Guard zones. Raw painted
zone counts were not recorded, so this is an accepted provenance/AI approximation,
not a reconstruction of tilepaint history. It never tests captured_goal_count0.
All Maxima/Warrush zero-goal records remain, including valid blocked/ineligible
painted zones and unknown empty zones.

Retained eligible rows:237,120, including19,791Guard rows. Original persistent
whole-map splits remain unchanged;55,582rows on315heldoutmaps are compared. The same
format/role/cohort/map/family/collection hierarchy recomputes weights on retained
rows, so original numerical row masses are not claimed unchanged.

| Same retained heldout rows, threshold24 | Previous fit | Filtered fit |
| --- | ---: | ---: |
| Decision regret |2.514794|2.504155|
| MAE |8.199112|8.189618|
| Selected weighted percentage |5.623067|5.568049|
| Weighted calibration decile error |0.645879|0.665056|
| Guard decision regret |0.149955|0.149955|
| Guard MAE |0.986834|1.058042|

Regret improves0.42%, while calibration and GuardMAE worsen; these are small mixed
offline differences, not proof of gameplay dominance or statistical significance.
Three paired CV folds are2.12656→2.13043,2.19017→2.17354,2.49913→2.48148.
Offline threshold scoring applies neither minimum refresh spacing nor age cap.
Production policy remains threshold24/min32/publication8/H128/cap1024.

Planning-only6,734rows are retained for separate reporting and excluded from model
fitting; no unknown-capture rows are present in these exported complete records.
PAIRED-REPORT.json keeps family/format/generator/size/age slices, planning/reachability/
stall outcomes and selected-row support separately from direction-cost labels.
Overlapping future episode observations are not unique whole-game live rates.

The final combined fallback and four experts use the same twelve covariates and
integer head method. Markets remains unmeasured and uses the combined fallback.
No new feature search, target penalty, new collection or duplicate baseline fit was
performed. New headers passed4,240actual Python/C++UBSan cases and42permanent model
tests. Canonical production header was not changed by this staging operation.

Original fullfit source4751d995… is preserved. Compactcandidate46b936c0… and header
d22e1eb7… retain filter identity, exact input/exclusion hashes and whole-slot counts.
reproduce-filtered-fit.py uses permanent fitting APIs and verifies every combined
and family expert head against the selected artifact. It was syntax-checked only;
packaging did not run an additional fit.
