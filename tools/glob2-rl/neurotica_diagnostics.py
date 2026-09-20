#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit reward density, calibration on learner states and executed controls.
A historical calibrated predictor is only a heuristic potential on new policy
states. Reports are per episode/phase so long games do not silently dominate.
"""

import argparse
import json
from pathlib import Path
import numpy as np
from neurotica_ppo import Episode, compute_rewards
from neurotica_actions import OPS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rollouts")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    rows = []
    bins = {}
    for p in sorted(Path(args.rollouts).glob("*.json")):
        m = json.loads(p.read_text())
        e = Episode(p, m["policy_id"])
        phi = e.a["potentials"]
        prob = phi + 0.5
        ticks = e.a["ticks"]
        rewards = compute_rewards(phi, m["outcome"], np.ones(len(phi)))
        ops = np.array([a["op"] for a in e.actions])
        counts = {name: int((ops == i).sum()) for i, name in enumerate(OPS)}
        rows.append(
            dict(
                game_id=m["game_id"],
                opponent=m["opponent"],
                outcome=m["outcome"],
                steps=len(phi),
                potential_changes=int(np.count_nonzero(np.diff(phi))),
                shaping_return=float(rewards.sum() - m["outcome"]),
                expected_shaping_return=-float(phi[0]),
                actions=counts,
            )
        )
        for phase, mask in [("opening", ticks < 5120), ("later", ticks >= 5120)]:
            for i in range(10):
                selected = mask & (np.minimum((prob * 10).astype(int), 9) == i)
                if selected.any():
                    bins.setdefault((phase, i), []).append(
                        (float(prob[selected].mean()), m["outcome"])
                    )
    calibration = []
    for (phase, i), values in sorted(bins.items()):
        a = np.array(values)
        calibration.append(
            dict(
                phase=phase,
                bin=i,
                episodes=len(values),
                predicted=float(a[:, 0].mean()),
                wins=float(a[:, 1].mean()),
                brier=float(((a[:, 0] - a[:, 1]) ** 2).mean()),
            )
        )
    result = dict(
        episodes=rows,
        calibration=calibration,
        note="Calibration here targets wins, with caps counted as nonwins. Each game contributes once per occupied bin.",
    )
    Path(args.out).write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
