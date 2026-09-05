# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex DECIDE pilot — offline-RL (AWR) trainer for the decision-selection net.
# Warm-starts from the BC net, fits a value baseline V(s) to Monte-Carlo returns,
# then runs Advantage-Weighted Regression: the SAME masked-softmax CE as BC, but
# each contested (popcount>=2) decision row is reweighted by exp(A_t/tau) (clipped).
#
#   BC clones the hand rule; AWR nudges the clone toward the decisions seen on
#   higher-return trajectories — the first pass that can BEAT, not just match, the
#   hand scores. AWR (Peng et al. 2019) cast offline + Monte-Carlo: no OOD value
#   bootstrapping, so it is the stable offline-RL baseline.
#
# The net architecture is EXACTLY 48->64->64->18 and the masked-CE / mask-then-
# argmax inference rule is identical to BC — the blob / inference contract is
# unchanged (DECIDE_CONTRACT.md). Reward/shaping lives in decide_reward.py.
#
#   python3 train_decide_awr.py --bc <bc.json> --data <dir> --out <awr.json>
#
# See docs/AI/cortex/DECIDE_PILOT.md (Method, Reward) and DECIDE_CONTRACT.md.
# Sibling of train_awr.py (worker-cap) and train_decide_bc.py (BC).

import argparse
import json
import os
import numpy as np

from decide_dataset import (
    DecideTraceFile, find_trace_files, split_files,
    FEATURE_NAMES, NUM_FEATURES, NUM_CLASSES,
)
from decide_mlp import DecideMLP, f32_forward_from_json
from decide_reward import (
    Episode, fit_norms, terminal_reward, GAMMA, LAMBDA, W_MIL, W_ECO, W_RISK,
)
from train_decide_bc import mask_to_valid, masked_argmax

SEED = 1234


# --- value baseline V(s): 48 -> 64 -> 1 MSE on Monte-Carlo returns ----------
class ValueMLP:
    """48 -> H -> 1 ReLU value net (AWR baseline). Standardizes inputs with a
    fixed mu/sigma fit on the data. Manual forward + backward, MSE regression.
    Not exported — only used to form advantages during training. Sibling of
    rl_common.ValueMLP, sized for the 48-feature decide observation."""

    def __init__(self, in_dim=NUM_FEATURES, hidden=64, seed=0):
        rng = np.random.default_rng(seed)
        self.W1 = rng.standard_normal((in_dim, hidden)) * np.sqrt(2.0 / in_dim)
        self.b1 = np.zeros(hidden)
        self.W2 = rng.standard_normal((hidden, 1)) * np.sqrt(2.0 / hidden)
        self.b2 = np.zeros(1)
        self.mu = np.zeros(in_dim)
        self.sigma = np.ones(in_dim)

    def fit_standardizer(self, X):
        self.mu = X.mean(axis=0)
        s = X.std(axis=0)
        s[s < 1e-6] = 1.0
        self.sigma = s

    def _std(self, X):
        return (X - self.mu) / self.sigma

    def predict(self, X):
        x = self._std(X)
        h = np.maximum(x @ self.W1 + self.b1, 0.0)
        return (h @ self.W2 + self.b2)[:, 0]

    def fit(self, X, y, epochs=300, lr=0.01, batch=256, seed=0):
        rng = np.random.default_rng(seed)
        n = X.shape[0]
        for _ in range(epochs):
            perm = rng.permutation(n)
            for start in range(0, n, batch):
                idx = perm[start:start + batch]
                xb = self._std(X[idx])
                yb = y[idx]
                h_pre = xb @ self.W1 + self.b1
                h = np.maximum(h_pre, 0.0)
                pred = (h @ self.W2 + self.b2)[:, 0]
                m = xb.shape[0]
                dpred = (2.0 / m) * (pred - yb)[:, None]
                dW2 = h.T @ dpred
                db2 = dpred.sum(axis=0)
                dh = (dpred @ self.W2.T) * (h_pre > 0.0)
                dW1 = xb.T @ dh
                db1 = dh.sum(axis=0)
                self.W2 -= lr * dW2
                self.b2 -= lr * db2
                self.W1 -= lr * dW1
                self.b1 -= lr * db1
        return self


