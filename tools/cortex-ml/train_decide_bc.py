# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex DECIDE pilot — decision-selection behavior-cloning trainer. Fits the
# numpy MLP to reproduce the hand rule's chosen decision class, applies the
# contract's eval-time masking (mask by eligibility -> argmax ties->lowest),
# reports BC parity metrics, and exports the cortex-decide-mlp-f32-v1 JSON.
#
#   python3 train_decide_bc.py --data <dir> --out <weights.json> [--epochs N]
#
# See docs/AI/cortex/DECIDE_CONTRACT.md (binding spec) and DECIDE_PILOT.md.
# Sibling of train_bc.py (worker-cap).

import argparse
import numpy as np

from decide_dataset import (
    load_files, split_files, assemble, popcount,
    FEATURE_NAMES, NUM_FEATURES, NUM_CLASSES,
)
from decide_mlp import DecideMLP, f32_forward_from_json

SEED = 1234


def mask_to_valid(masks):
    """Expand an (N,) array of 18-bit eligibility masks to a bool (N, 18) matrix:
    valid[n, k] = bit k of masks[n]. Matches the inference rule's per-class mask."""
    bits = np.arange(NUM_CLASSES, dtype=np.int64)[None, :]
    return ((masks[:, None] >> bits) & 1).astype(bool)


def masked_argmax(logits, valid):
    """Contract inference rule: mask ineligible classes (-inf) then argmax, ties
    -> lowest class index (np.argmax returns the first max). Returns (N,) class."""
    masked = np.where(valid, logits, -np.inf)
    return np.argmax(masked, axis=1)


def class_weights(y, valid):
    """Inverse-frequency class weights so the rare classes (3/10/11) are not
    ignored. Weight per class = N_total / (NUM_CLASSES * count_c); normalized to
    mean 1 over the training rows. Returns per-class weights (NUM_CLASSES,)."""
    counts = np.bincount(y, minlength=NUM_CLASSES).astype(np.float64)
    counts[counts == 0] = 1.0
    w = y.shape[0] / (NUM_CLASSES * counts)
    # normalize so the mean per-sample weight is ~1 (keeps the LR scale stable)
    sample_w = w[y]
    w = w / sample_w.mean()
    return w


def evaluate(net, X, valid, y):
    """Masked selection accuracy overall and on popcount>=2 rows, plus per-class
    breakdown. Returns (overall_acc, pc2_acc, n_pc2, breakdown_string)."""
    logits = net.logits(X)
    pred = masked_argmax(logits, valid)
    overall = float(np.mean(pred == y))

    pc = valid.sum(axis=1)               # number of eligible classes per row
    pc2 = pc >= 2
    n_pc2 = int(pc2.sum())
    pc2_acc = float(np.mean(pred[pc2] == y[pc2])) if n_pc2 else float("nan")

    lines = []
    for c in range(NUM_CLASSES):
        sel = y == c
        cnt = int(sel.sum())
        if cnt == 0:
            lines.append(f"    class {c:2d}: n=    0")
            continue
        acc = float(np.mean(pred[sel] == c))
        # accuracy on the contested (popcount>=2) subset of this class
        selc = sel & pc2
        cnt2 = int(selc.sum())
        acc2 = float(np.mean(pred[selc] == c)) if cnt2 else float("nan")
        wrong = pred[sel][pred[sel] != c]
        note = ""
        if wrong.size:
            vals, counts = np.unique(wrong, return_counts=True)
            note = f"  (most-confused -> {int(vals[np.argmax(counts)])})"
        lines.append(
            f"    class {c:2d}: n={cnt:6d}  acc={acc:6.3f}  "
            f"[pc>=2 n={cnt2:5d} acc={acc2:6.3f}]{note}"
        )
    return overall, pc2_acc, n_pc2, "\n".join(lines)


def fold_verify(net, X, n_samples=64):
    """Verify the folded net (RAW features) reproduces the normalized net
    (standardized features) outputs. Compares the in-memory net.logits (which
    standardizes internally) against a manual forward using the FOLDED layer 0 on
    RAW inputs. Returns the max abs logit difference over the sample."""
    n = min(n_samples, X.shape[0])
    rng = np.random.default_rng(SEED)
    idx = rng.choice(X.shape[0], size=n, replace=False)
    Xs = X[idx]
    normalized = net.logits(Xs)  # standardizes internally

    # manual forward with folded layer 0 on RAW features
    W0f, b0f = net.folded_layer0()
    a = Xs @ W0f + b0f
    a = np.maximum(a, 0.0)
    for li in range(1, len(net.W)):
        a = a @ net.W[li] + net.b[li]
        if li < len(net.W) - 1:
            a = np.maximum(a, 0.0)
    folded = a
    return float(np.max(np.abs(normalized - folded)))


