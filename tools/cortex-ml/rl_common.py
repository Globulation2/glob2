# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — shared helpers for the offline-RL trainers (AWR, CQL).
# The contract's eval-time action mask (ML_CONTRACT.md step 3) and a small value
# baseline MLP. numpy-only, deterministic. See PILOT.md / ML_CONTRACT.md.

import numpy as np

from dataset import FEATURE_NAMES

F_FREEWORKERS = FEATURE_NAMES.index("freeWorkers")    # 7
F_MAXBUILDLEVEL = FEATURE_NAMES.index("maxBuildLevel")  # 15


def valid_action_mask(X, consts):
    """Contract step 3 eval-time mask. Returns a (N, 20) bool array: class k
    (0-based; action k+1) is valid iff WORKER_MIN <= k+1 <= swarmWorkerCap(row),
    where swarmWorkerCap = LATE if maxBuildLevel >= CAP_LIFT && freeWorkers > 0
    else base. Mirrors train_bc.masked_argmax / int_ref.swarm_worker_cap."""
    worker_min = consts["CORTEX_SWARM_WORKER_MIN"]
    lift = consts["CORTEX_SWARM_CAP_LIFT_BUILDLEVEL"]
    cap_base = consts["CORTEX_SWARM_WORKER_CAP"]
    cap_late = consts["CORTEX_SWARM_WORKER_CAP_LATE"]
    n_cls = consts["CORTEX_MAX_BUILDING_WORKERS"]

    actions = np.arange(1, n_cls + 1)[None, :]          # (1, 20)
    mbl = X[:, F_MAXBUILDLEVEL]
    fw = X[:, F_FREEWORKERS]
    caps = np.where((mbl >= lift) & (fw > 0), cap_late, cap_base)[:, None]
    return (actions >= worker_min) & (actions <= caps)  # (N, 20)


def masked_argmax_actions(logits, X, consts):
    """Masked argmax (ties -> lowest class index, contract step 4). Returns the
    chosen ACTIONS (1..20). Used for offline policy inspection."""
    mask = valid_action_mask(X, consts)
    masked = np.where(mask, logits, -np.inf)
    return np.argmax(masked, axis=1) + 1


class ValueMLP:
    """Tiny 16 -> H -> 1 ReLU value net (AWR baseline). Standardizes inputs with
    a fixed mu/sigma fit on the data. Manual forward + backward, MSE regression.
    Not exported — only used to form advantages during training."""

    def __init__(self, in_dim=16, hidden=32, seed=0):
        rng = np.random.default_rng(seed)
        self.W1 = (rng.standard_normal((in_dim, hidden)) * np.sqrt(2.0 / in_dim))
        self.b1 = np.zeros(hidden)
        self.W2 = (rng.standard_normal((hidden, 1)) * np.sqrt(2.0 / hidden))
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
                dpred = (2.0 / m) * (pred - yb)[:, None]      # (m,1)
                dW2 = h.T @ dpred
                db2 = dpred.sum(axis=0)
                dh = dpred @ self.W2.T
                dh = dh * (h_pre > 0.0)
                dW1 = xb.T @ dh
                db1 = dh.sum(axis=0)
                self.W2 -= lr * dW2
                self.b2 -= lr * db2
                self.W1 -= lr * dW1
                self.b1 -= lr * db1
        return self
