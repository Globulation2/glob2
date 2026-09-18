#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PPO learner for Neurotica self-play.

The action is the set of k sampled placements plus a production-mix preset
held for a number of steps (see neurotica_net.act_placements). Each step's
stored context -- the allowed-cell mask and whether the mix was re-chosen --
is loaded back and used to re-score, so the distribution scored is exactly
the one that acted.

Rewards are sparse win/loss plus POTENTIAL-BASED shaping. Potential shaping
(gamma*Phi(s') - Phi(s)) is provably policy-invariant, so the shaping cannot
introduce a strategy that wins the shaping rather than the game.
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
    mixes: np.ndarray      # (T,) chosen production-mix preset, part of the action
    ticks: np.ndarray      # (T,)
    static: np.ndarray     # (n_static, H, W) uint8, constant for the episode
    outcome: float         # +1 win, -1 loss, 0 undecided
    opponent: str = "?"    # for the per-episode log; not used in the update
    mix_decided: np.ndarray = None  # (T,) bool: mix re-chosen this step (term in logp)
    allowed: np.ndarray = None      # (T, ceil(HW/8)) packbits of the placement mask
    temperature: np.ndarray = None  # (T,) sampling temperature used to act


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
                mixes=(arrays["mixes"] if "mixes" in arrays.files else None),
                mix_decided=(arrays["mix_decided"] if "mix_decided" in arrays.files else None),
                allowed=(arrays["allowed"] if "allowed" in arrays.files
                         and arrays["allowed"].size else None),
                temperature=(arrays["temperature"] if "temperature" in arrays.files
                             and arrays["temperature"].size else None),
                values=arrays["values"], potentials=arrays["potentials"],
                ticks=arrays["ticks"], static=arrays["static"],
                outcome=float(meta["outcome"]),
                opponent=str(meta.get("opponent", "?"))))
        except Exception:
            continue  # a partially written episode; skip rather than crash
    return out


def ppo_update(net, opt, scaler, trajs: List[Trajectory], args, device) -> dict:
    if not trajs:
        return {}
    # An episode recorded without its action context (mask, mix, decide flag)
    # cannot be scored against the distribution that acted. Drop it. The old
    # guard dropped the MIX TERM for the whole batch instead, which scored
    # episodes whose stored logp included the term against a distribution
    # without it -- ratios inflated ~5x for exactly those samples.
    complete = [t for t in trajs
                if t.mixes is not None and t.mix_decided is not None
                and t.allowed is not None and len(t.mixes) == len(t.logps)]
    if len(complete) < len(trajs):
        print(f"dropping {len(trajs) - len(complete)} episodes without action context",
              flush=True)
    trajs = complete
    if not trajs:
        return {}
    all_adv, all_ret, all_act, all_logp, obs_refs = [], [], [], [], []
    all_mix, all_dec, all_allow, all_temp = [], [], [], []
    for traj in trajs:
        rewards = compute_rewards(traj, args.gamma, args.shaping)
        adv, ret = gae(rewards, traj.values, args.gamma, args.lam)
        all_adv.append(adv)
        all_ret.append(ret)
        all_act.append(traj.placements)
        all_logp.append(traj.logps)
        all_mix.append(traj.mixes)
        all_dec.append(traj.mix_decided)
        all_allow.append(traj.allowed)
        all_temp.append(traj.temperature if traj.temperature is not None
                        else np.ones(len(traj.logps), dtype=np.float32))
        obs = np.load(traj.obs_path, mmap_mode="r")
        obs_refs.extend([(obs, i, traj) for i in range(len(traj.logps))])

    adv = np.concatenate(all_adv)
    ret = np.concatenate(all_ret)
    acts = np.concatenate(all_act)
    logp_old = np.concatenate(all_logp)
    # Only score the mix when every episode in the batch carries one, so a
    # mixed batch of old and new trajectories cannot silently score a joint
    # action against a placements-only log-prob.
    mix_all = np.concatenate(all_mix)
    dec_all = np.concatenate(all_dec)
    allow_all = np.concatenate(all_allow)          # (N, ceil(HW/8)) uint8
    temp_all = np.concatenate(all_temp).astype(np.float32)
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
                mb = torch.from_numpy(mix_all[idx]).to(device)
                db = torch.from_numpy(dec_all[idx]).to(device)
                hw = x.shape[2] * x.shape[3]
                allow_b = torch.from_numpy(
                    np.unpackbits(allow_all[idx], axis=1)[:, :hw].astype(bool)).to(device)
                tb = torch.from_numpy(temp_all[idx]).to(device)
                ev = net.evaluate_placements(x, ab, existing, allowed=allow_b,
                                             mix=mb, decide_mix=db, temperature=tb)
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
    ap.add_argument("--snapshot-every", type=int, default=25,
                    help="keep a copy of policy.pt every N iterations")
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

    updates = 0

    for it in range(args.iterations):
        trajs = load_trajectories(args.rollouts)
        if len(trajs) < 4:
            time.sleep(10)
            continue
        stats = ppo_update(net, opt, scaler, trajs, args, device)
        print(f"iter {it}: {stats}", flush=True)
        state = {"model": net.state_dict(), "in_planes": ckpt.get("in_planes", 60),
                 "args": ckpt.get("args", {}), "metrics": stats, "iter": it}
        torch.save(state, f"{args.out}/policy.pt")
        # policy.pt is overwritten every iteration; without snapshots a run
        # that degrades (measured: 22.6% -> 6.9% over 2200 episodes) leaves
        # nothing to roll back to.
        # Count UPDATES, not iterations: iterations with no episodes `continue`
        # before reaching here, so `it % N == 0` only snapshots when a
        # multiple of N happens to land on a non-empty iteration -- the loop
        # ran to iter 160 with nothing saved past 75.
        updates += 1
        if args.snapshot_every > 0 and updates % args.snapshot_every == 0:
            os.makedirs(f"{args.out}/snapshots", exist_ok=True)
            torch.save(state, f"{args.out}/snapshots/policy_{it:06d}.pt")
        # Episodes are deleted once consumed, so anything worth analysing
        # later has to be recorded here. One line per episode: which mixes it
        # played and how it ended, which is the only way to answer "does the
        # military preset actually win" over a long run.
        with open(f"{args.out}/episodes.csv", "a") as fh:
            for traj in trajs:
                mx = traj.mixes
                share = ([float(np.mean(mx == k)) for k in range(5)]
                         if mx is not None and len(mx) else [float("nan")] * 5)
                fh.write(f"{it},{traj.opponent},{traj.outcome},{len(traj.logps)},"
                         + ",".join(f"{v:.3f}" for v in share) + "\n")
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
