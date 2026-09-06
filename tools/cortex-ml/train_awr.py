# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — offline RL trainer (Advantage-Weighted Regression).
#
#   BC warm-start  ->  fit value baseline V(s)  ->  weighted BC where each
#   (s_t, a_t) is reweighted by exp((Ret_t - V(s_t)) / beta) (clipped).
#
# AWR (Peng et al. 2019) cast as offline, Monte-Carlo: it nudges the clone toward
# the cap-setting behaviour seen on HIGHER-return trajectories without ever
# bootstrapping an OOD action value, so it is the stable baseline of the two
# offline-RL methods (CQL is the other; we benchmark both and keep the winner).
#
# Same 16->32->32->20 contract as BC (mlp.MLP). Inference is masked argmax over
# the output logits (no softmax in the sim path), so the exported net drops
# straight into quantize.py + CortexNet with no format change.
#
#   python3 train_awr.py --data <corpus_dir> --out <weights.json>
#
# See docs/AI/cortex/PILOT.md ("Method", "Reward") and ML_CONTRACT.md.

import argparse
import numpy as np

from dataset import load_constants, NUM_FEATURES
from mlp import MLP, f32_forward_from_json
from reward import build_transitions
from rl_common import valid_action_mask, masked_argmax_actions, ValueMLP

SEED = 1234


def weighted_ce_epoch(net, X, y, w, lr, batch, rng):
    """One epoch of WEIGHTED softmax cross-entropy (AWR policy update). Reuses the
    MLP's cached forward + _backward; the per-sample weight scales each row's
    gradient. Returns the mean weighted loss."""
    n = X.shape[0]
    perm = rng.permutation(n)
    total = 0.0
    for start in range(0, n, batch):
        idx = perm[start:start + batch]
        xb, yb, wb = X[idx], y[idx], w[idx]
        logits = net.forward(xb, cache=True)
        probs = net._softmax(logits)
        m = xb.shape[0]
        ll = -np.log(probs[np.arange(m), yb] + 1e-12)
        total += float((wb * ll).sum())
        dlogits = probs.copy()
        dlogits[np.arange(m), yb] -= 1.0
        dlogits *= wb[:, None]          # weight each sample's gradient
        dlogits /= m
        net._backward(dlogits, lr)
    return total / n


def bc_pretrain(net, X, y, epochs, lr, batch, rng):
    """Plain (uniform-weight) BC warm-start so AWR starts from the clone."""
    for _ in range(epochs):
        net.train_epoch(X, y, lr, batch, rng)


def report_policy(net, X, y, consts, tag):
    """Masked-argmax stats vs the behaviour action on the (non-starved) set."""
    actions = masked_argmax_actions(net.logits(X), X, consts)
    true_a = y + 1
    match = float(np.mean(actions == true_a))
    # how often the net DIVERGES from the hand rule (the room to beat it)
    print(f"  [{tag}] matches-behaviour={match:.4f}  diverged={1 - match:.4f}  "
          f"mean_cap(net)={actions.mean():.2f} mean_cap(data)={true_a.mean():.2f}")
    return match


def main():
    ap = argparse.ArgumentParser(description="Cortex offline-RL trainer (AWR).")
    ap.add_argument("--data", required=True, help="corpus dir / glob of traces")
    ap.add_argument("--out", required=True, help="output f32 weights JSON")
    ap.add_argument("--bc-epochs", type=int, default=150)
    ap.add_argument("--awr-epochs", type=int, default=120)
    ap.add_argument("--lr", type=float, default=0.05)
    ap.add_argument("--batch-size", type=int, default=128)
    ap.add_argument("--beta", type=float, default=1.0,
                    help="AWR temperature; smaller = sharper advantage weighting")
    ap.add_argument("--w-max", type=float, default=20.0, help="weight clip ceiling")
    ap.add_argument("--val-epochs", type=int, default=300)
    args = ap.parse_args()

    consts = load_constants()
    d = build_transitions(args.data, consts)
    info = d["info"]
    print(f"transitions: files={info['n_files']} rows={info['rows_total']} "
          f"trajectories={info['n_trajectories']} transitions={info['n_transitions']} "
          f"starved={info['n_starved']}")

    # Policy training set: non-starved rows only (the net never runs on starved
    # rows at inference — the hard clamp bypasses it). Returns/value use ALL rows.
    keep = ~d["starved"]
    Xp = d["S"][keep]
    yp = d["Acls"][keep]
    Retp = d["Ret"][keep]
    print(f"policy rows (non-starved): {Xp.shape[0]}")

    # 1. value baseline on ALL transitions (Monte-Carlo return regression)
    vnet = ValueMLP(seed=SEED)
    vnet.fit_standardizer(d["S"])
    vnet.fit(d["S"], d["Ret"], epochs=args.val_epochs, lr=0.01,
             batch=256, seed=SEED)
    V = vnet.predict(Xp)
    adv = Retp - V
    # standardize advantages for a scale-free temperature, then AWR weights.
    adv_std = adv / (adv.std() + 1e-8)
    w = np.minimum(np.exp(adv_std / args.beta), args.w_max)
    print(f"advantage: mean={adv.mean():.3f} std={adv.std():.3f}  "
          f"weight: mean={w.mean():.3f} max={w.max():.3f} min={w.min():.3f}")

    # 2. BC warm-start
    net = MLP(seed=SEED)
    net.fit_standardizer(Xp)
    rng = np.random.default_rng(SEED + 1)
    print(f"\nBC warm-start: {args.bc_epochs} epochs")
    bc_pretrain(net, Xp, yp, args.bc_epochs, args.lr, args.batch_size, rng)
    report_policy(net, Xp, yp, consts, "after-BC")

    # 3. AWR weighted update
    print(f"AWR: {args.awr_epochs} epochs, beta={args.beta}, w_max={args.w_max}")
    awr_rng = np.random.default_rng(SEED + 2)
    for ep in range(1, args.awr_epochs + 1):
        loss = weighted_ce_epoch(net, Xp, yp, w, args.lr, args.batch_size, awr_rng)
        if ep == 1 or ep % max(1, args.awr_epochs // 6) == 0 or ep == args.awr_epochs:
            actions = masked_argmax_actions(net.logits(Xp), Xp, consts)
            match = float(np.mean(actions == (yp + 1)))
            print(f"    epoch {ep:4d}  wloss={loss:.4f}  match-behaviour={match:.4f}")
    report_policy(net, Xp, yp, consts, "after-AWR")

    # 4. export
    doc = net.export_f32_json(args.out)
    print(f"\nexported f32 JSON -> {args.out}  format={doc['format']} arch={doc['arch']}")

    # round-trip self-check (exported JSON reproduces the in-memory masked argmax)
    rng2 = np.random.default_rng(SEED)
    idx = rng2.choice(Xp.shape[0], size=min(64, Xp.shape[0]), replace=False)
    a_mem = masked_argmax_actions(net.logits(Xp[idx]), Xp[idx], consts)
    a_json = masked_argmax_actions(f32_forward_from_json(doc, Xp[idx]), Xp[idx], consts)
    agree = int(np.sum(a_mem == a_json))
    print(f"round-trip self-check: {agree}/{len(idx)} masked-argmax agree "
          f"[{'OK' if agree == len(idx) else 'MISMATCH'}]")
    if agree != len(idx):
        raise SystemExit("round-trip mismatch")


if __name__ == "__main__":
    main()
