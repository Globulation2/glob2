# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — behavior-cloning trainer.  Fits the numpy MLP to reproduce
# the hand rule's swarm worker cap (`desired`), applies the contract's eval-time
# masking, reports BC parity metrics, and exports the cortex-mlp-f32-v1 JSON.
#
#   python3 train_bc.py --data <dir> --out <weights.json> [--epochs N]
#
# See docs/AI/cortex/ML_CONTRACT.md (binding spec) and PILOT.md.

import argparse
import numpy as np

from dataset import (
    load_constants, load_dataset, swarm_worker_cap, NUM_FEATURES,
)
from mlp import MLP, f32_forward_from_json

SEED = 1234


def masked_argmax(logits, X_raw, consts):
    """Contract step 3+4 (eval-time masking). Valid actions are
    [WORKER_MIN .. swarmWorkerCap(row)]; class index k -> action k+1. Classes
    outside the band are masked (-inf). Ties -> lowest class index.
    Returns predicted ACTIONS (1..20), shape (N,)."""
    worker_min = consts["CORTEX_SWARM_WORKER_MIN"]
    lift = consts["CORTEX_SWARM_CAP_LIFT_BUILDLEVEL"]
    cap_base = consts["CORTEX_SWARM_WORKER_CAP"]
    cap_late = consts["CORTEX_SWARM_WORKER_CAP_LATE"]

    n, k = logits.shape
    actions = np.arange(1, k + 1)[None, :]            # (1, 20)
    # maxBuildLevel is feature index 15, freeWorkers is index 7
    mbl = X_raw[:, 15]
    fw = X_raw[:, 7]
    caps = np.where((mbl >= lift) & (fw > 0), cap_late, cap_base)[:, None]
    valid = (actions >= worker_min) & (actions <= caps)   # (N, 20)

    masked = np.where(valid, logits, -np.inf)
    # ties -> lowest class index: np.argmax already returns the first max.
    pred_class = np.argmax(masked, axis=1)
    return pred_class + 1


def evaluate(net, X, y, consts):
    """Returns (masked_top1_acc, parity_rate, confusion-summary string).
    Both metrics use the contract's masked-argmax. desired = y+1."""
    logits = net.logits(X)
    pred_action = masked_argmax(logits, X, consts)
    true_action = (y + 1).astype(np.int64)

    masked_top1 = float(np.mean(pred_action == true_action))
    # On BC the "hand rule" target IS `desired`, so parity == top-1 here; both
    # are reported because post-RL they diverge (parity is the BC contract).
    parity = masked_top1

    # confusion summary: per-true-action accuracy + worst confusions
    lines = []
    cap = consts["CORTEX_MAX_BUILDING_WORKERS"]
    for a in range(1, cap + 1):
        sel = true_action == a
        cnt = int(sel.sum())
        if cnt == 0:
            continue
        acc = float(np.mean(pred_action[sel] == a))
        # most common wrong prediction
        wrong = pred_action[sel][pred_action[sel] != a]
        note = ""
        if wrong.size:
            vals, counts = np.unique(wrong, return_counts=True)
            mc = int(vals[np.argmax(counts)])
            note = f"  (most-confused -> {mc})"
        lines.append(f"    action {a:2d}: n={cnt:5d}  acc={acc:6.3f}{note}")
    return masked_top1, parity, "\n".join(lines)


def round_trip_check(net, doc, X, consts, n_samples=16):
    """Re-load the exported JSON and verify the f32 forward pass reproduces the
    same MASKED argmax as the in-memory net on a few rows."""
    n = min(n_samples, X.shape[0])
    rng = np.random.default_rng(SEED)
    idx = rng.choice(X.shape[0], size=n, replace=False)
    Xs = X[idx]
    in_mem = masked_argmax(net.logits(Xs), Xs, consts)
    json_logits = f32_forward_from_json(doc, Xs)
    from_json = masked_argmax(json_logits, Xs, consts)
    agree = int(np.sum(in_mem == from_json))
    return agree, n


def main():
    ap = argparse.ArgumentParser(description="Cortex BC trainer (numpy-only).")
    ap.add_argument("--data", required=True,
                    help="directory / glob / file of *.team*.csv traces")
    ap.add_argument("--out", required=True, help="output f32 weights JSON path")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--lr", type=float, default=0.05)
    ap.add_argument("--batch-size", type=int, default=64)
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--include-wheat-starved", action="store_true",
                    help="keep hard-clamp rows in training (default: excluded)")
    args = ap.parse_args()

    consts = load_constants()
    print("constants (from CortexConstants.h):")
    for k, v in consts.items():
        print(f"    {k} = {v}")

    X, y, info = load_dataset(args.data, consts,
                              include_wheat_starved=args.include_wheat_starved)
    print(f"\ndata: {info['n_files']} file(s), {info['rows_total']} rows total, "
          f"{info['rows_wheat_starved']} wheat-starved excluded"
          f"{' (KEPT)' if args.include_wheat_starved else ''}, "
          f"{info['rows_used']} rows used")
    assert X.shape[1] == NUM_FEATURES
    if X.shape[0] == 0:
        raise SystemExit("no usable rows after filtering")

    # deterministic train/val split
    rng = np.random.default_rng(SEED)
    perm = rng.permutation(X.shape[0])
    X, y = X[perm], y[perm]
    n_val = int(X.shape[0] * args.val_frac)
    Xtr, ytr = X[n_val:], y[n_val:]
    Xva, yva = X[:n_val], y[:n_val]
    if Xva.shape[0] == 0:  # tiny datasets: eval on train
        Xva, yva = Xtr, ytr

    net = MLP(seed=SEED)
    net.fit_standardizer(Xtr)   # mu/sigma from TRAIN only

    train_rng = np.random.default_rng(SEED + 1)
    print(f"\ntraining: {args.epochs} epochs, lr={args.lr}, "
          f"batch={args.batch_size}, train={Xtr.shape[0]}, val={Xva.shape[0]}")
    for ep in range(1, args.epochs + 1):
        loss = net.train_epoch(Xtr, ytr, args.lr, args.batch_size, train_rng)
        if ep == 1 or ep % max(1, args.epochs // 10) == 0 or ep == args.epochs:
            acc, _, _ = evaluate(net, Xva, yva, consts)
            print(f"    epoch {ep:4d}  loss={loss:.4f}  val_masked_top1={acc:.4f}")

    # final metrics on validation set
    top1, parity, conf = evaluate(net, Xva, yva, consts)
    print("\n=== eval (validation) ===")
    print(f"  (a) masked top-1 accuracy        : {top1:.4f}")
    print(f"  (b) exact-match-with-hand-rule   : {parity:.4f}  (BC parity)")
    print(f"  (c) confusion summary (per action):")
    print(conf)

    # export f32 JSON
    doc = net.export_f32_json(args.out)
    print(f"\nexported f32 JSON -> {args.out}")
    print(f"  format={doc['format']} arch={doc['arch']} "
          f"normalization_folded={doc['normalization_folded']}")

    # round-trip self-check
    agree, n = round_trip_check(net, doc, Xtr, consts)
    status = "OK" if agree == n else "MISMATCH"
    print(f"\nround-trip self-check: {agree}/{n} masked-argmax agree  [{status}]")
    if agree != n:
        raise SystemExit("round-trip mismatch: exported JSON diverges from net")


if __name__ == "__main__":
    main()
