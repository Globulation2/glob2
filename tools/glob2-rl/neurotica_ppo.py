#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PPO over the latent, for Neurotica self-play.

The action is the 32-dimensional exploration latent, not the field. The field
is a deterministic decode of (observation, latent), so a policy step is a draw
from a 32-dim diagonal Gaussian rather than ~16k per-cell decisions. That is the
whole reason PPO is tractable here: sampling each cell independently would make
log pi(a|s) a sum over 16,384 terms, and the importance ratio
exp(sum of log-prob deltas) would explode on essentially every update. It would
also explore in a useless direction — flipping one tile is noise, while moving
the latent changes what kind of game the net wants to play.

Rewards are sparse win/loss plus POTENTIAL-BASED shaping. Potential shaping
(gamma*Phi(s') - Phi(s)) is provably policy-invariant, so the shaping cannot
introduce a strategy that wins the shaping rather than the game — which is the
usual way shaped RTS agents go wrong. Phi is read off the observation planes
the server already has, so it costs nothing extra.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import time
from dataclasses import dataclass
from typing import List

import numpy as np
import torch
import torch.nn.functional as F

from neurotica_net import NeuroticaNet


@dataclass
class Trajectory:
    obs_path: str          # .npy memmap of DYNAMIC planes for this episode
    placements: np.ndarray # (T, k) flat cell indices that were acted on
    logps: np.ndarray      # (T,)
    values: np.ndarray     # (T,)
    potentials: np.ndarray # (T,)
    ticks: np.ndarray      # (T,)
    static: np.ndarray     # (n_static, H, W) uint8, constant for the episode
    outcome: float         # +1 win, -1 loss, 0 undecided


def build_observation(static: np.ndarray, dynamic: np.ndarray,
                      tick: int) -> np.ndarray:
    """Reassemble the exact network input from a stored transition.

    Must match neurotica_data.py and neurotica_serve.py exactly: static planes,
    then dynamic, then the two tick planes. A mismatch here trains the policy on
    a different input than it acted on, which is the kind of bug that shows up
    as "PPO mysteriously does not learn".
    """
    h, w = dynamic.shape[-2:]
    t = np.float32(min(int(tick), 60000) / 60000.0)
    tick_planes = np.stack([
        np.full((h, w), t, dtype=np.float32),
        np.full((h, w), np.float32(np.sqrt(t)), dtype=np.float32)])
    return np.concatenate([static.astype(np.float32) / 255.0,
                           dynamic.astype(np.float32) / 255.0,
                           tick_planes], axis=0)


def compute_rewards(traj: Trajectory, gamma: float, shaping: float) -> np.ndarray:
    """Terminal win/loss plus potential-based shaping."""
    T = len(traj.logps)
    rewards = np.zeros(T, dtype=np.float32)
    if shaping > 0 and T > 1:
        phi = traj.potentials.astype(np.float32)
        rewards[:-1] += shaping * (gamma * phi[1:] - phi[:-1])
    rewards[-1] += traj.outcome
    return rewards


def gae(rewards: np.ndarray, values: np.ndarray, gamma: float, lam: float):
    T = len(rewards)
    adv = np.zeros(T, dtype=np.float32)
    last = 0.0
    for t in reversed(range(T)):
        next_v = values[t + 1] if t + 1 < T else 0.0
        delta = rewards[t] + gamma * next_v - values[t]
        last = delta + gamma * lam * last
        adv[t] = last
    return adv, adv + values


def load_trajectories(run_dir: str) -> List[Trajectory]:
    out = []
    for meta_path in sorted(glob.glob(os.path.join(run_dir, "*.json"))):
        try:
            with open(meta_path) as fh:
                meta = json.load(fh)
            if meta.get("outcome") is None:
                continue  # game still running, or no result recorded
            base = meta_path[:-5]
            arrays = np.load(base + ".npz")
            out.append(Trajectory(
                obs_path=base + ".obs.npy",
                placements=arrays["placements"], logps=arrays["logps"],
                values=arrays["values"], potentials=arrays["potentials"],
                ticks=arrays["ticks"], static=arrays["static"],
                outcome=float(meta["outcome"])))
        except Exception:
            continue  # a partially written episode; skip rather than crash
    return out


def ppo_update(net, opt, scaler, trajs: List[Trajectory], args, device) -> dict:
    if not trajs:
        return {}
    all_adv, all_ret, all_act, all_logp, obs_refs = [], [], [], [], []
    for traj in trajs:
        rewards = compute_rewards(traj, args.gamma, args.shaping)
        adv, ret = gae(rewards, traj.values, args.gamma, args.lam)
        all_adv.append(adv)
        all_ret.append(ret)
        all_act.append(traj.placements)
        all_logp.append(traj.logps)
        obs = np.load(traj.obs_path, mmap_mode="r")
        obs_refs.extend([(obs, i, traj) for i in range(len(traj.logps))])

    adv = np.concatenate(all_adv)
    ret = np.concatenate(all_ret)
    acts = np.concatenate(all_act)
    logp_old = np.concatenate(all_logp)
    adv = (adv - adv.mean()) / (adv.std() + 1e-8)

    n = len(adv)
    stats = dict(n=n, mean_return=float(ret.mean()),
                 win_rate=float(np.mean([t.outcome > 0 for t in trajs])))
    idx_all = np.arange(n)
    for _ in range(args.ppo_epochs):
        np.random.shuffle(idx_all)
        for start in range(0, n, args.minibatch):
            idx = idx_all[start:start + args.minibatch]
            obs_batch = np.stack([
                build_observation(obs_refs[i][2].static, obs_refs[i][0][obs_refs[i][1]],
                                  obs_refs[i][2].ticks[obs_refs[i][1]])
                for i in idx])
            x = torch.from_numpy(obs_batch).to(device).to(memory_format=torch.channels_last)
            ab = torch.from_numpy(acts[idx]).to(device)
            # Rebuild the same "already occupied" mask the server used, or the
            # re-scored distribution would not be the one that acted.
            existing = x[:, 4 + 8:4 + 8 + 13].amax(dim=1) > 0.5
            with torch.autocast("cuda", dtype=torch.float16):
                # Budget planes are the team's own building planes. The mask is
                # recomputed from the observation, so it is identical to the one
                # the server applied when the action was sampled -- otherwise
                # the re-scored distribution is not the one that acted.
                ev = net.evaluate_placements(
                    x, ab, existing, budget_planes=x[:, 4 + 8:4 + 8 + 13])
            ratio = (ev["logp"] - torch.from_numpy(logp_old[idx]).to(device)).exp()
            a = torch.from_numpy(adv[idx]).to(device)
            pg = -torch.min(ratio * a,
                            ratio.clamp(1 - args.clip, 1 + args.clip) * a).mean()
            vloss = F.mse_loss(ev["value"], torch.from_numpy(ret[idx]).to(device))
            loss = pg + args.vf_coef * vloss - args.ent_coef * ev["entropy"].mean()
            opt.zero_grad(set_to_none=True)
            scaler.scale(loss).backward()
            scaler.unscale_(opt)
            torch.nn.utils.clip_grad_norm_(net.parameters(), 0.5)
            scaler.step(opt)
            scaler.update()
            stats["pg"] = float(pg.item())
            stats["vloss"] = float(vloss.item())
    return stats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--init", required=True, help="BC checkpoint to start from")
    ap.add_argument("--rollouts", required=True, help="directory of episodes")
    ap.add_argument("--out", default=os.path.expanduser("~/neurotica/ppo"))
    ap.add_argument("--gamma", type=float, default=0.999)
    ap.add_argument("--lam", type=float, default=0.95)
    ap.add_argument("--clip", type=float, default=0.2)
    ap.add_argument("--vf-coef", type=float, default=0.5)
    ap.add_argument("--ent-coef", type=float, default=0.003)
    ap.add_argument("--shaping", type=float, default=0.1)
    ap.add_argument("--ppo-epochs", type=int, default=2)
    ap.add_argument("--minibatch", type=int, default=16)
    ap.add_argument("--lr", type=float, default=1e-5)
    ap.add_argument("--iterations", type=int, default=1000)
    args = ap.parse_args()

    device = "cuda"
    ckpt = torch.load(args.init, map_location="cpu", weights_only=False)
    net = NeuroticaNet(ckpt.get("in_planes", 60),
                       width=ckpt.get("args", {}).get("width", 48)).to(device)
    net.load_state_dict(ckpt["model"], strict=False)
    net.to(memory_format=torch.channels_last)
    # A low learning rate on purpose: the BC prior is the only thing keeping
    # early self-play from wandering into strategies that have never built an
    # inn, and a large step destroys it before the value head is worth
    # anything.
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=0.0)
    scaler = torch.amp.GradScaler("cuda")
    os.makedirs(args.out, exist_ok=True)

    for it in range(args.iterations):
        trajs = load_trajectories(args.rollouts)
        if len(trajs) < 4:
            time.sleep(10)
            continue
        stats = ppo_update(net, opt, scaler, trajs, args, device)
        print(f"iter {it}: {stats}", flush=True)
        torch.save({"model": net.state_dict(), "in_planes": ckpt.get("in_planes", 60),
                    "args": ckpt.get("args", {}), "metrics": stats},
                   f"{args.out}/policy.pt")
        for traj in trajs:
            base = traj.obs_path[:-8]
            for suffix in (".json", ".npz", ".obs.npy"):
                try:
                    os.unlink(base + suffix)
                except OSError:
                    pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
