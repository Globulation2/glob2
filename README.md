# Maxima inn 8 / swarm 12 review evidence

This compact export preserves the confirmatory experiment and its measurements, not an experimental feature for the production game. The original campaign contains 3,432 successful games in 1,144 matched three-policy scenarios, from 12 generators and eight fresh seeds. Two rejected two-colony Lava maps blocked 24 planned games; they were not substituted or counted as successful.

Read `reports/final-review/FULL-ANALYSIS.md` and its plot first. The data support a modest staffing guardrail interpretation, not a demonstrated general competitive or starvation improvement. The recorded adjusted-score intervals include zero. Food comparisons use exact common observed exposure across all three arms, with no post-stop extrapolation or hunger-selected headline cases.

## Reproduce the statistical results

From the export directory, run using Python3:

```sh
python3 recompute_scores.py
```

The script imports the **byte-identical frozen worker archive**, independently integrates the exported probability curves using its official scoring functions, checks all 3,432 scores and paired differences, and reproduces all three overall hierarchical bootstrap intervals plus each format's results. `RECOMPUTATION-RESULT.json` records the successful verification.

`data/score-inputs.jsonl.gz` contains per-game results sufficient for official scoring, including exact integer probability summaries, complete probability curves, outcome declarations, alliance identities and endpoint checksums. `data/per-game-values.csv.gz` is a compact review table. Frozen job definitions, seeds, roles, rosters, stopping rules and missing coverage are retained in `protocol/experiment.json.gz` and the report inventories.

The primary metric is `log_time_outcome_v1`, coefficient 2, horizon 90,000 ticks, stopping threshold 950. It is an adjusted trajectory score, not a win rate. Actual engine endings, probability-stop declarations and model-adjudicated tick caps remain distinguishable. Alliance curves are scored once; shared outcome credits are never probability-normalized.

## Food measurements

`reports/food-common-window/` preserves the per-case joint measurements, balanced estimates, exposure/coverage inventory and analysis provenance. JSON and CSV datasets use lossless gzip; decompress them before opening them in ordinary tools. These sufficient measured data allow the reported aggregates to be checked. Raw stdout extraction cannot be rerun from this export because multi-gigabyte raw logs were deliberately omitted. Frozen extraction code is retained under `frozen-scripts/`.

## Source and runtime provenance

`provenance/identity.json` identifies native Linux/GCC15 release bundle `7758e379d2bcdfc64e0f9a42bbadee792c65d679c1dd924bd8701424af04a03b`, simulation revision 121, source commit `726b37f573d2b1401033b5536562c7ee92b4050d` plus its exact experimental dirty source snapshot. The decompressed `frozen-experimental-source.json.gz` SHA256 equals the bundle's declared dirty identity. It contains the tracked patch and complete untracked source contents needed to identify that benchmark build. This historical snapshot is **not** the production PR patch.

The full bundle manifest and byte-identical framework archive are retained. Huge engine/assets bundles, replay/save files, worker caches, databases, raw stdout/stderr and raw development notes are omitted. Statistical reproduction needs none of them. Exact simulation reruns require rebuilding the recorded historical engine/assets from the preserved source identity; this export does not claim to contain a runnable engine bundle.

The generated map transport artifacts are small and retained under `maps/`. `maps/index.json` identifies generator revision, seed, colony count, rotation, hashes and transport encoding, including failures. If transport encoding is gzip, decode that outer layer to materialize the engine `.map.gz` bytes. Do not confuse transport compression with map-file compression.

## Review and cleanup

`EXPORT-VALIDATION.json` records export checks. `INVENTORY.json` lists retained paths, sizes and hashes; run `write_inventory.py` again after adding final-production validation logs. Root will add final revision/platform/save/replay test results separately: the historical benchmark source is deliberately distinct from the focused official-policy revision.

`CLEANUP-INVENTORY.json` is a proposed cleanup list only. Nothing in the worktree, existing campaigns or other `/tmp` paths was deleted by this export. In particular, the currently used `artifacts/maxima-food/native-prefix` must be retained until root relocates it and confirms the build no longer uses it. Keep this export until evidence publication is verified.
