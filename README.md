# Portage Lakes validation evidence

Frozen validation evidence for the Portage Lakes generator and handbook additions. The implementation PR assigns **ID 65**; this pre-integration corpus uses provisional **ID 58**. Use the string ID `portage-lakes` to generate maps on the integrated branch. The saved terrain/game fixtures can be loaded directly; do not regenerate them by numeric ID 58 on current master.

- [Gallery](gallery.html), with native PNGs, request reports and downloadable maps.
- [Reliability](reliability-final.md): 2,000 randomized requests pass, including all four former failures. Raw rows are in `random-r19.jsonl.gz`.
- [Control comparison](final-control-analysis.md): all 777 control and 436 envelope requests pass; raw rows in `controls-r19/ablation.jsonl.gz` and `envelope-r19.jsonl.gz`.
- [Review rounds](REVIEW.md), [final map review](review-r19.md), and [translation review](translation-final-review.md).
- `game-summary.json` and `game-summary-r19.json`: 18 final-layout AI games, with raw logs and result JSON. Earlier superseded narrow results remain explicitly labeled in the historical summary. Three representative games include both initial and final saves; other game directories contain reports and logs only.
- `performance-r19.jsonl`, actual sampling profiles, and paired optimization results document the largest CPU benchmark reduction from18.90 to3.94 seconds. Wall times include machine contention.
- `compatibility.json`, both complete compressed6,000-tick checksum traces, and `save-load-continuity.json` establish the recorded Mac/Linux and initial-save continuation checks.
- `provenance.json` retains pre-integration source/build identities. The source snapshot differs from integration in its reserved numeric ID. `MANIFEST.json` hashes every file in this evidence commit.

All production source is in the implementation branch. Scripts assume the repository working directory and original local `artifacts/portage-lakes` layout; compiled binaries are not bundled here. Gzip files are lossless and can be decompressed normally. Human balance/playtesting and native-speaker certification for every language are not claimed. Crowded narrow maps retain late-game AI starvation in some rotations; see the final review rather than treating successful generation as proof of balance.
