# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — trajectory CSV loader + gid-join utility for the BC trainer.
# See docs/AI/cortex/ML_CONTRACT.md (the binding spec) and PILOT.md.
#
# numpy-only, dependency-light, deterministic.

import glob as _glob
import os
import re
import numpy as np

# Feature order is the contract's exact index order (ML_CONTRACT.md "Feature
# vector"). These are exactly the trace CSV columns minus the metadata
# (tick, team, swarm_index, gid) and the label (desired).
FEATURE_NAMES = [
    "corn", "maxCorn", "maxUnitWorking", "unitsInside", "maxUnitInside",
    "nearestWheatDist", "harvestableWheatNearby", "freeWorkers", "totalFree",
    "totalNeeded", "workers", "swarmCount", "feedCapacity", "starvingUnits",
    "needFood", "maxBuildLevel",
]
NUM_FEATURES = len(FEATURE_NAMES)  # 16
LABEL_NAME = "desired"
META_NAMES = ["tick", "team", "swarm_index", "gid"]


def _constants_header_path():
    here = os.path.dirname(os.path.abspath(__file__))
    # tools/cortex-ml -> tools -> glob2
    return os.path.normpath(
        os.path.join(here, "..", "..", "src", "ai", "cortex", "CortexConstants.h")
    )


def load_constants(header_path=None):
    """Read the cap/clamp constants straight out of CortexConstants.h so the
    trainer never hardcodes a value the C++ side might change. Returns a dict of
    the names the contract's inference rule and masking need."""
    if header_path is None:
        header_path = _constants_header_path()
    wanted = [
        "CORTEX_SWARM_WORKER_CAP",
        "CORTEX_SWARM_WORKER_CAP_LATE",
        "CORTEX_SWARM_CAP_LIFT_BUILDLEVEL",
        "CORTEX_SWARM_WHEAT_STARVED_TILES",
        "CORTEX_SWARM_WHEAT_STARVED_WORKER_CAP",
        "CORTEX_SWARM_WORKER_MIN",
        "CORTEX_MAX_BUILDING_WORKERS",
        # corn band thresholds — used by the RL reward (reward.py), not BC.
        "CORTEX_SWARM_CORN_ADD_LO",
        "CORTEX_SWARM_CORN_REM_HI",
    ]
    text = open(header_path, "r", encoding="utf-8").read()
    out = {}
    for name in wanted:
        # match: static const int NAME = 123;
        m = re.search(
            r"\bstatic\s+const\s+int\s+" + re.escape(name) + r"\s*=\s*(-?\d+)\s*;",
            text,
        )
        if not m:
            raise ValueError(f"could not find {name} in {header_path}")
        out[name] = int(m.group(1))
    return out


def swarm_worker_cap(max_build_level, free_workers, consts):
    """swarmWorkerCap(obs) per ML_CONTRACT.md step 3:
    late cap if maxBuildLevel >= CAP_LIFT && freeWorkers > 0 else base cap."""
    if (max_build_level >= consts["CORTEX_SWARM_CAP_LIFT_BUILDLEVEL"]
            and free_workers > 0):
        return consts["CORTEX_SWARM_WORKER_CAP_LATE"]
    return consts["CORTEX_SWARM_WORKER_CAP"]


def _read_one_csv(path):
    """Read one trace CSV into a structured array of float64 columns keyed by
    header name. Validates the header against the contract."""
    with open(path, "r", encoding="utf-8") as fh:
        header_line = fh.readline().strip()
    header = [h.strip() for h in header_line.split(",")]
    expected = META_NAMES + FEATURE_NAMES + [LABEL_NAME]
    if header != expected:
        raise ValueError(
            f"{path}: header mismatch.\n  got:      {header}\n  expected: {expected}"
        )
    raw = np.loadtxt(path, delimiter=",", skiprows=1, ndmin=2)
    if raw.size == 0:
        raw = raw.reshape(0, len(expected))
    cols = {name: raw[:, i] for i, name in enumerate(header)}
    return cols


