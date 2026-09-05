# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex ML pilot — offline-RL reward + trajectory builder (step 6).
#
# Implements the dense, local, per-swarm reward from docs/AI/cortex/PILOT.md and
# turns the raw trace CSVs into discounted-return transitions for offline RL
# (AWR / CQL). All reward logic lives HERE in Python so shaping iterates without
# recompiling the engine (PILOT.md "Reward").
#
# A transition is one decision cycle of one swarm: (s_t, a_t, r_t, s_{t+1}),
# joined by `gid` WITHIN a single trace file (one game+team). a_t is the cap the
# BEHAVIOUR policy actually set that cycle (`desired`); r_t is a function of
# (s_t, a_t, s_{t+1}) per the spec — the action at t shows up in the buffer at
# t+1. Trajectories never cross files (gid is unique only within a game+team).
#
# numpy-only, deterministic.

import numpy as np

from dataset import (
    TraceFile, find_trace_files, load_constants, FEATURE_NAMES, NUM_FEATURES,
)

# Feature indices (ML_CONTRACT.md order) used by the reward.
F_CORN = FEATURE_NAMES.index("corn")
F_MAXUNITWORKING = FEATURE_NAMES.index("maxUnitWorking")
F_HARVEST = FEATURE_NAMES.index("harvestableWheatNearby")
F_FREEWORKERS = FEATURE_NAMES.index("freeWorkers")    # 7
F_TOTALNEEDED = FEATURE_NAMES.index("totalNeeded")    # 9

# --- reward shaping weights (tunable; the whole point of keeping reward in
# Python is that these iterate without an engine rebuild). Signs per PILOT.md.
# Every term below is SWARM-LOCAL and controllable by the lever the net sets (the
# swarm's worker cap). The PILOT.md "colony coupling" term (−starvingUnits) is
# DELIBERATELY OMITTED: starvingUnits is a colony-wide FEEDING outcome driven by
# inns, which this head does not tune, so the swarm cap can only move it through
# the weak shared-worker-pool path — and the same colony-wide number is stamped on
# every swarm's transition each tick, so it adds variance to the return without
# discriminating good vs bad cap choices. Dropped per user review (2026-06-07).
W_IN_BAND = 1.0          # + producing, not hoarding
W_STALL_BASE = 1.0       # - production halts (corn < ADD_LO); plus depth term
W_STALL_DEPTH = 0.5      # - extra per corn below ADD_LO
W_HOARD = 0.05           # - per excess worker while saturated (corn >= REM_HI)
W_OSC = 0.05             # - per unit of |Δ maxUnitWorking|
W_WHEAT_WASTE = 0.5      # - haulers assigned to a wheat-starved catchment

# Colony labor-shortfall coupling (added per user review 2026-06-07). The hand
# rule's swarm loop is PURELY LOCAL (each swarm's own corn buffer) and blind to
# colony-wide labor supply — yet freeWorkers/totalNeeded are already in the net's
# input, so rewarding this is a global signal the teacher ignores and a concrete
# way the learned policy can beat it. A swarm holding workers it can RELEASE while
# the colony has more open jobs than idle workers shares the blame for the crunch;
# dropping its cap frees haulers for higher-priority inns/sites. Per-swarm-attributed
# (weighted by THIS swarm's releasable workers, a_t-MIN) to avoid the colony-wide-
# smear dilution that sank the starvingUnits term. Deadband K: 'a few open jobs is
# fine' (a new building ramping); only a SUSTAINED gap beyond K is penalised, and
# the γ-return makes sustained ~10x a one-cycle transient automatically. Measured
# on s_next (the action's effect on totalNeeded shows in the next observation).
W_SHORTFALL = 0.05       # - per releasable worker during a labor crunch
K_SHORTFALL = 4          # deadband: unfilled jobs tolerated before penalising

GAMMA = 0.9              # discount over decision cycles


def _reward_row(s_t, a_t, s_next, consts):
    """Dense local reward for one transition (PILOT.md 'Reward').

    s_t, s_next: 16-feature float vectors (contract order). a_t: cap chosen at t
    (1..20). Returns a float. The buffer outcome of a_t is read from s_next.corn;
    oscillation/wheat-waste read the action and the pre-decision state at t.
    """
    add_lo = consts["CORTEX_SWARM_CORN_ADD_LO"]
    rem_hi = consts["CORTEX_SWARM_CORN_REM_HI"]
    starved_tiles = consts["CORTEX_SWARM_WHEAT_STARVED_TILES"]
    worker_min = consts["CORTEX_SWARM_WORKER_MIN"]

    corn_next = s_next[F_CORN]
    r = 0.0

    # corn band on the RESULTING buffer (action at t -> buffer at t+1).
    if corn_next < add_lo:
        # stall: strong negative scaling with how deep below the stall line.
        depth = add_lo - corn_next
        r -= W_STALL_BASE + W_STALL_DEPTH * depth
    elif corn_next < rem_hi:
        r += W_IN_BAND            # in-band: producing, not hoarding
    else:
        # hoard: small negative proportional to the excess workers we committed.
        excess = max(0.0, a_t - worker_min)
        r -= W_HOARD * excess

    # oscillation: penalise churn in the cap relative to the pre-decision count.
    r -= W_OSC * abs(a_t - s_t[F_MAXUNITWORKING])

    # wheat waste: extra haulers on a starved catchment can't find wheat.
    harvest_t = s_t[F_HARVEST]
    if 0 <= harvest_t < starved_tiles and a_t > worker_min:
        r -= W_WHEAT_WASTE

    # colony labor shortfall: more open jobs than idle workers (a crunch). This
    # swarm shares the blame up to the workers it could release (a_t - MIN), but
    # only beyond the deadband K (a few open jobs are normal / a ramping build).
    shortfall = (s_next[F_TOTALNEEDED] - s_next[F_FREEWORKERS]) - K_SHORTFALL
    if shortfall > 0:
        releasable = a_t - worker_min
        if releasable > 0:
            r -= W_SHORTFALL * min(releasable, shortfall)

    return r


