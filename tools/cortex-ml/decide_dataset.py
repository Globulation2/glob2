# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex DECIDE pilot — trajectory CSV loader for the decision-selection BC
# trainer. Sibling of dataset.py (worker-cap). See
# docs/AI/cortex/DECIDE_CONTRACT.md (the binding spec) and DECIDE_PILOT.md.
#
# numpy-only, dependency-light, deterministic.

import glob as _glob
import os
import numpy as np

# Feature order is the DECIDE_CONTRACT.md exact index order (48 features). These
# are exactly the trace CSV columns minus the metadata (tick, team) and the
# labels (eligible_mask, chosen). NOTE the CSV repeats a `tick` column at index
# 49 — that is the FEATURE `tick` (DECIDE idx 47), distinct from the leading
# metadata `tick` at column 0.
FEATURE_NAMES = [
    "swarms", "swarmSites", "inns", "innSites", "school", "schoolSites",
    "race", "raceSites", "heal", "healSites", "barracks", "barracksSites",
    "upgradableCount", "totalUnit", "workers", "explorers", "warriors",
    "freeWorkers", "totalFree", "totalNeeded", "fillableNeeded",
    "unfillableNeeded", "feedCapacity", "needFood", "starvingUnits",
    "maxBuildLevel", "swarmCount", "innCount", "swarmsProducing",
    "swarmsProducingWorker", "swarmsProducingWarrior", "swarmsProducingExplorer",
    "attackStrengthLevel", "walkLevel", "buildLevel", "buildingsUnderAttack",
    "unitsUnderAttack", "warFlagsActive", "enemyCount", "enemyUnitsNearFlag",
    "flagTargetsValid", "flagPosture", "haveDefenseTarget", "algaeReachable",
    "algaeDiscovered", "swimLandReach", "swimWaterReach", "tick",
]
NUM_FEATURES = len(FEATURE_NAMES)  # 48
NUM_CLASSES = 18                   # decide() candidate count
LABEL_MASK = "eligible_mask"
LABEL_CHOSEN = "chosen"
META_NAMES = ["tick", "team"]


def _read_one_csv(path):
    """Read one trace CSV into a dict of float64 columns keyed by header name.
    Validates the header against the contract (metadata + 48 features + labels)."""
    with open(path, "r", encoding="utf-8") as fh:
        header_line = fh.readline().strip()
    header = [h.strip() for h in header_line.split(",")]
    expected = META_NAMES + FEATURE_NAMES + [LABEL_MASK, LABEL_CHOSEN]
    if header != expected:
        raise ValueError(
            f"{path}: header mismatch.\n  got:      {header}\n  expected: {expected}"
        )
    raw = np.loadtxt(path, delimiter=",", skiprows=1, ndmin=2)
    if raw.size == 0:
        raw = raw.reshape(0, len(expected))
    return raw, header


class DecideTraceFile:
    """One trace CSV = one game+team. Holds the contract feature matrix, the
    eligibility mask, and the chosen-class label."""

    def __init__(self, path):
        self.path = path
        raw, header = _read_one_csv(path)
        # features are columns 2 .. 2+48 (after tick, team)
        f0 = len(META_NAMES)
        self.X = raw[:, f0:f0 + NUM_FEATURES].astype(np.float64)
        self.mask = raw[:, f0 + NUM_FEATURES + 0].astype(np.int64)      # eligible_mask
        self.chosen = raw[:, f0 + NUM_FEATURES + 1].astype(np.int64)    # chosen (-1 = hold)
        self.tick = raw[:, 0].astype(np.int64)

    def __len__(self):
        return self.X.shape[0]


def find_trace_files(data_path):
    """Accept a directory (globbed for *.csv), a glob string, or a single file."""
    if os.path.isdir(data_path):
        paths = _glob.glob(os.path.join(data_path, "*.csv"))
    elif any(ch in data_path for ch in "*?["):
        paths = _glob.glob(data_path)
    elif os.path.isfile(data_path):
        paths = [data_path]
    else:
        paths = []
    return sorted(paths)


def load_files(data_path):
    """Load every trace CSV under data_path. Returns a list of DecideTraceFile."""
    paths = find_trace_files(data_path)
    if not paths:
        raise FileNotFoundError(f"no trace CSVs found at {data_path!r}")
    return [DecideTraceFile(p) for p in paths]


def split_files(files, val_frac, seed):
    """Hold out a fraction of FILES (games) as validation — split by game, not by
    row, to avoid leakage. Returns (train_files, val_files)."""
    rng = np.random.default_rng(seed)
    order = rng.permutation(len(files))
    n_val = int(len(files) * val_frac)
    val_idx = set(order[:n_val].tolist())
    train = [f for i, f in enumerate(files) if i not in val_idx]
    val = [f for i, f in enumerate(files) if i in val_idx]
    return train, val


def assemble(files):
    """Concatenate the action rows (chosen != -1) of the given files into a BC
    dataset. Returns (X, mask, y) where y == chosen class index (0..17).
    Rows with chosen == -1 (the hand rule held) are excluded — there is no BC
    target for a hold."""
    Xs, Ms, Ys = [], [], []
    for tf in files:
        sel = tf.chosen != -1
        Xs.append(tf.X[sel])
        Ms.append(tf.mask[sel])
        Ys.append(tf.chosen[sel])
    X = np.concatenate(Xs, axis=0) if Xs else np.empty((0, NUM_FEATURES))
    M = np.concatenate(Ms, axis=0) if Ms else np.empty((0,), dtype=np.int64)
    Y = np.concatenate(Ys, axis=0) if Ys else np.empty((0,), dtype=np.int64)
    return X, M, Y


def popcount(masks):
    """Number of set bits per mask value (vectorized over a 1-D int array)."""
    out = np.zeros(masks.shape[0], dtype=np.int64)
    m = masks.copy()
    while np.any(m):
        out += (m & 1)
        m >>= 1
    return out
