# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Globulation 2 Authors
#
# Cortex DECIDE pilot — offline reward module for the decision-selection AWR
# trainer. Joins the per-(team,game) trajectory CSVs to their game log's
# terminal winner, builds a FOW-respecting own-colony-strength potential Phi,
# and emits potential-based shaped rewards + Monte-Carlo returns per episode.
#
# Variant A (own strength only): Phi is built from the 48 logged features; the
# terminal +/-1 carries the opponent signal. All reward/shaping logic lives here
# (Python) so it iterates without recompiling. See DECIDE_PILOT.md (Reward).
#
# numpy-only, deterministic.

import glob as _glob
import os
import re
import numpy as np

from decide_dataset import (
    DecideTraceFile, FEATURE_NAMES, NUM_FEATURES, find_trace_files,
)

# --- reward / shaping constants (TUNABLE — top of file by design) ----------
GAMMA = 0.997          # per decision-cycle discount
LAMBDA = 1.0           # shaping scale
W_MIL = 1.0            # potential weight: military
W_ECO = 1.0            # potential weight: economy
W_RISK = 1.0           # potential weight: risk (subtracted)

# Feature indices into the 48-vector (DECIDE_CONTRACT order). Sanity-checked
# against FEATURE_NAMES at import so a contract reorder is caught loudly.
F_WARRIORS = 16
F_ATTACKSTRENGTHLEVEL = 32
F_WARFLAGSACTIVE = 37
F_WORKERS = 14
F_SWARMS = 0
F_FEEDCAPACITY = 22
F_INNS = 2
F_MAXBUILDLEVEL = 25
F_STARVINGUNITS = 24
F_TOTALUNIT = 13
F_BUILDINGSUNDERATTACK = 35

_EXPECT = {
    F_WARRIORS: "warriors", F_ATTACKSTRENGTHLEVEL: "attackStrengthLevel",
    F_WARFLAGSACTIVE: "warFlagsActive", F_WORKERS: "workers",
    F_SWARMS: "swarms", F_FEEDCAPACITY: "feedCapacity", F_INNS: "inns",
    F_MAXBUILDLEVEL: "maxBuildLevel", F_STARVINGUNITS: "starvingUnits",
    F_TOTALUNIT: "totalUnit", F_BUILDINGSUNDERATTACK: "buildingsUnderAttack",
}
for _idx, _name in _EXPECT.items():
    if FEATURE_NAMES[_idx] != _name:
        raise AssertionError(
            "decide_reward feature index drift: idx %d is %r, expected %r"
            % (_idx, FEATURE_NAMES[_idx], _name))

# The raw "strength" quantities Phi is built from, before normalization. The
# norm() divisor for each is the corpus 95th percentile of that quantity, so
# every term is ~O(1) regardless of map/economy scale.
_NORM_KEYS = ("mil", "eco")


# --- terminal label join ---------------------------------------------------
_END_RE = re.compile(r"GLOB2_GAME_END\b.*?winner_team=(-?\d+)")
_FILE_RE = re.compile(r"^(?P<prefix>.+)\.team(?P<team>\d+)\.csv$")


def _log_path_for(csv_path):
    """The `<prefix>.log` that records the terminal winner for a `<prefix>.team<N>.csv`."""
    base = os.path.basename(csv_path)
    m = _FILE_RE.match(base)
    if not m:
        raise ValueError("trace file name does not match <prefix>.team<N>.csv: %r" % base)
    prefix = m.group("prefix")
    team = int(m.group("team"))
    return os.path.join(os.path.dirname(csv_path), prefix + ".log"), team