class TraceFile:
    """One trace CSV = one game+team. Holds raw columns and the contract feature
    matrix / labels for that file. gid is unique only within this file."""

    def __init__(self, path):
        self.path = path
        self.cols = _read_one_csv(path)
        n = len(self.cols["gid"])
        self.X = np.empty((n, NUM_FEATURES), dtype=np.float64)
        for j, name in enumerate(FEATURE_NAMES):
            self.X[:, j] = self.cols[name]
        # label desired in 1..20 -> class index 0..19
        self.desired = self.cols[LABEL_NAME].astype(np.int64)
        self.y = self.desired - 1
        self.gid = self.cols["gid"].astype(np.int64)
        self.tick = self.cols["tick"].astype(np.int64)

    def __len__(self):
        return self.X.shape[0]

    def wheat_starved_mask(self, consts):
        """Rows governed by the hard C++ clamp:
        harvestableWheatNearby in [0, CORTEX_SWARM_WHEAT_STARVED_TILES)."""
        hw = self.cols["harvestableWheatNearby"]
        thr = consts["CORTEX_SWARM_WHEAT_STARVED_TILES"]
        return (hw >= 0) & (hw < thr)

    def transitions(self):
        """gid-join for the LATER RL reward step: pair consecutive decision
        cycles by gid WITHIN this file to yield (s_t, a_t, s_{t+1}) tuples.

        One swarm's history is the rows sharing a gid, ordered by tick. Each
        adjacent pair (sorted by tick) is a transition. Joining never crosses
        files because TraceFile holds a single game+team. BC does not use this;
        it exists so the RL step can compute reward(s_t, a_t, s_{t+1}).

        Returns a list of dicts: {s_t, a_t, s_next, gid, tick_t, tick_next}.
        """
        out = []
        order = np.argsort(self.gid, kind="stable")
        g_sorted = self.gid[order]
        # group rows by gid
        for g in np.unique(self.gid):
            idx = order[g_sorted == g]
            # within the group, order by tick (stable on original order)
            idx = idx[np.argsort(self.tick[idx], kind="stable")]
            for a, b in zip(idx[:-1], idx[1:]):
                out.append({
                    "s_t": self.X[a],
                    "a_t": int(self.desired[a]),
                    "s_next": self.X[b],
                    "gid": int(g),
                    "tick_t": int(self.tick[a]),
                    "tick_next": int(self.tick[b]),
                })
        return out


def find_trace_files(data_path):
    """Accept a directory (globbed for *.team*.csv), a glob string, or a single
    file. Returns a sorted list of paths."""
    if os.path.isdir(data_path):
        paths = _glob.glob(os.path.join(data_path, "*.team*.csv"))
    elif any(ch in data_path for ch in "*?["):
        paths = _glob.glob(data_path)
    elif os.path.isfile(data_path):
        paths = [data_path]
    else:
        paths = []
    return sorted(paths)


def load_dataset(data_path, consts, include_wheat_starved=False):
    """Load every trace file under data_path and concatenate into a BC dataset.

    Excludes wheat-starved rows by default (contract: those rows are governed by
    a hard C++ rule that bypasses the net; the net should learn buffer control,
    not memorise a clamp that C++ applies regardless). Pass
    include_wheat_starved=True to keep them in case we revisit.

    Returns (X, y, info) where info has per-file accounting and the file list.
    """
    paths = find_trace_files(data_path)
    if not paths:
        raise FileNotFoundError(f"no trace CSVs found at {data_path!r}")

    files = [TraceFile(p) for p in paths]
    Xs, ys = [], []
    n_total = 0
    n_starved = 0
    for tf in files:
        n_total += len(tf)
        starved = tf.wheat_starved_mask(consts)
        n_starved += int(starved.sum())
        keep = np.ones(len(tf), dtype=bool) if include_wheat_starved else ~starved
        Xs.append(tf.X[keep])
        ys.append(tf.y[keep])

    X = np.concatenate(Xs, axis=0) if Xs else np.empty((0, NUM_FEATURES))
    y = np.concatenate(ys, axis=0) if ys else np.empty((0,), dtype=np.int64)
    info = {
        "files": paths,
        "n_files": len(paths),
        "rows_total": n_total,
        "rows_wheat_starved": n_starved,
        "rows_used": int(X.shape[0]),
        "include_wheat_starved": include_wheat_starved,
    }
    return X, y, info
