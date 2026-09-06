#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
"""Parity harness: prove the numpy integer reference and the C++ CortexNet agree
bit-for-bit on the I16F16 inference.

Steps:
  1. Generate a deterministic random cortex-mlp-f32-v1 net JSON.
  2. Quantize -> I16F16 blob (quantize.py logic, imported).
  3. Generate ~N random RAW integer feature vectors + masking inputs.
  4. Run int_ref.py to get the chosen action per input.
  5. Build + run a standalone C++ runner that loads the SAME blob and inputs and
     emits its chosen actions.
  6. Compare: must be identical for ALL inputs.

All scratch lives in glob2/.tmp (NEVER /tmp). Run from anywhere; paths are
resolved relative to this file.
"""
import json
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import quantize  # noqa: E402
import int_ref  # noqa: E402

GLOB2 = os.path.normpath(os.path.join(HERE, "..", ".."))
TMP = os.path.join(GLOB2, ".tmp")
ARCH = [16, 32, 32, 20]
SEED = 0xC0FFEE
N_INPUTS = 1000


def gen_f32_net(seed):
    rng = random.Random(seed)
    layers = []
    for li in range(len(ARCH) - 1):
        in_dim = ARCH[li]
        out_dim = ARCH[li + 1]
        # Modest magnitudes so logits stay well within I16F16 / int64 range while
        # still exercising sign, ReLU cutoffs, and close ties.
        W = [[rng.uniform(-1.5, 1.5) for _ in range(in_dim)] for _ in range(out_dim)]
        b = [rng.uniform(-2.0, 2.0) for _ in range(out_dim)]
        layers.append({"W": W, "b": b})
    return {
        "format": "cortex-mlp-f32-v1",
        "arch": ARCH,
        "activation": "relu",
        "input_features": [
            "corn", "maxCorn", "maxUnitWorking", "unitsInside", "maxUnitInside",
            "nearestWheatDist", "harvestableWheatNearby", "freeWorkers", "totalFree",
            "totalNeeded", "workers", "swarmCount", "feedCapacity", "starvingUnits",
            "needFood", "maxBuildLevel",
        ],
        "normalization_folded": True,
        "layers": layers,
    }


def gen_inputs(seed, n):
    """Return list of (features[16], maxBuildLevel, freeWorkers, harvestableWheatNearby).

    The masking inputs share the SAME values as the corresponding feature slots
    (maxBuildLevel == feature[15], freeWorkers == feature[7], harvestableWheatNearby
    == feature[6]) so the rule sees consistent data, as it will in the sim path.
    Plausible ranges per ML_CONTRACT feature semantics.
    """
    rng = random.Random(seed)
    rows = []
    for _ in range(n):
        corn = rng.randint(0, 20)
        max_corn = rng.randint(10, 20)
        max_unit_working = rng.randint(0, 20)
        units_inside = rng.randint(0, 8)
        max_unit_inside = rng.randint(1, 17)
        nearest_wheat = rng.randint(0, 100)
        # harvestableWheatNearby: bias toward the starved range AND the normal
        # range so both inference branches get heavy coverage. Occasionally -1
        # (unknown -> not starved) to exercise the >= 0 guard.
        roll = rng.random()
        if roll < 0.1:
            harvestable = -1
        elif roll < 0.4:
            harvestable = rng.randint(0, 4)   # starved branch
        else:
            harvestable = rng.randint(5, 30)  # net branch
        free_workers = rng.randint(0, 30)
        total_free = rng.randint(0, 60)
        total_needed = rng.randint(0, 60)
        workers = rng.randint(0, 60)
        swarm_count = rng.randint(0, 8)
        feed_capacity = rng.randint(0, 200)
        starving = rng.randint(0, 30)
        need_food = rng.randint(0, 1)
        max_build_level = rng.randint(0, 3)
        feats = [corn, max_corn, max_unit_working, units_inside, max_unit_inside,
                 nearest_wheat, harvestable, free_workers, total_free, total_needed,
                 workers, swarm_count, feed_capacity, starving, need_food,
                 max_build_level]
        rows.append((feats, max_build_level, free_workers, harvestable))
    return rows