def winner_team_of(log_path):
    """Parse `winner_team=K` from the log's GLOB2_GAME_END line. Returns K
    (>=0 decisive winner; -1 = draw/timeout / no decisive winner)."""
    with open(log_path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "GLOB2_GAME_END" in line:
                m = _END_RE.search(line)
                if m:
                    return int(m.group(1))
    raise ValueError("no GLOB2_GAME_END winner_team in %r" % log_path)


def terminal_reward(csv_path):
    """R_terminal for a `<prefix>.team<N>.csv` stream:
       +1 if winner==N, -1 if a decisive other team won, 0 on draw/timeout (-1)."""
    log_path, team = _log_path_for(csv_path)
    winner = winner_team_of(log_path)
    if winner < 0:
        return 0.0
    return 1.0 if winner == team else -1.0


# --- potential Phi ---------------------------------------------------------
def raw_strength_terms(X):
    """Compute the un-normalized (mil, eco, risk) terms from a feature matrix X
    (N, 48). Returns a dict of (N,) arrays. FOW-respecting: only own-colony
    fields are read (variant A)."""
    warriors = X[:, F_WARRIORS]
    attack_lvl = X[:, F_ATTACKSTRENGTHLEVEL]
    war_flags = X[:, F_WARFLAGSACTIVE]
    workers = X[:, F_WORKERS]
    swarms = X[:, F_SWARMS]
    feed_cap = X[:, F_FEEDCAPACITY]
    inns = X[:, F_INNS]
    max_build = X[:, F_MAXBUILDLEVEL]
    starving = X[:, F_STARVINGUNITS]
    total_unit = X[:, F_TOTALUNIT]
    bua = X[:, F_BUILDINGSUNDERATTACK]

    mil = warriors * (1.0 + 0.5 * attack_lvl) + 0.5 * war_flags
    # economic capacity: workforce + swarm production scaled by food/tech capacity.
    eco = workers + 1.5 * swarms * (1.0 + feed_cap + inns + max_build)
    risk = starving / np.maximum(total_unit, 1.0) + 0.25 * bua
    return {"mil": mil, "eco": eco, "risk": risk}


def fit_norms(X_all):
    """Corpus 95th-percentile divisors for the mil/eco terms (so norm(x) ~ O(1)).
    Guards a zero/degenerate percentile to 1.0. risk is already in [0, ~]."""
    terms = raw_strength_terms(X_all)
    norms = {}
    for k in _NORM_KEYS:
        p95 = float(np.percentile(terms[k], 95))
        norms[k] = p95 if p95 > 1e-9 else 1.0
    return norms


def phi(X, norms):
    """Own-colony-strength potential in [-1, 1] for each row of X (N, 48):
       Phi = tanh( w_mil*norm(mil) + w_eco*norm(eco) - w_risk*risk )."""
    terms = raw_strength_terms(X)
    mil_n = terms["mil"] / norms["mil"]
    eco_n = terms["eco"] / norms["eco"]
    z = W_MIL * mil_n + W_ECO * eco_n - W_RISK * terms["risk"]
    return np.tanh(z)


# --- per-episode transition / reward assembly ------------------------------
class Episode:
    """One (team, game) stream ordered by tick. Holds the full per-cycle feature
    matrix (X), eligibility masks, chosen labels, the shaped per-step reward r,
    and the Monte-Carlo return G computed over ALL cycle rows (holds included)."""

    def __init__(self, tf, R_terminal, norms):
        order = np.argsort(tf.tick, kind="stable")
        self.X = tf.X[order]
        self.mask = tf.mask[order]
        self.chosen = tf.chosen[order]
        self.tick = tf.tick[order]
        self.path = tf.path
        self.R_terminal = float(R_terminal)

        n = self.X.shape[0]
        ph = phi(self.X, norms)
        self.phi = ph
        # potential-based shaped reward: r_t = lambda*(gamma*Phi(s_{t+1}) - Phi(s_t))
        # for non-terminal steps; the terminal +/-1 is added at the last step.
        r = np.zeros(n, dtype=np.float64)
        if n >= 2:
            r[:-1] = LAMBDA * (GAMMA * ph[1:] - ph[:-1])
        if n >= 1:
            r[-1] += self.R_terminal
        self.r = r
        # Monte-Carlo return G_t = sum_k gamma^k r_{t+k} over the whole episode.
        G = np.zeros(n, dtype=np.float64)
        acc = 0.0
        for t in range(n - 1, -1, -1):
            acc = r[t] + GAMMA * acc
            G[t] = acc
        self.G = G

    def __len__(self):
        return self.X.shape[0]


def build_episodes(files, norms):
    """Wrap each loaded DecideTraceFile in an Episode with its terminal reward."""
    eps = []
    for tf in files:
        Rt = terminal_reward(tf.path)
        eps.append(Episode(tf, Rt, norms))
    return eps


def load_all_episodes(data_path):
    """Convenience: load every trace CSV under data_path, fit corpus norms over
    ALL rows, return (episodes, norms). Used by the sanity-gate and trainer."""
    paths = find_trace_files(data_path)
    if not paths:
        raise FileNotFoundError("no trace CSVs found at %r" % data_path)
    files = [DecideTraceFile(p) for p in paths]
    X_all = np.concatenate([f.X for f in files], axis=0)
    norms = fit_norms(X_all)
    eps = build_episodes(files, norms)
    return eps, norms


# --- sanity gate: is Phi predictive of the terminal outcome? ----------------
def midgame_phi_outcome_corr(episodes, mid_tick=8000, window=2000):
    """Correlation between Phi at mid-game and R_terminal across episodes. Picks,
    per episode, the row whose tick is closest to mid_tick (within `window`);
    skips episodes with no row in range or with R_terminal==0 (draws carry no
    decisive signal). Returns (corr, n_used, phi_values, R_values)."""
    phis = []
    Rs = []
    for ep in episodes:
        if ep.R_terminal == 0.0:
            continue
        d = np.abs(ep.tick - mid_tick)
        j = int(np.argmin(d))
        if d[j] > window:
            continue
        phis.append(float(ep.phi[j]))
        Rs.append(ep.R_terminal)
    phis = np.asarray(phis)
    Rs = np.asarray(Rs)
    if phis.size < 2 or phis.std() < 1e-12 or Rs.std() < 1e-12:
        return float("nan"), phis.size, phis, Rs
    corr = float(np.corrcoef(phis, Rs)[0, 1])
    return corr, phis.size, phis, Rs


def main():
    """CLI: report the Phi<->outcome sanity gate + Phi distribution over a corpus."""
    import argparse
    ap = argparse.ArgumentParser(description="Cortex DECIDE reward sanity gate.")
    ap.add_argument("--data", required=True, help="dir / glob / file of *.csv traces")
    ap.add_argument("--mid-tick", type=int, default=8000)
    ap.add_argument("--window", type=int, default=2000)
    args = ap.parse_args()

    eps, norms = load_all_episodes(args.data)
    n_decisive = sum(1 for e in eps if e.R_terminal != 0.0)
    n_draw = sum(1 for e in eps if e.R_terminal == 0.0)
    print("episodes: %d (%d decisive, %d draw/timeout)"
          % (len(eps), n_decisive, n_draw))
    print("norms (95th pct): mil=%.3f eco=%.3f" % (norms["mil"], norms["eco"]))

    all_phi = np.concatenate([e.phi for e in eps])
    print("Phi range: min=%.3f max=%.3f mean=%.3f std=%.3f"
          % (all_phi.min(), all_phi.max(), all_phi.mean(), all_phi.std()))
    qs = np.percentile(all_phi, [1, 5, 25, 50, 75, 95, 99])
    print("Phi pctiles [1,5,25,50,75,95,99]: "
          + " ".join("%.3f" % q for q in qs))

    corr, n_used, phis, Rs = midgame_phi_outcome_corr(
        eps, args.mid_tick, args.window)
    print("\n=== SANITY GATE: Phi(mid-game) <-> R_terminal ===")
    print("  mid_tick=%d window=%d  episodes used=%d" % (args.mid_tick, args.window, n_used))
    print("  correlation = %.4f" % corr)
    win = phis[Rs > 0]
    loss = phis[Rs < 0]
    if win.size and loss.size:
        print("  Phi|win  : mean=%.3f std=%.3f n=%d" % (win.mean(), win.std(), win.size))
        print("  Phi|loss : mean=%.3f std=%.3f n=%d" % (loss.mean(), loss.std(), loss.size))
    if not np.isnan(corr) and abs(corr) <= 0.1:
        print("  !! WARNING: |corr| <= 0.1 — variant-A FOW-only Phi is too weak.")
        print("     Switch to privileged ground-truth enemy strength (variant B)")
        print("     before investing more in shaping.")
    else:
        print("  Phi is predictive of outcome (|corr| > 0.1): shaping is informative.")


if __name__ == "__main__":
    main()
