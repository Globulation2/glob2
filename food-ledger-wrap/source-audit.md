# Food-ledger wrapping invariant

Baseline: 6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf, merged PR 963. Current master fetched before validation: b20563a89; intervening changes touch CLI checksum text portability, a browser replay fixture, and LLVM CI installation, not the food-ledger/snapshot/executor components. No unrelated rebase was performed.

Map::setSize constructs dimensions as 1 << wDec and 1 << hDec and stores corresponding masks. Placement prepares its food input from the observed world dimensions. Standalone callers are more general, and Input width/height are publicly writable, so the candidate checks each axis independently and retains modulo for nonpositive/non-power-of-two or stale-mask dimensions. Mask comparison against current dimension minus one ensures a direct dimension change cannot accidentally use an old mask. Nonpositive dimensions preserve the previous zero result.

The unsigned conversion before masking is defined for negative coordinates. For any representable positive power-of-two int dimension, the masked result lies in [0, dimension), so the conversion back to int is representable. Tests cover INT_MIN, INT_MAX, all representable power-of-two int dimensions, arbitrary dimensions and direct dimension changes.

Masks are local derived input metadata, are copied with Input, and are not serialized. No traversal order, stopping condition, random number use, scheduling delay, save/replay format, golden or SIM_REVISION change is intended.
