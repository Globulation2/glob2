# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — offline RL trainer (Conservative Q-Learning / fitted-Q).
#
# Learns Q(s, a) over the 20 absolute caps by fitted-Q iteration with a frozen
# target net, plus the CQL conservatism penalty (push DOWN the log-sum-exp over
# actions, push UP the dataset action) so out-of-distribution caps are not
# over-valued. The greedy policy is masked argmax over Q — which is EXACTLY the
# contract's inference rule, so the Q-net's 20 outputs ARE the deployable logits:
# quantize.py + CortexNet consume it unchanged (16->32->32->20, argmax).
#
# This is the method that can actually BEAT the ±1 hand rule: Q is defined for
# every absolute cap, so the greedy action can JUMP straight to the in-band cap
# in one cycle instead of crawling. The CQL weight trades that extrapolation off
# against staying near the data. We benchmark CQL vs AWR and keep the winner.
#
#   python3 train_cql.py --data <corpus_dir> --out <weights.json>
#
# See docs/AI/cortex/PILOT.md ("Method", "Reward") and ML_CONTRACT.md.

import argparse
import copy
import numpy as np

from dataset import load_constants, NUM_FEATURES
from mlp import MLP, f32_forward_from_json
from reward import build_transitions, GAMMA
from rl_common import valid_action_mask, masked_argmax_actions

SEED = 1234


def clone_net(net):
    """Frozen target copy (weights + standardizer)."""
    t = MLP(seed=0)
    t.W = [w.copy() for w in net.W]
    t.b = [b.copy() for b in net.b]
    t.mu = net.mu.copy()
    t.sigma = net.sigma.copy()
    return t


def bootstrap_targets(qt, R, Snext, done, starved_next, consts, q_lo, q_hi):
    """y = r + GAMMA*(1-done)*V(s'), where V(s') is the masked max of the TARGET
    Q over valid caps — except on wheat-starved s' where the deployed policy is
    the hard clamp (action 1), so V(s') = Q_target(s', class0). Matches what the
    net will actually do at s'.

    Targets are clamped to [q_lo, q_hi], the theoretical discounted-return bounds
    (R_min/(1-γ), R_max/(1-γ)). Offline FQI with a max-bootstrap is the classic
    'deadly triad' and Q diverges without this — the clamp is a hard tripwire that
    keeps the regression inside the values any real return could take."""
    Qn = qt.logits(Snext)
    maskn = valid_action_mask(Snext, consts)
    Qn_masked = np.where(maskn, Qn, -np.inf)
    v_next = Qn_masked.max(axis=1)
    v_next = np.where(starved_next, Qn[:, 0], v_next)
    v_next = np.where(np.isfinite(v_next), v_next, 0.0)  # degenerate mask guard
    y = R + GAMMA * (1.0 - done.astype(np.float64)) * v_next
    return np.clip(y, q_lo, q_hi)