def round_trip_check(net, doc, X, valid, n_samples=64):
    """Re-load the exported JSON; verify the f32 forward (RAW input) reproduces
    the same MASKED argmax as the in-memory net."""
    n = min(n_samples, X.shape[0])
    rng = np.random.default_rng(SEED + 7)
    idx = rng.choice(X.shape[0], size=n, replace=False)
    Xs, Vs = X[idx], valid[idx]
    in_mem = masked_argmax(net.logits(Xs), Vs)
    from_json = masked_argmax(f32_forward_from_json(doc, Xs), Vs)
    return int(np.sum(in_mem == from_json)), n


def main():
    ap = argparse.ArgumentParser(description="Cortex DECIDE BC trainer (numpy-only).")
    ap.add_argument("--data", required=True, help="directory / glob / file of *.csv traces")
    ap.add_argument("--out", required=True, help="output f32 weights JSON path")
    ap.add_argument("--epochs", type=int, default=400)
    ap.add_argument("--lr", type=float, default=0.02)
    ap.add_argument("--batch-size", type=int, default=256)
    ap.add_argument("--val-frac", type=float, default=0.2)
    args = ap.parse_args()

    files = load_files(args.data)
    train_files, val_files = split_files(files, args.val_frac, SEED)
    if not val_files:  # tiny corpora: eval on train
        val_files = train_files
    Xtr, Mtr, ytr = assemble(train_files)
    Xva, Mva, yva = assemble(val_files)

    print(f"data: {len(files)} game files "
          f"({len(train_files)} train / {len(val_files)} val by GAME)")
    print(f"action rows (chosen != -1): train={Xtr.shape[0]} val={Xva.shape[0]}")
    pc_tr = popcount(Mtr)
    pc_va = popcount(Mva)
    print(f"  popcount>=2 rows: train={int((pc_tr>=2).sum())} "
          f"val={int((pc_va>=2).sum())}")
    assert Xtr.shape[1] == NUM_FEATURES
    if Xtr.shape[0] == 0:
        raise SystemExit("no usable rows after filtering")

    valid_tr = mask_to_valid(Mtr)
    valid_va = mask_to_valid(Mva)
    # sanity: the chosen class must be eligible in its own row (hand rule picks
    # from the eligible set). If this fails the corpus/contract disagree.
    bad = ~valid_tr[np.arange(Xtr.shape[0]), ytr]
    if bad.any():
        raise SystemExit(f"{int(bad.sum())} train rows have chosen class not in eligible_mask")

    net = DecideMLP(seed=SEED)
    net.fit_standardizer(Xtr)  # mu/sigma from TRAIN only

    cw = class_weights(ytr, valid_tr)
    sample_w = cw[ytr]
    print("class weights (inverse-freq, mean-1 normalized):")
    print("    " + " ".join(f"{c}:{cw[c]:.2f}" for c in range(NUM_CLASSES)))

    train_rng = np.random.default_rng(SEED + 1)
    print(f"\ntraining: {args.epochs} epochs, lr={args.lr}, "
          f"batch={args.batch_size}")
    for ep in range(1, args.epochs + 1):
        loss = net.train_epoch(Xtr, valid_tr, ytr, sample_w,
                               args.lr, args.batch_size, train_rng)
        if ep == 1 or ep % max(1, args.epochs // 10) == 0 or ep == args.epochs:
            ov, pc2, npc2, _ = evaluate(net, Xva, valid_va, yva)
            print(f"    epoch {ep:4d}  loss={loss:.4f}  "
                  f"val_overall={ov:.4f}  val_pc>=2={pc2:.4f}")

    ov, pc2, npc2, breakdown = evaluate(net, Xva, valid_va, yva)
    print("\n=== eval (validation, held-out GAMES) ===")
    print(f"  (a) overall masked selection accuracy : {ov:.4f}  (n={Xva.shape[0]})")
    print(f"  (b) accuracy on popcount>=2 rows      : {pc2:.4f}  (n={npc2})")
    print(f"      ^ this is the parity-with-hand number that matters")
    print(f"  (c) per-class breakdown:")
    print(breakdown)

    # fold verification
    fold_err = fold_verify(net, Xtr)
    print(f"\nnormalization-fold check: max |normalized - folded| logit diff = "
          f"{fold_err:.3e}  [{'OK' if fold_err < 1e-6 else 'CHECK'}]")

    # export f32 JSON
    doc = net.export_f32_json(args.out, FEATURE_NAMES)
    print(f"\nexported f32 JSON -> {args.out}")
    print(f"  format={doc['format']} arch={doc['arch']} "
          f"normalization_folded={doc['normalization_folded']}")

    agree, n = round_trip_check(net, doc, Xtr, valid_tr)
    status = "OK" if agree == n else "MISMATCH"
    print(f"round-trip self-check: {agree}/{n} masked-argmax agree  [{status}]")
    if agree != n:
        raise SystemExit("round-trip mismatch: exported JSON diverges from net")


if __name__ == "__main__":
    main()
