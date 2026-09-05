#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
"""Decision-net parity harness: prove the numpy integer reference and the C++
CortexNet --decide path agree bit-for-bit on the I16F16 inference, using REAL
corpus rows (their 48 features + eligible_mask).

This is the lockstep-safety proof for the DECIDE pilot. Mismatches MUST be 0.

Steps:
  1. Sample N rows from glob2/.tmp/decide_corpus/*.csv (48 features + eligible_mask).
  2. numpy reference: int_ref.score_decision + forward_decide_logits_i32 per row.
  3. Build + run the standalone C++ --decide runner on the SAME blob + inputs.
  4. Compare BOTH the 18 raw I16F16 logits AND the chosen class index. 0 mismatches.

All scratch lives in glob2/.tmp (NEVER /tmp). Paths resolved relative to this file.
"""
import glob as _glob
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import int_ref  # noqa: E402

GLOB2 = os.path.normpath(os.path.join(HERE, "..", ".."))
TMP = os.path.join(GLOB2, ".tmp")
CORPUS = os.path.join(TMP, "decide_corpus")
# Default to the BC blob; override with DECIDE_PARITY_BLOB to gate any decide net
# (e.g. the AWR blob) through the SAME lockstep parity check.
BLOB = os.environ.get("DECIDE_PARITY_BLOB", os.path.join(TMP, "cortex_decide_bc.blob"))

NUM_FEATURES = 48
NUM_LOGITS = 18
SEED = 0xDEC1DE
N_SAMPLE = 5000


def sample_rows(n, seed):
    """Sample n real corpus rows. Returns a list of (features[48], eligible_mask).
    Biased to include a healthy share of popcount>=2 rows (the contested ones)
    but kept REAL — no synthetic feature values."""
    paths = sorted(_glob.glob(os.path.join(CORPUS, "*.csv")))
    if not paths:
        sys.exit("no corpus CSVs at %s" % CORPUS)
    rng = np.random.default_rng(seed)
    feats = []
    masks = []
    for p in paths:
        raw = np.loadtxt(p, delimiter=",", skiprows=1, ndmin=2)
        if raw.size == 0:
            continue
        # columns: tick, team, 48 features, eligible_mask, chosen
        feats.append(raw[:, 2:2 + NUM_FEATURES].astype(np.int64))
        masks.append(raw[:, 2 + NUM_FEATURES].astype(np.int64))
    F = np.concatenate(feats, axis=0)
    M = np.concatenate(masks, axis=0)
    total = F.shape[0]
    # take all popcount>=2 rows (the signal) up to half the budget, fill the rest
    # with a uniform random sample so single-eligible and mask==0 rows are covered.
    pc = np.array([bin(int(x)).count("1") for x in M])
    contested = np.where(pc >= 2)[0]
    rng.shuffle(contested)
    n_contested = min(len(contested), n // 2)
    chosen = list(contested[:n_contested])
    remaining = n - len(chosen)
    pool = rng.permutation(total)
    for idx in pool:
        if remaining <= 0:
            break
        chosen.append(int(idx))
        remaining -= 1
    chosen = chosen[:n]
    rows = [(F[i].tolist(), int(M[i])) for i in chosen]
    return rows


def main():
    os.makedirs(TMP, exist_ok=True)
    inputs_path = os.path.join(TMP, "cortex_decide_parity_inputs.txt")
    cpp_out_path = os.path.join(TMP, "cortex_decide_parity_cpp.txt")
    runner_src = os.path.join(GLOB2, "src", "ai", "cortex", "CortexNetParityMain.cpp")
    runner_obj = os.path.join(GLOB2, "build", "src", "ai", "cortex", "CortexNet.o")
    runner_bin = os.path.join(TMP, "cortex_decide_parity_runner")

    if not os.path.isfile(BLOB):
        sys.exit("missing blob %s — run quantize.py first" % BLOB)

    # 1. sample real corpus rows
    rows = sample_rows(N_SAMPLE, SEED)
    with open(inputs_path, "w") as f:
        for feats, mask in rows:
            f.write(" ".join(str(x) for x in feats))
            f.write(" %d\n" % mask)
    pc2 = sum(1 for _, m in rows if bin(m).count("1") >= 2)
    zero = sum(1 for _, m in rows if m == 0)
    print("[1] sampled %d real corpus rows -> %s  (popcount>=2: %d, mask==0: %d)"
          % (len(rows), inputs_path, pc2, zero))

    # 2. numpy integer reference: 18 narrowed logits + chosen index per row
    net = int_ref.CortexNet.load(BLOB)
    ref = []
    for feats, mask in rows:
        logits = int_ref.forward_decide_logits_i32(net, feats)
        chosen = int_ref.score_decision(net, feats, mask)
        ref.append((logits, chosen))
    print("[2] numpy reference computed %d rows" % len(ref))

    # 3. build + run C++ --decide runner. Force a real recompile of CortexNet.o
    #    (editor LSP can report phantom errors / "up to date"; trust the compile).
    if os.path.isfile(runner_obj):
        os.remove(runner_obj)
    inc = ["-Isrc", "-Isrc/ai", "-I."]
    compile_cmd = [
        "g++", "-std=c++20", "-O2",
        "-I/opt/homebrew/include/SDL2", "-I/opt/homebrew/include",
    ] + inc + [
        runner_src,
        os.path.join(GLOB2, "src", "ai", "cortex", "CortexNet.cpp"),
        "-o", runner_bin,
    ]
    print("[3] compiling C++ --decide runner")
    r = subprocess.run(compile_cmd, cwd=GLOB2)
    if r.returncode != 0:
        sys.exit("C++ runner compilation failed")
    r = subprocess.run([runner_bin, "--decide", BLOB, inputs_path, cpp_out_path], cwd=GLOB2)
    if r.returncode != 0:
        sys.exit("C++ runner execution failed")

    cpp = []
    with open(cpp_out_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            vals = [int(x) for x in line.split()]
            if len(vals) != NUM_LOGITS + 1:
                sys.exit("bad C++ output row: %d ints" % len(vals))
            cpp.append((vals[:NUM_LOGITS], vals[NUM_LOGITS]))
    print("[3] C++ runner computed %d rows" % len(cpp))

    # 4. compare logits AND chosen, bit-for-bit
    if len(cpp) != len(ref):
        sys.exit("LENGTH MISMATCH: ref=%d cpp=%d" % (len(ref), len(cpp)))
    logit_mismatch = 0
    chosen_mismatch = 0
    first = []
    for i, ((rl, rc), (cl, cc)) in enumerate(zip(ref, cpp)):
        if rl != cl:
            logit_mismatch += 1
            if len(first) < 10:
                first.append(("logits", i, rl, cl))
        if rc != cc:
            chosen_mismatch += 1
            if len(first) < 10:
                first.append(("chosen", i, rc, cc))
    print("\n=== DECIDE parity (real corpus rows) ===")
    print("  rows compared           : %d" % len(ref))
    print("  logit mismatches        : %d / %d" % (logit_mismatch, len(ref)))
    print("  chosen-index mismatches : %d / %d" % (chosen_mismatch, len(ref)))
    if logit_mismatch or chosen_mismatch:
        for kind, i, rv, cv in first:
            print("  [%s] row %d: ref=%s cpp=%s" % (kind, i, rv, cv))
        sys.exit(1)
    print("  PARITY OK: 0 mismatches on BOTH logits and chosen index.")


if __name__ == "__main__":
    main()
