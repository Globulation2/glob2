# Echo/Nicowar continuation investigation

Source: `5b667a936` on `codex/echo-save-continuity`, based on merged farming master `802e6687d`.

## Cause

Echo/Nicowar stored pending building orders but discarded its shared AI gradient manager at load. This loses cached distance values, field ages and the ordered refresh queue. Building eligibility directly consults whether those fields are ready.

The retained g1 trace has 89 identical post-checkpoint orders, including Maxima zone edits. In uninterrupted execution the last required gradient refresh is complete at6169 and the building order is emitted at6170. Reloaded execution still has that field queued at6169, finishes at6170 and emits the same request at6171. Diagnostic logs retain the field ages, queue and pending building id. Both diagnostic runs match their uninstrumented parent traces.

The new end-to-end regression fails on the old binary at6171 and passes with the fix. The second previously failing v117 continuation at7808 also passes after preserving the manager. This is AI planning-cache continuation, not an engine-gradient invalidation change.

## Fix and compatibility

Format119 writes exact cached grids, ages, queue order and manager scheduling fields. Multiple Echo players save the shared manager once, using the earliest serialized AI player as its owner; later players reattach to it. The existing update-owner flag remains separate, so the first serialized player need not be the controller updating the manager. Previous construction id, fruit observation and initialization timer are also preserved.

Format58 remains the minimum. Formats115/116/117/118 load in the retained scenarios; a new save made after loading them preserves the reconstructed runtime state. Historical caches absent from older saves cannot be recovered. Network/YOG admission moves to42 for saved-game state transfers; replay minimum99 is unchanged. No per-tick checks are added, and fresh/uninterrupted outcomes remain unchanged in the retained comparisons.

## Verification

Eight scenarios: fresh Maxima/Nicowar on g1/g52, Nicowar/Nicowar, Econo/Nicowar, crowded Rugged Isles, and loaded v115/v117/v118 saves. Full per-tick sidecars match macOS/Linux for43520 initial ticks and21760 resumed ticks. All eight save/reload continuations pass. The original five uninterrupted traces match23040 detailed team/entity ticks; loading the retained v118 save preserves its previous reconstructed continuation for4096 ticks.

The checked-in arena regression independently checks two successive reloads at4096 and6144 for Maxima/Nicowar and Nicowar/Nicowar. The manager harness checks binary/text values, stale ages, uncomputed fields, duplicate queued entries, invalid queue indices, sharing with a distinct updating controller and a human slot retaining an old AI object. AI portability, team-stat/save/replay boundaries and real network-version admission tests pass on both platforms. Windows execution is delegated to CI and was not run locally.

Sample saved-file growth: g1 +295229bytes(4.37%), g52 +393339bytes(1.74%), crowded +393348bytes(0.21%), v117 continuation +1311061bytes(5.33%). These are storage deltas, not a CPU benchmark.

## Reproduction and retained artifacts

Production regressions and the compressed arena are in the source PR. This evidence contains diagnostic logs/scripts, platform summaries, per-tick hashes, compatibility logs and example format119 checkpoints. `validate.py` retains the broader scenario commands; adjust host paths for another checkout.

Older maps/saves used by the broader scenarios are retained in the [previous immutable farming evidence](https://github.com/Globulation2/glob2/tree/110431475c76c4801acf64d6b98da5a1aace4ea6/final-policy/inputs). Decompress them before running. Complete binary checksum sidecars remain in the local/therig validation directories; attached per-tick hashes and inputs allow reproduction without committing large transient dumps to the source branch.