def main():
    ap = argparse.ArgumentParser(description="Cortex offline-RL trainer (CQL/FQI).")
    ap.add_argument("--data", required=True, help="corpus dir / glob of traces")
    ap.add_argument("--out", required=True, help="output f32 weights JSON")
    ap.add_argument("--epochs", type=int, default=250)
    ap.add_argument("--lr", type=float, default=0.005)
    ap.add_argument("--batch-size", type=int, default=256)
    ap.add_argument("--cql-alpha", type=float, default=0.5,
                    help="conservatism weight (0 = plain FQI)")
    ap.add_argument("--target-sync", type=int, default=10,
                    help="epochs between target-net syncs")
    ap.add_argument("--bc-init-epochs", type=int, default=40,
                    help="warm-start: pretrain Q so argmax ~ behaviour before FQI")
    ap.add_argument("--huber-delta", type=float, default=1.0,
                    help="TD-error clip for the Huber gradient (stability)")
    ap.add_argument("--grad-clip", type=float, default=1.0,
                    help="element-wise clip on the output-logit gradient (stability)")
    args = ap.parse_args()

    consts = load_constants()
    d = build_transitions(args.data, consts)
    info = d["info"]
    print(f"transitions: files={info['n_files']} rows={info['rows_total']} "
          f"trajectories={info['n_trajectories']} transitions={info['n_transitions']} "
          f"starved={info['n_starved']}")

    S, A, Acls = d["S"], d["A"], d["Acls"]
    R, Snext, done = d["R"], d["Snext"], d["done"]
    starved_next = d["starved_next"]
    n_cls = consts["CORTEX_MAX_BUILDING_WORKERS"]

    mask = valid_action_mask(S, consts)                  # (N, 20)
    # rows whose behaviour action is a valid class (for the data-action terms).
    valid_row = mask[np.arange(len(Acls)), np.clip(Acls, 0, n_cls - 1)] & (Acls >= 0) & (Acls < n_cls)
    print(f"rows: {len(Acls)} total, {int(valid_row.sum())} with in-mask behaviour action")

    qnet = MLP(seed=SEED)
    qnet.fit_standardizer(S)

    # Warm-start: a few epochs of plain CE so Q's argmax starts near the hand rule
    # (stabilises FQI; Q magnitudes get corrected by the Bellman regression).
    rng = np.random.default_rng(SEED + 1)
    kept = valid_row
    Xb, yb = S[kept], Acls[kept]
    for _ in range(args.bc_init_epochs):
        qnet.train_epoch(Xb, yb, 0.05, args.batch_size, rng)
    a0 = masked_argmax_actions(qnet.logits(S[kept]), S[kept], consts)
    print(f"after BC-init: match-behaviour={np.mean(a0 == (yb + 1)):.4f}")

    # Theoretical discounted-return bounds (R/(1-γ)) used to clamp the bootstrap
    # targets — the tripwire that stops the deadly-triad blow-up.
    q_hi = max(float(R.max()), 0.0) / (1.0 - GAMMA)
    q_lo = min(float(R.min()), 0.0) / (1.0 - GAMMA)
    print(f"target clamp range: [{q_lo:.2f}, {q_hi:.2f}]")

    target = clone_net(qnet)
    train_rng = np.random.default_rng(SEED + 2)
    n = len(Acls)

    print(f"FQI+CQL: {args.epochs} epochs, alpha={args.cql_alpha}, "
          f"target-sync={args.target_sync}, gamma={GAMMA}, lr={args.lr}, "
          f"huber={args.huber_delta}, grad_clip={args.grad_clip}")
    for ep in range(1, args.epochs + 1):
        # recompute bootstrap targets from the CURRENT frozen target each epoch.
        y_all = bootstrap_targets(target, R, Snext, done, starved_next, consts,
                                  q_lo, q_hi)
        perm = train_rng.permutation(n)
        tot_td = 0.0
        for start in range(0, n, args.batch_size):
            idx = perm[start:start + args.batch_size]
            xb = S[idx]
            ab = Acls[idx]
            yb_t = y_all[idx]
            vb = valid_row[idx]
            mb = mask[idx]
            m = xb.shape[0]

            Q = qnet.forward(xb, cache=True)             # (m, 20) Q-values
            dQ = np.zeros_like(Q)

            # --- Huber TD on the taken action (all rows with a valid action
            # class). The gradient uses the CLIPPED td (Huber): outliers move the
            # net by a bounded amount, which together with the target clamp keeps
            # FQI from diverging. The reported MSE still uses the raw td.
            ar = np.clip(ab, 0, n_cls - 1)
            q_taken = Q[np.arange(m), ar]
            td = (q_taken - yb_t)
            td_use = td * vb                              # zero out invalid-action rows
            td_grad = np.clip(td_use, -args.huber_delta, args.huber_delta)
            dQ[np.arange(m), ar] += 2.0 * td_grad
            tot_td += float((td_use ** 2).sum())

            # --- CQL: push down logsumexp over VALID actions, up the data action.
            if args.cql_alpha > 0:
                Qm = np.where(mb, Q, -np.inf)
                Qm_max = np.max(np.where(np.isfinite(Qm), Qm, -1e18), axis=1, keepdims=True)
                ex = np.where(mb, np.exp(Qm - Qm_max), 0.0)
                sm = ex / (ex.sum(axis=1, keepdims=True) + 1e-12)   # softmax over valid
                cql_grad = sm.copy()
                cql_grad[np.arange(m), ar] -= 1.0
                cql_grad *= vb[:, None]                   # only rows with valid data action
                dQ += args.cql_alpha * cql_grad

            dQ /= m
            if args.grad_clip > 0:
                np.clip(dQ, -args.grad_clip, args.grad_clip, out=dQ)
            qnet._backward(dQ, args.lr)

        if ep % args.target_sync == 0:
            target = clone_net(qnet)
        if ep == 1 or ep % max(1, args.epochs // 8) == 0 or ep == args.epochs:
            a = masked_argmax_actions(qnet.logits(S[kept]), S[kept], consts)
            match = float(np.mean(a == (yb + 1)))
            print(f"    epoch {ep:4d}  td_mse={tot_td / n:.5f}  "
                  f"match-behaviour={match:.4f}  mean_cap(net)={a.mean():.2f}")

    # policy summary on non-starved rows (where the net actually governs)
    keep = ~d["starved"]
    a_net = masked_argmax_actions(qnet.logits(S[keep]), S[keep], consts)
    a_data = A[keep]
    print(f"\nfinal policy (non-starved): match-behaviour={np.mean(a_net == a_data):.4f}  "
          f"mean_cap(net)={a_net.mean():.2f} mean_cap(data)={a_data.mean():.2f}")
    vals, counts = np.unique(a_net, return_counts=True)
    print("net cap histogram:", {int(v): int(c) for v, c in zip(vals, counts)})

    doc = qnet.export_f32_json(args.out)
    print(f"\nexported f32 JSON -> {args.out}  format={doc['format']} arch={doc['arch']}")

    rng2 = np.random.default_rng(SEED)
    idx = rng2.choice(S[keep].shape[0], size=min(64, int(keep.sum())), replace=False)
    Xs = S[keep][idx]
    a_mem = masked_argmax_actions(qnet.logits(Xs), Xs, consts)
    a_json = masked_argmax_actions(f32_forward_from_json(doc, Xs), Xs, consts)
    agree = int(np.sum(a_mem == a_json))
    print(f"round-trip self-check: {agree}/{len(idx)} masked-argmax agree "
          f"[{'OK' if agree == len(idx) else 'MISMATCH'}]")
    if agree != len(idx):
        raise SystemExit("round-trip mismatch")


if __name__ == "__main__":
    main()
