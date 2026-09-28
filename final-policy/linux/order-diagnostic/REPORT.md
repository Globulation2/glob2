# Candidate g1 save/resume order diagnostic

Both diagnostic simulations ran only after the quiet benchmark's `results.json` existed. They used the already-built candidate, the same g1 map/game seed, checkpoint at4096 and stop at6200, with replay recording enabled. No source changes or builds were made.

The replay-enabled uninterrupted run matches the original candidate run for all6200 detailed team/entity tick records. Resumed execution reproduces the original first entity difference at6171.

The first89 emitted orders after the checkpoint match exactly in tick, player, type and payload. This includes preceding forbidden/clearing-zone edits. The first difference is **Nicowar player1's building creation timing**:

| Order | Uninterrupted tick | Resumed tick | Payload |
|---|---:|---:|---|
| CREATE |6170|6171| team1, position(9,28), building type26, one worker; identical bytes |
| MODIFY_BUILDING |6172|6173| building gid1028, six workers; identical bytes |
| SET_ALLIANCE from Maxima player0 |6176|6176| identical bytes |

Thus there is no earlier differing emitted Maxima protection-reconciliation order in this reproduction. The first observable order divergence is a one-tick delay in Nicowar construction, followed by its worker assignment. This localizes the symptom; it does not by itself identify the exact internal field responsible.

A strong diagnostic target is Echo's unsaved GradientManager: its cached AI gradients, ages and queued refreshes are recreated after load, and BuildingOrder::passes_conditions consults gradient freshness before emitting construction. `previous_building_id` is also omitted from continuation state, though resetting it generally relaxes rather than delays the gate. These omissions predate the current changes. Untouched master's matching run means this particular failure is not reproduced there; the changed candidate trajectory could expose a latent issue, so do not call attribution proven.

Next narrow step, if authorized: log the pending building order, each constraint's is_updated result, relevant gradient ages/queued entries and previous_building_id around6167–6171 in both arms. Do not change engine-gradient save behavior on the basis of this evidence alone.

`comparison.json` retains the first-difference context. `full/` and `resumed/` retain commands, replay files, decoded orders, logs and checksum traces. `trace.py --parse-only` reparses retained remote files without rerunning games. The parser accepts exactly one replay-body offset whose complete framed stream validates through its terminal NullOrder at6200; checksum words are excluded from order comparison. Relative replay steps are converted to absolute game ticks using the initial0 or resumed4096 start.