def main():
    os.makedirs(TMP, exist_ok=True)
    json_path = os.path.join(TMP, "cortex_parity_net.json")
    blob_path = os.path.join(TMP, "cortex_parity.blob")
    inputs_path = os.path.join(TMP, "cortex_parity_inputs.txt")
    ref_out_path = os.path.join(TMP, "cortex_parity_ref.txt")
    cpp_out_path = os.path.join(TMP, "cortex_parity_cpp.txt")
    runner_src = os.path.join(GLOB2, "src", "ai", "cortex", "CortexNetParityMain.cpp")
    runner_bin = os.path.join(TMP, "cortex_parity_runner")

    # 1. random f32 net
    net = gen_f32_net(SEED)
    with open(json_path, "w") as f:
        json.dump(net, f)
    print("[1] wrote random f32 net -> %s" % json_path)

    # 2. quantize -> blob
    blob = quantize.quantize(net)
    with open(blob_path, "wb") as f:
        f.write(blob)
    print("[2] quantized -> %s (%d bytes)" % (blob_path, len(blob)))

    # 3. random inputs
    rows = gen_inputs(SEED + 1, N_INPUTS)
    with open(inputs_path, "w") as f:
        for feats, mbl, fw, hw in rows:
            f.write(" ".join(str(x) for x in feats))
            f.write(" %d %d %d\n" % (mbl, fw, hw))
    print("[3] wrote %d input rows -> %s" % (len(rows), inputs_path))

    # 4. numpy reference
    netobj = int_ref.CortexNet.load(blob_path)
    ref = [int_ref.choose_swarm_workers(netobj, feats, mbl, fw, hw)
           for feats, mbl, fw, hw in rows]
    with open(ref_out_path, "w") as f:
        for r in ref:
            f.write("%d\n" % r)
    print("[4] numpy reference computed %d actions" % len(ref))

    # 5. build + run C++ runner
    inc = ["-Isrc", "-Isrc/ai", "-I."]
    compile_cmd = [
        "g++", "-std=c++20", "-O2",
        "-I/opt/homebrew/include/SDL2", "-I/opt/homebrew/include",
    ] + inc + [
        runner_src,
        os.path.join(GLOB2, "src", "ai", "cortex", "CortexNet.cpp"),
        "-o", runner_bin,
    ]
    print("[5] compiling C++ runner: %s" % " ".join(compile_cmd))
    r = subprocess.run(compile_cmd, cwd=GLOB2)
    if r.returncode != 0:
        sys.exit("C++ runner compilation failed")
    r = subprocess.run([runner_bin, blob_path, inputs_path, cpp_out_path], cwd=GLOB2)
    if r.returncode != 0:
        sys.exit("C++ runner execution failed")
    with open(cpp_out_path) as f:
        cpp = [int(line.strip()) for line in f if line.strip()]
    print("[5] C++ runner computed %d actions" % len(cpp))

    # 6. compare
    if len(cpp) != len(ref):
        sys.exit("LENGTH MISMATCH: ref=%d cpp=%d" % (len(ref), len(cpp)))
    mismatches = [(i, ref[i], cpp[i]) for i in range(len(ref)) if ref[i] != cpp[i]]
    if mismatches:
        print("MISMATCHES: %d / %d" % (len(mismatches), len(ref)))
        for i, rv, cv in mismatches[:20]:
            print("  input %d: ref=%d cpp=%d  feats=%s" % (i, rv, cv, rows[i][0]))
        sys.exit(1)
    print("[6] PARITY OK: %d / %d inputs match, 0 mismatches" % (len(ref), len(ref)))


if __name__ == "__main__":
    main()
