# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex DECIDE pilot — numpy MLP for decision-selection BC. Architecture
# (DECIDE_CONTRACT.md):  48 -> Dense(64) -> ReLU -> Dense(64) -> ReLU -> Dense(18)
# Manual forward + backprop. MASKED softmax cross-entropy for TRAINING ONLY
# (softmax restricted to the eligible classes, matching the mask-then-argmax
# inference rule); inference is argmax over logits (no softmax in the sim path).
#
# Standardization (per-feature mu/sigma over the training set) is FOLDED into
# the exported layer-0 weights so the exported net consumes RAW features
# (contract normalization_folded: true). Internally we keep mu/sigma separate
# and standardize the input each forward pass; export does the fold. Sibling of
# mlp.py (worker-cap), same machinery; differs only in arch, masked loss, class
# weighting, and the decide f32 format string.

import json
import numpy as np

ARCH = [48, 64, 64, 18]
ACTIVATION = "relu"
NEG_INF = -1e30  # finite sentinel for masked logits (avoids nan in softmax)


def _he_init(rng, fan_in, fan_out):
    std = np.sqrt(2.0 / fan_in)
    return (rng.standard_normal((fan_in, fan_out)) * std).astype(np.float64)


class DecideMLP:
    """48->64->64->18 ReLU MLP. Weights stored as W[in][out] internally (matches
    x @ W convention); export transposes to the contract's W[out][in]."""

    def __init__(self, seed=0):
        self.rng = np.random.default_rng(seed)
        sizes = ARCH
        self.W = []
        self.b = []
        for fi, fo in zip(sizes[:-1], sizes[1:]):
            self.W.append(_he_init(self.rng, fi, fo))
            self.b.append(np.zeros(fo, dtype=np.float64))
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
        x = self._standardize(X_raw)
        a = x
        acts = [a]
        pre = []
        for li, (W, b) in enumerate(zip(self.W, self.b)):
            z = a @ W + b
            pre.append(z)
            if li < len(self.W) - 1:
                a = np.maximum(z, 0.0)
            else:
                a = z
            acts.append(a)
        if cache:
            self._cache = {"x_std": x, "acts": acts, "pre": pre}
        return acts[-1]

    @staticmethod
    def _masked_softmax(logits, valid):
        """Softmax restricted to columns where valid is True (others -> ~0).
        valid: bool (N, K). Returns probs (N, K) with mass only on valid cols."""
        z = np.where(valid, logits, NEG_INF)
        z = z - z.max(axis=1, keepdims=True)
        e = np.where(valid, np.exp(z), 0.0)
        denom = e.sum(axis=1, keepdims=True)
        denom[denom == 0.0] = 1.0
        return e / denom

    # --- training step (masked softmax cross-entropy) ----------------------
    def train_epoch(self, X, valid, y, sample_w, lr, batch_size, rng,
                    weight_decay=0.0, freeze_layer0=False):
        """One SGD epoch with MASKED softmax CE and per-sample class weighting.
        valid: bool (N, K) eligibility mask. y: int class 0..17.
        sample_w: per-row loss/grad weight (inverse class freq). Returns mean
        weighted loss.

        weight_decay: optional L2 penalty on the weights (NOT biases). Default 0.0
        leaves the BC path byte-identical; the AWR trainer passes a nonzero value
        to keep weights in the I16F16-quantization-safe range (large unbounded
        logit separation under AWR's exp-advantage weighting otherwise overflows
        the int32 weight / int64 accumulator at inference)."""
        n = X.shape[0]
        perm = rng.permutation(n)
        total_loss = 0.0
        total_w = 0.0
        for start in range(0, n, batch_size):
            bidx = perm[start:start + batch_size]
            xb, vb, yb, wb = X[bidx], valid[bidx], y[bidx], sample_w[bidx]
            logits = self.forward(xb, cache=True)
            probs = self._masked_softmax(logits, vb)
            m = xb.shape[0]
            ll = -np.log(probs[np.arange(m), yb] + 1e-12)
            total_loss += float((ll * wb).sum())
            total_w += float(wb.sum())
            # gradient of masked-softmax-CE wrt logits, scaled per-sample.
            dlogits = probs.copy()
            dlogits[np.arange(m), yb] -= 1.0
            # zero gradient on ineligible columns (they contribute no probability
            # mass; their logits are free and must not be pushed).
            dlogits = np.where(vb, dlogits, 0.0)
            dlogits *= wb[:, None]  # per-sample (inverse class freq) weight
            dlogits /= m
            self._backward(dlogits, lr, weight_decay, freeze_layer0)
        return total_loss / max(total_w, 1.0)

    def _backward(self, dlogits, lr, weight_decay=0.0, freeze_layer0=False):
        acts = self._cache["acts"]
        pre = self._cache["pre"]
        grad = dlogits
        for li in reversed(range(len(self.W))):
            a_prev = acts[li]
            dW = a_prev.T @ grad
            db = grad.sum(axis=0)
            if li > 0:
                dprev = grad @ self.W[li].T
                dprev = dprev * (pre[li - 1] > 0.0)
                grad = dprev
            # Optionally freeze layer 0: it encodes the (folded) input scaling.
            # Holding it at the BC values keeps the per-feature activation
            # magnitudes identical to BC, which is what guarantees the I16F16
            # accumulator never overflows on large raw features (swim*/tick).
            if freeze_layer0 and li == 0:
                continue
            if weight_decay:
                dW = dW + weight_decay * self.W[li]  # L2 on weights only
            self.W[li] -= lr * dW
            self.b[li] -= lr * db

    # --- inference helpers -------------------------------------------------
    def logits(self, X_raw):
        return self.forward(X_raw, cache=False)

    # --- export ------------------------------------------------------------
    def folded_layer0(self):
        s = 1.0 / self.sigma
        W0 = self.W[0]
        W0_fold = W0 * s[:, None]
        b0_fold = self.b[0] - (self.mu @ W0_fold)
        return W0_fold, b0_fold

    def export_f32_json(self, path, feature_names):
        W0_fold, b0_fold = self.folded_layer0()
        layers = []
        for li in range(len(self.W)):
            if li == 0:
                W_in_out, b = W0_fold, b0_fold
            else:
                W_in_out, b = self.W[li], self.b[li]
            W_out_in = np.asarray(W_in_out, dtype=np.float32).T  # contract W[out][in]
            layers.append({
                "W": W_out_in.astype(np.float32).tolist(),
                "b": np.asarray(b, dtype=np.float32).tolist(),
            })
        doc = {
            "format": "cortex-decide-mlp-f32-v1",
            "arch": list(ARCH),
            "activation": ACTIVATION,
            "normalization_folded": True,
            "input_features": list(feature_names),
            "layers": layers,
        }
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(doc, fh)
        return doc


def f32_forward_from_json(doc, X_raw):
    """Reference f32 forward pass reading the exported JSON directly. Consumes
    RAW features (normalization folded). Returns logits (N, 18) as float32."""
    a = np.asarray(X_raw, dtype=np.float32)
    layers = doc["layers"]
    for li, layer in enumerate(layers):
        W_out_in = np.asarray(layer["W"], dtype=np.float32)
        b = np.asarray(layer["b"], dtype=np.float32)
        z = (a @ W_out_in.T + b).astype(np.float32)
        if li < len(layers) - 1:
            a = np.maximum(z, np.float32(0.0))
        else:
            a = z
    return a
