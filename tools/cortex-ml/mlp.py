# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — numpy MLP for BC.  Architecture (ML_CONTRACT.md):
#   16 -> Dense(32) -> ReLU -> Dense(32) -> ReLU -> Dense(20 logits)
# Manual forward + backprop. Cross-entropy on softmax for TRAINING ONLY;
# inference is argmax over logits (no softmax in the sim path).
#
# Standardization (per-feature mu/sigma over the training set) is FOLDED into
# the exported layer-0 weights so the exported net consumes RAW features
# (contract normalization_folded: true). Internally we keep mu/sigma separate
# and standardize the input each forward pass; export does the fold.

import json
import numpy as np

ARCH = [16, 32, 32, 20]
ACTIVATION = "relu"


def _he_init(rng, fan_in, fan_out):
    # He/Kaiming for ReLU. Deterministic given the seeded rng.
    std = np.sqrt(2.0 / fan_in)
    return (rng.standard_normal((fan_in, fan_out)) * std).astype(np.float64)


class MLP:
    """16->32->32->20 ReLU MLP. Weights stored as W[in][out] internally (matches
    x @ W convention); export transposes to the contract's W[out][in]."""

    def __init__(self, seed=0):
        self.rng = np.random.default_rng(seed)
        sizes = ARCH
        self.W = []
        self.b = []
        for fi, fo in zip(sizes[:-1], sizes[1:]):
            self.W.append(_he_init(self.rng, fi, fo))
            self.b.append(np.zeros(fo, dtype=np.float64))
        # Input standardization (identity until fit). Applied to RAW x before
        # layer 0 during in-memory forward; folded into layer 0 on export.
        self.mu = np.zeros(sizes[0], dtype=np.float64)
        self.sigma = np.ones(sizes[0], dtype=np.float64)

    # --- standardization ---------------------------------------------------
    def fit_standardizer(self, X):
        self.mu = X.mean(axis=0)
        sigma = X.std(axis=0)
        sigma[sigma < 1e-6] = 1.0  # guard constant features (avoid div by 0)
        self.sigma = sigma

    def _standardize(self, X):
        return (X - self.mu) / self.sigma

    # --- forward -----------------------------------------------------------
    def forward(self, X_raw, cache=False):
        """X_raw: (N, 16) RAW features. Returns logits (N, 20). If cache, also
        stash intermediates for backprop."""
        x = self._standardize(X_raw)
        a = x
        acts = [a]      # post-activation values (input counts as a0)
        pre = []        # pre-activation values
        for li, (W, b) in enumerate(zip(self.W, self.b)):
            z = a @ W + b
            pre.append(z)
            if li < len(self.W) - 1:
                a = np.maximum(z, 0.0)  # ReLU
            else:
                a = z                   # logits (no activation)
            acts.append(a)
        if cache:
            self._cache = {"x_std": x, "acts": acts, "pre": pre}
        return acts[-1]

    @staticmethod
    def _softmax(logits):
        z = logits - logits.max(axis=1, keepdims=True)
        e = np.exp(z)
        return e / e.sum(axis=1, keepdims=True)

    # --- training step (cross-entropy on softmax) --------------------------
    def train_epoch(self, X, y, lr, batch_size, rng):
        """One SGD epoch. y: int class indices 0..19. Returns mean loss."""
        n = X.shape[0]
        perm = rng.permutation(n)
        total_loss = 0.0
        for start in range(0, n, batch_size):
            bidx = perm[start:start + batch_size]
            xb, yb = X[bidx], y[bidx]
            logits = self.forward(xb, cache=True)
            probs = self._softmax(logits)
            m = xb.shape[0]
            # cross-entropy loss
            ll = -np.log(probs[np.arange(m), yb] + 1e-12)
            total_loss += ll.sum()
            # gradient of softmax-CE wrt logits
            dlogits = probs.copy()
            dlogits[np.arange(m), yb] -= 1.0
            dlogits /= m
            self._backward(dlogits, lr)
        return total_loss / n

    def _backward(self, dlogits, lr):
        acts = self._cache["acts"]
        pre = self._cache["pre"]
        grad = dlogits
        for li in reversed(range(len(self.W))):
            a_prev = acts[li]
            dW = a_prev.T @ grad
            db = grad.sum(axis=0)
            if li > 0:
                dprev = grad @ self.W[li].T
                # ReLU' on the previous layer's pre-activation
                dprev = dprev * (pre[li - 1] > 0.0)
                grad = dprev
            self.W[li] -= lr * dW
            self.b[li] -= lr * db

    # --- inference helpers -------------------------------------------------
    def logits(self, X_raw):
        return self.forward(X_raw, cache=False)

    def argmax_class(self, X_raw):
        return np.argmax(self.logits(X_raw), axis=1)

    # --- export ------------------------------------------------------------
    def folded_layer0(self):
        """Fold standardization (x-mu)/sigma into layer 0 so the exported net
        consumes RAW x.  Layer 0 in-memory computes  W0^T @ ((x-mu)/sigma) + b0.
        Let s = 1/sigma. Then  W0^T diag(s) x  + (b0 - W0^T diag(s) mu).
        Returns (W0_folded[in][out], b0_folded[out])."""
        s = 1.0 / self.sigma
        W0 = self.W[0]                      # [in][out]
        W0_fold = W0 * s[:, None]           # scale each input row by s_i
        b0_fold = self.b[0] - (self.mu @ W0_fold)
        return W0_fold, b0_fold

    def export_f32_json(self, path):
        """Write the cortex-mlp-f32-v1 format. W shape [out][in] (y=W.x+b),
        f32 precision. Layer 0 has folded standardization (RAW input)."""
        W0_fold, b0_fold = self.folded_layer0()
        layers = []
        for li in range(len(self.W)):
            if li == 0:
                W_in_out = W0_fold
                b = b0_fold
            else:
                W_in_out = self.W[li]
                b = self.b[li]
            # contract wants W[out][in] => transpose from internal [in][out]
            W_out_in = np.asarray(W_in_out, dtype=np.float32).T
            layers.append({
                "W": W_out_in.astype(np.float32).tolist(),
                "b": np.asarray(b, dtype=np.float32).tolist(),
            })
        from dataset import FEATURE_NAMES
        doc = {
            "format": "cortex-mlp-f32-v1",
            "arch": list(ARCH),
            "activation": ACTIVATION,
            "input_features": list(FEATURE_NAMES),
            "normalization_folded": True,
            "layers": layers,
        }
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(doc, fh)
        return doc


def f32_forward_from_json(doc, X_raw):
    """Reference f32 forward pass reading the exported JSON directly. Consumes
    RAW features (normalization folded). Returns logits (N, 20) as float32.
    Used by train_bc.py's round-trip self-check."""
    a = np.asarray(X_raw, dtype=np.float32)
    layers = doc["layers"]
    for li, layer in enumerate(layers):
        W_out_in = np.asarray(layer["W"], dtype=np.float32)   # [out][in]
        b = np.asarray(layer["b"], dtype=np.float32)
        z = (a @ W_out_in.T + b).astype(np.float32)           # y = W.x + b
        if li < len(layers) - 1:
            a = np.maximum(z, np.float32(0.0))
        else:
            a = z
    return a
