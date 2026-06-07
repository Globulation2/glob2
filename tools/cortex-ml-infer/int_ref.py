#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
"""numpy integer reference for Cortex I16F16 inference.

Loads a cortex-i16f16-v1 blob (FORMAT.md), runs the integer forward pass
(I16F16 matmul via int64 intermediate >>16, integer ReLU) and the ML_CONTRACT.md
inference rule (wheat-starved clamp -> mask -> argmax ties-to-lowest -> idx+1).

This is the authority the C++ CortexNet must match bit-for-bit. Uses Python ints
(arbitrary precision) for the int64-equivalent accumulation; values are checked to
stay within int64 so the C++ int64_t path is exact.

Constants below mirror glob2/src/ai/cortex/CortexConstants.h. Keep in sync.
"""
import struct

# --- mirrored from CortexConstants.h --------------------------------------
CORTEX_SWARM_WORKER_MIN = 1
CORTEX_SWARM_WORKER_CAP = 7
CORTEX_SWARM_WORKER_CAP_LATE = 12
CORTEX_SWARM_CAP_LIFT_BUILDLEVEL = 3
CORTEX_SWARM_WHEAT_STARVED_TILES = 5
CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP = 1
CORTEX_MAX_BUILDING_WORKERS = 20

MAGIC = 0x434E5831
VERSION = 1
FRAC_BITS = 16
INT64_MIN = -(1 << 63)
INT64_MAX = (1 << 63) - 1


class CortexNet:
    """Loaded I16F16 net: list of (in_dim, out_dim, W_flat, b) layers."""

    def __init__(self, arch, layers):
        self.arch = arch
        self.layers = layers  # each: (in_dim, out_dim, [i32...], [i32...])

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            data = f.read()
        off = 0
        magic, version, frac, num_layers, arch_len = struct.unpack_from("<5I", data, off)
        off += 20
        if magic != MAGIC:
            raise ValueError("bad magic 0x%08X" % magic)
        if version != VERSION:
            raise ValueError("bad version %d" % version)
        if frac != FRAC_BITS:
            raise ValueError("bad frac bits %d" % frac)
        arch = list(struct.unpack_from("<%dI" % arch_len, data, off))
        off += 4 * arch_len
        if num_layers != arch_len - 1:
            raise ValueError("num_layers/arch mismatch")
        layers = []
        for _ in range(num_layers):
            in_dim, out_dim = struct.unpack_from("<2I", data, off)
            off += 8
            n = in_dim * out_dim
            W = list(struct.unpack_from("<%di" % n, data, off))
            off += 4 * n
            b = list(struct.unpack_from("<%di" % out_dim, data, off))
            off += 4 * out_dim
            layers.append((in_dim, out_dim, W, b))
        return cls(arch, layers)

    def forward(self, features):
        """Run the integer forward pass on RAW integer features -> 20 I16F16 logits."""
        # Convert raw int inputs to I16F16 (x << 16).
        acts = [int(x) << FRAC_BITS for x in features]
        if len(acts) != self.layers[0][0]:
            raise ValueError("feature length %d != input dim %d" % (len(acts), self.layers[0][0]))
        last = len(self.layers) - 1
        for li, (in_dim, out_dim, W, b) in enumerate(self.layers):
            out = [0] * out_dim
            for o in range(out_dim):
                acc = 0  # int64 accumulator (Python int; checked below)
                base = o * in_dim
                for i in range(in_dim):
                    prod = (W[base + i] * acts[i]) >> FRAC_BITS  # arithmetic shift
                    acc += prod
                acc += b[o]
                if acc < INT64_MIN or acc > INT64_MAX:
                    raise OverflowError("accumulator out of int64 range: %d" % acc)
                if li != last and acc < 0:
                    acc = 0  # integer ReLU
                out[o] = acc
            acts = out
        return acts


def swarm_worker_cap(max_build_level, free_workers):
    if max_build_level >= CORTEX_SWARM_CAP_LIFT_BUILDLEVEL and free_workers > 0:
        return CORTEX_SWARM_WORKER_CAP_LATE
    return CORTEX_SWARM_WORKER_CAP


def choose_swarm_workers(net, features, max_build_level, free_workers, harvestable_wheat_nearby):
    """Apply the full ML_CONTRACT inference rule. Returns the chosen action (cap)."""
    # 1. Wheat-starved hard clamp (bypass the net).
    if 0 <= harvestable_wheat_nearby < CORTEX_SWARM_WHEAT_STARVED_TILES:
        return CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP

    # 2. Run the net.
    logits = net.forward(features)

    # 3. Mask: valid action k+1 in [WORKER_MIN .. cap].
    cap = swarm_worker_cap(max_build_level, free_workers)
    lo_idx = CORTEX_SWARM_WORKER_MIN - 1  # class index of action == WORKER_MIN
    hi_idx = cap - 1                      # class index of action == cap
    if hi_idx >= len(logits):
        hi_idx = len(logits) - 1

    # 4. argmax over unmasked logits, ties -> lowest index.
    best_idx = -1
    best_val = None
    for idx in range(lo_idx, hi_idx + 1):
        if idx < 0 or idx >= len(logits):
            continue
        v = logits[idx]
        if best_val is None or v > best_val:
            best_val = v
            best_idx = idx
    if best_idx < 0:
        return CORTEX_SWARM_WORKER_MIN  # degenerate: no valid class
    return best_idx + 1


def main():
    import argparse
    ap = argparse.ArgumentParser(description="Cortex I16F16 integer inference reference")
    ap.add_argument("blob")
    ap.add_argument("inputs", help="TSV: 16 features + maxBuildLevel freeWorkers harvestableWheatNearby per line")
    ap.add_argument("out", help="output file: one chosen action per line")
    args = ap.parse_args()
    net = CortexNet.load(args.blob)
    results = []
    with open(args.inputs) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            vals = [int(x) for x in line.split()]
            feats = vals[:16]
            mbl, fw, hw = vals[16], vals[17], vals[18]
            results.append(choose_swarm_workers(net, feats, mbl, fw, hw))
    with open(args.out, "w") as f:
        for r in results:
            f.write("%d\n" % r)
    print("wrote %d actions to %s" % (len(results), args.out))


if __name__ == "__main__":
    main()
