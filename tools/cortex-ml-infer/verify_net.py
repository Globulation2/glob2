#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
"""Targeted lockstep + quantization check for a SPECIFIC trained net.

parity.py proves C++ CortexNet == numpy int_ref for RANDOM nets on random inputs
(the net-agnostic lockstep proof). This script proves it for the ACTUAL shipped
net on REAL corpus feature rows, and additionally reports the quantization
fidelity (does the I16F16 integer policy agree with the f32 policy):

  1. f32 JSON -> I16F16 blob (quantize.py).
  2. sample feature rows from the trace corpus.
  3. numpy integer inference (int_ref) on the blob   -> ref actions.
  4. C++ CortexNet on the SAME blob + inputs         -> cpp actions.
     ==> MUST be identical (lockstep safety).
  5. f32 forward (masked argmax) on the JSON         -> f32 actions.
     ==> agreement rate vs the integer policy = quantization fidelity (info).

All scratch lives in glob2/.tmp. Usage:
  python3 verify_net.py <net.json> <corpus_dir> [--n 4000] [--blob out.blob]
"""
import argparse
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import quantize  # noqa: E402
import int_ref  # noqa: E402

GLOB2 = os.path.normpath(os.path.join(HERE, "..", ".."))
TMP = os.path.join(GLOB2, ".tmp")
ML = os.path.join(GLOB2, "tools", "cortex-ml")
sys.path.insert(0, ML)
from dataset import find_trace_files, TraceFile, FEATURE_NAMES  # noqa: E402
from mlp import f32_forward_from_json  # noqa: E402

F_HARVEST = FEATURE_NAMES.index("harvestableWheatNearby")
F_FREE = FEATURE_NAMES.index("freeWorkers")
F_MBL = FEATURE_NAMES.index("maxBuildLevel")


def sample_features(corpus_dir, n, seed=7):
    paths = find_trace_files(corpus_dir)
    if not paths:
        sys.exit(f"no traces under {corpus_dir}")
    rng = np.random.default_rng(seed)
    # pull rows from a spread of files
    Xs = []
    for p in paths:
        tf = TraceFile(p)
        if len(tf):
            Xs.append(tf.X)
    X = np.concatenate(Xs, axis=0).astype(np.int64)
    if X.shape[0] > n:
        idx = rng.choice(X.shape[0], size=n, replace=False)
        X = X[idx]
    return X


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("net_json")
    ap.add_argument("corpus")
    ap.add_argument("--n", type=int, default=4000)
    ap.add_argument("--blob", default=None)
    args = ap.parse_args()

    blob_path = args.blob or os.path.join(
        TMP, os.path.splitext(os.path.basename(args.net_json))[0] + ".blob")
    inputs_path = os.path.join(TMP, "_verify_inputs.txt")
    ref_path = os.path.join(TMP, "_verify_ref.txt")
    cpp_path = os.path.join(TMP, "_verify_cpp.txt")
    runner = os.path.join(TMP, "cortex_parity_runner")

    # 1. quantize
    net = quantize.load_f32(args.net_json)
    blob = quantize.quantize(net)
    with open(blob_path, "wb") as f:
        f.write(blob)
    print(f"[1] quantized {os.path.basename(args.net_json)} -> {blob_path} ({len(blob)} bytes)")

    # 2. sample real feature rows
    X = sample_features(args.corpus, args.n)
    with open(inputs_path, "w") as f:
        for row in X:
            feats = " ".join(str(int(v)) for v in row)
            f.write(f"{feats} {int(row[F_MBL])} {int(row[F_FREE])} {int(row[F_HARVEST])}\n")
    print(f"[2] sampled {X.shape[0]} real corpus rows -> {inputs_path}")

    # 3. numpy integer reference
    netobj = int_ref.CortexNet.load(blob_path)
    ref = [int_ref.choose_swarm_workers(netobj, [int(v) for v in row],
                                        int(row[F_MBL]), int(row[F_FREE]), int(row[F_HARVEST]))
           for row in X]
    with open(ref_path, "w") as f:
        f.write("\n".join(str(r) for r in ref) + "\n")
    print(f"[3] numpy int_ref computed {len(ref)} actions")

    # 4. C++ runner
    if not os.path.exists(runner):
        sys.exit(f"C++ runner not built at {runner} — run parity.py once to build it")
    r = subprocess.run([runner, blob_path, inputs_path, cpp_path], cwd=GLOB2)
    if r.returncode != 0:
        sys.exit("C++ runner failed")
    with open(cpp_path) as f:
        cpp = [int(x) for x in f.read().split()]
    print(f"[4] C++ CortexNet computed {len(cpp)} actions")

    mism = [(i, ref[i], cpp[i]) for i in range(len(ref)) if ref[i] != cpp[i]]
    if mism:
        print(f"!! LOCKSTEP MISMATCH: {len(mism)}/{len(ref)}")
        for i, a, b in mism[:20]:
            print(f"   row {i}: ref={a} cpp={b} feats={list(X[i])}")
        sys.exit(1)
    print(f"[OK] LOCKSTEP PARITY: {len(ref)}/{len(ref)} C++ == numpy integer inference")

    # 5. quantization fidelity (int policy vs f32 policy) — informational
    consts = int_ref  # reuse the same constant values
    # f32 masked argmax with the SAME inference rule (wheat clamp + cap mask)
    logits = f32_forward_from_json(net, X.astype(np.float64))
    f32_act = []
    for i, row in enumerate(X):
        hw = int(row[F_HARVEST])
        if 0 <= hw < int_ref.CORTEX_SWARM_WHEAT_STARVED_TILES:
            f32_act.append(int_ref.CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP)
            continue
        cap = int_ref.swarm_worker_cap(int(row[F_MBL]), int(row[F_FREE]))
        lo = int_ref.CORTEX_SWARM_WORKER_MIN - 1
        hi = min(cap - 1, len(logits[i]) - 1)
        seg = logits[i][lo:hi + 1]
        f32_act.append(int(np.argmax(seg)) + lo + 1)
    agree = int(np.sum(np.asarray(f32_act) == np.asarray(ref)))
    print(f"[5] quantization fidelity (int vs f32 policy): {agree}/{len(ref)} "
          f"= {100.0 * agree / len(ref):.2f}%")


if __name__ == "__main__":
    main()