# --- warm-start: load the BC f32 JSON into a DecideMLP ----------------------
def load_bc_net(bc_json_path, X_for_standardizer):
    """Build a DecideMLP warm-started from the exported BC f32 JSON. The JSON has
    normalization FOLDED into layer 0 (consumes raw features); we run AWR on raw
    features too by setting mu=0, sigma=1 so the in-memory net == the exported net
    and the re-fold on export is the identity. Returns the net."""
    with open(bc_json_path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    if doc.get("format") != "cortex-decide-mlp-f32-v1":
        raise SystemExit("BC json format %r unexpected" % doc.get("format"))
    if doc.get("arch") != [48, 64, 64, 18]:
        raise SystemExit("BC json arch %r != [48,64,64,18]" % doc.get("arch"))

    net = DecideMLP(seed=SEED)
    # The exported JSON stores W as contract W[out][in]; DecideMLP holds W[in][out].
    for li, layer in enumerate(doc["layers"]):
        W_out_in = np.asarray(layer["W"], dtype=np.float64)
        b = np.asarray(layer["b"], dtype=np.float64)
        net.W[li] = W_out_in.T.copy()          # -> W[in][out]
        net.b[li] = b.copy()
    # Folded JSON consumes RAW features; keep the in-memory net identity-normalized
    # so forward() (which standardizes by mu/sigma) is a no-op and equals the JSON.
    net.mu = np.zeros(NUM_FEATURES, dtype=np.float64)
    net.sigma = np.ones(NUM_FEATURES, dtype=np.float64)
    return net


# --- AWR weighted masked-CE epoch (identical structure to BC train_epoch) ----
# I16F16 quantization is lossless only while |W| stays inside the int32 fixed-
# point range: q = W * 2^16 must fit in int32, so |W| < 2^15 = 32768. AWR's
# exp-advantage weighting otherwise drives the weights unbounded, overflowing the
# int32 weight AND (the binding constraint) the int64 accumulator on large raw
# features. Layer 0 is frozen at BC scale + the deeper layers get L2 decay; the
# authoritative guard below quantizes and runs the int64 forward pass on REAL
# corpus rows (the exact C++ arithmetic) and fails if it overflows.
I16F16_WEIGHT_CEIL = 32768.0


def awr_epoch(net, X, valid, y, sample_w, lr, batch_size, rng, weight_decay):
    """One epoch of WEIGHTED masked-softmax CE. Identical to DecideMLP.train_epoch
    except the per-sample weight is the AWR advantage weight (not inverse-freq),
    an L2 weight decay keeps the deeper weights bounded, and layer 0 is FROZEN at
    its BC (normalization-folded) values. Freezing layer 0 holds the per-feature
    activation scale identical to BC, which is what keeps the I16F16 int64
    accumulator from overflowing on large raw features (swim*/tick reach ~1e5).
    The masked structure is unchanged so the inference contract holds."""
    return net.train_epoch(X, valid, y, sample_w, lr, batch_size, rng,
                           weight_decay=weight_decay, freeze_layer0=True)


def max_abs_weight(net):
    return float(max(np.abs(net.W[li]).max() for li in range(len(net.W))))


def quantize_safety_check(net, files, n_rows=20000, seed=SEED):
    """Quantize the net to I16F16 and run the int_ref int64 forward pass (the
    C++-authoritative arithmetic) on a sample of REAL corpus rows. Raises if any
    weight clamps to int32 or any accumulator leaves int64 range — i.e. fails
    BEFORE export if the blob could overflow at inference."""
    import importlib.util as _ilu
    here = os.path.dirname(os.path.abspath(__file__))
    infer = os.path.normpath(os.path.join(here, "..", "cortex-ml-infer"))
    spec_q = _ilu.spec_from_file_location("quantize", os.path.join(infer, "quantize.py"))
    quantize = _ilu.module_from_spec(spec_q); spec_q.loader.exec_module(quantize)
    spec_i = _ilu.spec_from_file_location("int_ref", os.path.join(infer, "int_ref.py"))
    int_ref = _ilu.module_from_spec(spec_i); spec_i.loader.exec_module(int_ref)

    # Build the contract f32 doc in memory, quantize to a blob, load via int_ref.
    W0f, b0f = net.folded_layer0()
    layers = []
    for li in range(len(net.W)):
        W_in_out = W0f if li == 0 else net.W[li]
        b = b0f if li == 0 else net.b[li]
        layers.append({"W": np.asarray(W_in_out).T.tolist(), "b": np.asarray(b).tolist()})
    doc = {"format": "cortex-decide-mlp-f32-v1", "arch": [48, 64, 64, 18],
           "activation": "relu", "normalization_folded": True, "layers": layers}

    # clamp report
    clamped = 0
    for L in doc["layers"]:
        for row in L["W"]:
            for v in row:
                q = quantize.to_i16f16(v)
                if q in (quantize.INT32_MIN, quantize.INT32_MAX):
                    clamped += 1
    blob = quantize.quantize(doc)
    tmp_blob = os.path.join(os.path.dirname(files[0].path), "..", "cortex_decide_awr_safetycheck.blob")
    tmp_blob = os.path.normpath(tmp_blob)
    with open(tmp_blob, "wb") as fh:
        fh.write(blob)
    cnet = int_ref.CortexNet.load(tmp_blob)

    import numpy.random as _nr
    X_all = np.concatenate([f.X for f in files], axis=0).astype(np.int64)
    rng = _nr.default_rng(seed)
    idx = rng.choice(X_all.shape[0], size=min(n_rows, X_all.shape[0]), replace=False)
    overflow = 0
    for i in idx:
        try:
            cnet.forward(X_all[i].tolist())
        except OverflowError:
            overflow += 1
    os.remove(tmp_blob)
    print("quantization-safety: int32 weight clamps=%d  int64 accumulator "
          "overflows=%d / %d real rows" % (clamped, overflow, len(idx)))
    if clamped or overflow:
        raise SystemExit(
            "QUANT-SAFETY FAIL: %d clamps, %d overflows — blob would diverge from "
            "f32 at inference. Increase --weight-decay." % (clamped, overflow))


def report_divergence(net_awr, net_bc, X, valid, y, tag):
    """Fraction of contested (popcount>=2) rows where AWR's masked-argmax differs
    from BC's. Reports per-class shift (which decisions AWR prefers more/less)."""
    pc = valid.sum(axis=1)
    pc2 = pc >= 2
    n2 = int(pc2.sum())
    a_bc = masked_argmax(net_bc.logits(X[pc2]), valid[pc2])
    a_awr = masked_argmax(net_awr.logits(X[pc2]), valid[pc2])
    changed = int(np.sum(a_bc != a_awr))
    frac = changed / max(n2, 1)
    print("  [%s] popcount>=2 rows=%d  argmax changed vs BC=%d (%.4f)"
          % (tag, n2, changed, frac))
    # per-class preference shift among contested rows
    bc_counts = np.bincount(a_bc, minlength=NUM_CLASSES)
    awr_counts = np.bincount(a_awr, minlength=NUM_CLASSES)
    deltas = awr_counts - bc_counts
    order = np.argsort(-np.abs(deltas))
    shifts = [(int(c), int(deltas[c]), int(bc_counts[c]), int(awr_counts[c]))
              for c in order if deltas[c] != 0]
    return frac, changed, n2, shifts


# --- round-trip / export ---------------------------------------------------
def round_trip_check(net, doc, X, valid, n_samples=64):
    n = min(n_samples, X.shape[0])
    rng = np.random.default_rng(SEED + 7)
    idx = rng.choice(X.shape[0], size=n, replace=False)
    Xs, Vs = X[idx], valid[idx]
    in_mem = masked_argmax(net.logits(Xs), Vs)
    from_json = masked_argmax(f32_forward_from_json(doc, Xs), Vs)
    return int(np.sum(in_mem == from_json)), n


def fold_verify(net, X, n_samples=64):
    n = min(n_samples, X.shape[0])
    rng = np.random.default_rng(SEED)
    idx = rng.choice(X.shape[0], size=n, replace=False)
    Xs = X[idx]
    normalized = net.logits(Xs)
    W0f, b0f = net.folded_layer0()
    a = np.maximum(Xs @ W0f + b0f, 0.0)
    for li in range(1, len(net.W)):
        a = a @ net.W[li] + net.b[li]
        if li < len(net.W) - 1:
            a = np.maximum(a, 0.0)
    return float(np.max(np.abs(normalized - a)))


def assemble_value_rows(episodes):
    """Stack ALL cycle rows (holds included) for the value fit: returns (X, G)."""
    Xs = [ep.X for ep in episodes]
    Gs = [ep.G for ep in episodes]
    return np.concatenate(Xs, axis=0), np.concatenate(Gs, axis=0)


def assemble_policy_rows(episodes):
    """Stack the DECISION rows (chosen != -1) with their G, mask, chosen. The
    policy is only updated at decision rows; popcount<2 rows get a no-op mask."""
    Xs, Gs, Ms, Ys = [], [], [], []
    for ep in episodes:
        sel = ep.chosen != -1
        Xs.append(ep.X[sel])
        Gs.append(ep.G[sel])
        Ms.append(ep.mask[sel])
        Ys.append(ep.chosen[sel])
    X = np.concatenate(Xs, axis=0)
    G = np.concatenate(Gs, axis=0)
    M = np.concatenate(Ms, axis=0)
    Y = np.concatenate(Ys, axis=0)
    return X, G, M, Y


def main():
    ap = argparse.ArgumentParser(description="Cortex DECIDE AWR trainer (numpy-only).")
    ap.add_argument("--bc", required=True, help="BC warm-start f32 JSON")
    ap.add_argument("--data", required=True, help="dir / glob / file of *.csv traces")
    ap.add_argument("--out", required=True, help="output AWR f32 weights JSON")
    ap.add_argument("--awr-epochs", type=int, default=150)
    ap.add_argument("--lr", type=float, default=0.01)
    ap.add_argument("--batch-size", type=int, default=256)
    ap.add_argument("--val-epochs", type=int, default=300)
    ap.add_argument("--val-frac", type=float, default=0.2)
    ap.add_argument("--tau", type=float, default=-1.0,
                    help="AWR temperature; <0 (default) = std of advantages")
    ap.add_argument("--w-max", type=float, default=20.0, help="weight clip ceiling")
    ap.add_argument("--weight-decay", type=float, default=1e-3,
                    help="L2 decay on net weights — bounds them to the I16F16 "
                         "quantization-safe range (|W| < 32768). 0 reproduces the "
                         "unbounded BC-style update (may overflow at inference).")
    args = ap.parse_args()

    # --- load corpus, split by GAME, build episodes with shaped rewards ------
    paths = find_trace_files(args.data)
    if not paths:
        raise SystemExit("no trace CSVs at %r" % args.data)
    files = [DecideTraceFile(p) for p in paths]
    train_files, val_files = split_files(files, args.val_frac, SEED)
    if not val_files:
        val_files = train_files

    # corpus norms fit over ALL rows (FOW potential scale is corpus-global)
    X_all = np.concatenate([f.X for f in files], axis=0)
    norms = fit_norms(X_all)

    tr_eps = [Episode(f, terminal_reward(f.path), norms) for f in train_files]
    va_eps = [Episode(f, terminal_reward(f.path), norms) for f in val_files]

    n_dec_tr = sum(1 for e in tr_eps if e.R_terminal != 0)
    print("episodes: %d train (%d decisive) / %d val by GAME"
          % (len(tr_eps), n_dec_tr, len(va_eps)))
    print("reward consts: gamma=%.3f lambda=%.2f w_mil=%.2f w_eco=%.2f w_risk=%.2f"
          % (GAMMA, LAMBDA, W_MIL, W_ECO, W_RISK))
    print("norms (95th pct): mil=%.3f eco=%.3f" % (norms["mil"], norms["eco"]))

    # --- value fit on Monte-Carlo returns over ALL rows ----------------------
    Xv_tr, Gv_tr = assemble_value_rows(tr_eps)
    Xv_va, Gv_va = assemble_value_rows(va_eps)
    print("\nvalue-fit rows (all cycles): train=%d val=%d" % (Xv_tr.shape[0], Xv_va.shape[0]))
    print("MC return G: train mean=%.4f std=%.4f  val mean=%.4f std=%.4f"
          % (Gv_tr.mean(), Gv_tr.std(), Gv_va.mean(), Gv_va.std()))

    vnet = ValueMLP(seed=SEED)
    vnet.fit_standardizer(Xv_tr)
    vnet.fit(Xv_tr, Gv_tr, epochs=args.val_epochs, lr=0.01, batch=256, seed=SEED)

    def r2(X, G):
        pred = vnet.predict(X)
        ss_res = float(np.sum((G - pred) ** 2))
        ss_tot = float(np.sum((G - G.mean()) ** 2))
        mse = ss_res / X.shape[0]
        return mse, (1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan"))
    mse_tr, r2_tr = r2(Xv_tr, Gv_tr)
    mse_va, r2_va = r2(Xv_va, Gv_va)
    print("value fit: train MSE=%.5f R2=%.4f | val MSE=%.5f R2=%.4f"
          % (mse_tr, r2_tr, mse_va, r2_va))

    # --- policy rows + advantages (contested rows carry the AWR signal) ------
    Xp_tr, Gp_tr, Mp_tr, yp_tr = assemble_policy_rows(tr_eps)
    Xp_va, Gp_va, Mp_va, yp_va = assemble_policy_rows(va_eps)
    valid_tr = mask_to_valid(Mp_tr)
    valid_va = mask_to_valid(Mp_va)
    pc_tr = valid_tr.sum(axis=1)
    pc_va = valid_va.sum(axis=1)
    pc2_tr = pc_tr >= 2
    pc2_va = pc_va >= 2
    print("\npolicy decision rows: train=%d (popcount>=2: %d) | val=%d (popcount>=2: %d)"
          % (Xp_tr.shape[0], int(pc2_tr.sum()), Xp_va.shape[0], int(pc2_va.sum())))

    V_tr = vnet.predict(Xp_tr)
    adv = Gp_tr - V_tr
    adv_c = adv[pc2_tr]      # advantage on the rows that actually get a policy grad
    print("advantage (popcount>=2): mean=%.4f std=%.4f" % (adv_c.mean(), adv_c.std()))
    aq = np.percentile(adv_c, [1, 5, 25, 50, 75, 95, 99])
    print("  adv pctiles [1,5,25,50,75,95,99]: " + " ".join("%.4f" % q for q in aq))

    tau = args.tau if args.tau > 0 else float(adv_c.std())
    if tau < 1e-8:
        tau = 1.0
    print("tau = %.5f (%s)" % (tau, "given" if args.tau > 0 else "std of advantages"))

    # AWR weights: exp(A/tau) clipped to w_max. Forced rows (popcount<2) get the
    # mask-no-op anyway; set their weight to 0 so they contribute nothing.
    w = np.minimum(np.exp(adv / tau), args.w_max)
    w[~pc2_tr] = 0.0
    # normalize the active (contested) weights to mean 1 (keeps the LR scale stable)
    active = pc2_tr
    if active.any():
        w[active] = w[active] / max(w[active].mean(), 1e-8)
    print("weights (popcount>=2): mean=%.4f max=%.4f min=%.4f"
          % (w[active].mean(), w[active].max(), w[active].min()))

    # --- BC warm-start net (frozen reference) + AWR net (trained) ------------
    net_bc = load_bc_net(args.bc, Xp_tr)
    # fresh net warm-started from the same BC weights — AWR updates this one
    net = load_bc_net(args.bc, Xp_tr)

    # sanity: AWR net starts == BC (zero divergence before any update)
    f0, _, _, _ = report_divergence(net, net_bc, Xp_va, valid_va, yp_va, "warm-start")

    print("\nAWR: %d epochs, tau=%.5f, w_max=%.1f, lr=%.4g, batch=%d, weight_decay=%.2g"
          % (args.awr_epochs, tau, args.w_max, args.lr, args.batch_size, args.weight_decay))
    awr_rng = np.random.default_rng(SEED + 2)
    for ep in range(1, args.awr_epochs + 1):
        loss = awr_epoch(net, Xp_tr, valid_tr, yp_tr, w,
                         args.lr, args.batch_size, awr_rng, args.weight_decay)
        if ep == 1 or ep % max(1, args.awr_epochs // 10) == 0 or ep == args.awr_epochs:
            a_bc = masked_argmax(net_bc.logits(Xp_va[pc2_va]), valid_va[pc2_va])
            a_awr = masked_argmax(net.logits(Xp_va[pc2_va]), valid_va[pc2_va])
            chg = float(np.mean(a_bc != a_awr))
            print("    epoch %4d  wloss=%.4f  val popcount>=2 changed-vs-BC=%.4f  max|W|=%.1f"
                  % (ep, loss, chg, max_abs_weight(net)))

    # --- divergence report ---------------------------------------------------
    print("\n=== AWR vs BC divergence (held-out GAMES) ===")
    frac, changed, n2, shifts = report_divergence(
        net, net_bc, Xp_va, valid_va, yp_va, "final")
    if frac < 1e-6:
        print("  !! FLAG: AWR argmax matches BC on ~0% of contested rows — the")
        print("     advantage signal was too weak to move the policy. Consider a")
        print("     smaller tau, stronger shaping, or variant-B Phi.")
    print("  per-class preference shift (AWR vs BC, contested rows; +more / -less):")
    if not shifts:
        print("    (none)")
    for c, d, bc_n, awr_n in shifts[:12]:
        print("    class %2d: %+d   (BC %d -> AWR %d)" % (c, d, bc_n, awr_n))

    # --- quantization-safety guard: run the EXACT I16F16 int64 forward pass --
    # (int_ref, the C++-authoritative reference) on real corpus rows and confirm
    # no int32-weight clamp and no int64-accumulator overflow. This is the real
    # gate; max|W| is reported for context.
    mw = max_abs_weight(net)
    print("\nmax |W| after AWR = %.2f  (I16F16 int32 weight ceiling %.0f)"
          % (mw, I16F16_WEIGHT_CEIL))
    if mw >= I16F16_WEIGHT_CEIL:
        raise SystemExit(
            "max|W|=%.1f >= %.0f — quantized weight overflows int32. Increase "
            "--weight-decay or --awr-epochs." % (mw, I16F16_WEIGHT_CEIL))
    quantize_safety_check(net, files)

    # --- export f32 JSON (fold normalization == identity here) ---------------
    fe = fold_verify(net, Xp_tr)
    print("\nnormalization-fold check: max |normalized - folded| = %.3e [%s]"
          % (fe, "OK" if fe < 1e-6 else "CHECK"))
    doc = net.export_f32_json(args.out, FEATURE_NAMES)
    print("exported f32 JSON -> %s  format=%s arch=%s"
          % (args.out, doc["format"], doc["arch"]))

    agree, n = round_trip_check(net, doc, Xp_tr, valid_tr)
    status = "OK" if agree == n else "MISMATCH"
    print("round-trip self-check: %d/%d masked-argmax agree [%s]" % (agree, n, status))
    if agree != n:
        raise SystemExit("round-trip mismatch: exported JSON diverges from net")


if __name__ == "__main__":
    main()
