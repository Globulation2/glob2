# Separate final-cohort profile cross-check

This analysis uses 19,948 games in 9,974 complete pairs at d37c0c353, kept separate from the earlier 20,000-game version. The 52 omitted scheduled outcomes and missing-result selection limitation are documented in the ratings evidence branch. It does not measure newer Maxima changes #370/#371.

Re-run `python3 analyze.py --repository <glob2-checkout>` here. The script freezes generator tags to the measured source and runs 2,000 within-opponent/build paired permutations with joint max-T correction across all map and tag comparisons.

Warrush's river association remains negative (-9.3 percentage points after opponent/build adjustment), supporting its existing broad terrain weakness. Cortex again benefits on wide-open terrain (+5.6 points), but its profile deliberately discusses coordinated waves and support instead of naming favorable maps. Maxima shows an ocean association (-6.7 points); this is not added to the game because newer master fixes affect island/pool behavior and were not measured here. The remaining strategic descriptions are supported by implementation review, not causal conclusions from map correlations. No in-game wording change is warranted by this check. Counterplay remains advice to try, not measured human effectiveness.