def build_transitions(data_path, consts=None):
    """Load every trace file under data_path and build per-swarm transitions.

    Returns a dict of numpy arrays, all aligned row-for-row:
      S       (N, 16) float64  state s_t
      A       (N,)    int64    action a_t (cap 1..20)
      Acls    (N,)    int64    action class a_t - 1 (0..19)
      R       (N,)    float64  reward r_t = reward(s_t, a_t, s_{t+1})
      Snext   (N, 16) float64  state s_{t+1}
      done    (N,)    bool     True if s_{t+1} is the last cycle of its trajectory
      Ret     (N,)    float64  discounted return-to-go within the trajectory
    plus 'info' with per-file accounting.

    Wheat-starved rows (the hard C++ clamp) are NOT excluded here: they carry
    valid transitions and their reward (wheat-waste term) is informative. The
    net never RUNS on them at inference (the clamp bypasses it), but training on
    the surrounding dynamics is harmless and keeps trajectories contiguous.
    """
    if consts is None:
        consts = load_constants()
    paths = find_trace_files(data_path)
    if not paths:
        raise FileNotFoundError(f"no trace CSVs found at {data_path!r}")

    S, A, R, Snext, done, Ret = [], [], [], [], [], []
    n_files = 0
    n_rows = 0
    n_trans = 0
    n_traj = 0
    for p in paths:
        tf = TraceFile(p)
        n_files += 1
        n_rows += len(tf)
        # group rows by gid, order each group by tick (a swarm's history).
        order = np.argsort(tf.gid, kind="stable")
        g_sorted = tf.gid[order]
        for g in np.unique(tf.gid):
            idx = order[g_sorted == g]
            idx = idx[np.argsort(tf.tick[idx], kind="stable")]
            if len(idx) < 2:
                continue
            n_traj += 1
            # build the ordered (s, a, r) sequence, then discounted return-to-go.
            traj_s, traj_a, traj_r, traj_sn = [], [], [], []
            for a, b in zip(idx[:-1], idx[1:]):
                s_t = tf.X[a]
                a_t = int(tf.desired[a])
                s_n = tf.X[b]
                r_t = _reward_row(s_t, a_t, s_n, consts)
                traj_s.append(s_t)
                traj_a.append(a_t)
                traj_r.append(r_t)
                traj_sn.append(s_n)
            m = len(traj_r)
            n_trans += m
            # discounted return-to-go, last transition terminal.
            ret = np.zeros(m, dtype=np.float64)
            acc = 0.0
            for t in range(m - 1, -1, -1):
                acc = traj_r[t] + GAMMA * acc
                ret[t] = acc
            for t in range(m):
                S.append(traj_s[t])
                A.append(traj_a[t])
                R.append(traj_r[t])
                Snext.append(traj_sn[t])
                done.append(t == m - 1)
                Ret.append(ret[t])

    S = np.asarray(S, dtype=np.float64).reshape(-1, NUM_FEATURES)
    A = np.asarray(A, dtype=np.int64)
    Snext = np.asarray(Snext, dtype=np.float64).reshape(-1, NUM_FEATURES)
    # Wheat-starved rows: the hard C++ clamp bypasses the net at inference, so the
    # net never makes a decision there. Trainers exclude these from the policy/Q
    # LOSS (matching BC + inference), but they stay in the trajectory so returns
    # and bootstrap targets see the real dynamics. Threshold from the header.
    thr = consts["CORTEX_SWARM_WHEAT_STARVED_TILES"]
    starved = (S[:, F_HARVEST] >= 0) & (S[:, F_HARVEST] < thr)
    starved_next = (Snext[:, F_HARVEST] >= 0) & (Snext[:, F_HARVEST] < thr)
    out = {
        "S": S,
        "A": A,
        "Acls": A - 1,
        "R": np.asarray(R, dtype=np.float64),
        "Snext": Snext,
        "done": np.asarray(done, dtype=bool),
        "Ret": np.asarray(Ret, dtype=np.float64),
        "starved": starved,
        "starved_next": starved_next,
        "info": {
            "n_files": n_files,
            "rows_total": n_rows,
            "n_trajectories": n_traj,
            "n_transitions": n_trans,
            "n_starved": int(starved.sum()),
        },
    }
    return out


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Build RL transitions + reward stats")
    ap.add_argument("--data", required=True)
    args = ap.parse_args()
    consts = load_constants()
    d = build_transitions(args.data, consts)
    info = d["info"]
    print(f"files={info['n_files']} rows={info['rows_total']} "
          f"trajectories={info['n_trajectories']} transitions={info['n_transitions']}")
    R = d["R"]
    print(f"reward: mean={R.mean():.4f} std={R.std():.4f} "
          f"min={R.min():.4f} max={R.max():.4f}")
    print(f"return: mean={d['Ret'].mean():.4f} std={d['Ret'].std():.4f}")
    # action histogram
    vals, counts = np.unique(d["A"], return_counts=True)
    print("action histogram (cap -> count):")
    for v, c in zip(vals, counts):
        print(f"    {int(v):2d}: {int(c)}")
