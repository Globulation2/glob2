# Paired H32/H128 observer

Isolated diagnostic research only, native base 4a2cf3dac9eb4eab0aab932307ca5096be953491. One ResourceGradientProbe.cpp TU rebuilt; no header layout, production state, model, threshold, collector contract or original H128 CSV changes. 645 original link object/library inputs and original ELF verified unchanged. The active threshold pilot runtime affd7d2e remains untouched.

Dev binary: `/home/bradley/glob2/artifacts/gradient-unified-20261019/threshold-sweep-gen003/paired-horizon/runtime/bin/glob2`.
Binary SHA256: 425565fbc0222cfc0b0c073f5b6006fd5f8af8c3eba6bf44c4504c2a4f7e26ea.
Patch SHA256: 926cb30db4527f55a148a9aa73436260fe360a1ad28c073bfb4cc8734b069053.
`QUALIFIED-PAIRED-HORIZON.json` is PASS.

Probe ON adds `paired-horizons.csv`. A companion registry keyed by probe instance and stable deque Episode pointer stores only short-window counters and output stream; creation, writes and erasure follow existing episode lifecycle. It reuses existing computed deferred/refreshed direction costs for publication_delay<=age<32, with no additional snapshots, propagation, selectors, RNG or map scans. Off creates no registry/stream/sidecar. Existing H128 aggregates remain the authoritative untouched full-window values.

CSV join identity is capture tick plus family/team/movement/material/key, with serving capture tick and delay. Run/map provenance is external immutable command/input-map hash. Both short/full complete flags are emitted independently; common paired analysis should require full_complete. All episodes, including zero prefixes and negative signed benefit, remain explicit.

Focused qualification: exact original H128 four CSVs; exact world/snapshot/replay/final-save across original ON, paired ON and paired OFF. Extracted exact production accumulator hand-case under UBSan verifies offsets 8..31=24 vs8..127=120 and 7/8/31/32 boundaries. Actual 288tick fixture demonstrates short/full censoring; 320tick demonstrates short-complete/full-censored. An initial assertion assumed the 1024tick fixture contained a full-censored episode; it contained only complete episodes. That fixture-selection assertion failure is retained; the explicit 320tick fixture supplies the missing evidence without rerunning successful proofs.

Historical 72d2f99 source excerpt in `old72d2f99-label-window.txt` verifies old age>=delay8 and age<32. Equal per-tick benefit would give 32*120/24=160, but actual signed benefit ratios can be nonlinear, zero or negative. No equivalent cutoff is inferred here and no mature paired measurements have been launched by this agent. Parent/collector coordinates mature runs after the ongoing threshold pilot. All metadata remains research only, unsuitable for pooling into the production training dataset.
