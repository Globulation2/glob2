#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One synchronized PPO generation over complete, versioned semantic orders.
The task is a finite game: win=1, loss/cap=0. A cap is a terminal outcome, NOT
an infrastructure timeout or a continuing trajectory with a missing bootstrap.
"""

import argparse
import copy
import json
from pathlib import Path
import zlib
import numpy as np
import torch
from neurotica_actions import SCHEMA, parse_context, unpack_action
from neurotica_net import NeuroticaNet
from neurotica_bc import atomic_save, imitation_loss
from neurotica_data import OrderCorpus


def compute_rewards(
    potentials, outcome, discounts, shaping=1.0, terminal=True, next_potential=None
):
    phi = np.asarray(potentials, dtype=np.float64)
    if not len(phi):
        raise ValueError("empty trajectory")
    if not terminal and next_potential is None:
        raise ValueError("truncation requires next potential")
    endpoint = 0.0 if terminal else next_potential
    reward = shaping * (np.asarray(discounts) * np.r_[phi[1:], endpoint] - phi)
    if terminal:
        reward[-1] += outcome
    return reward.astype(np.float32)


def gae(rewards, values, discounts, trace_discounts, terminal=True, next_value=None):
    if not terminal and next_value is None:
        raise ValueError("truncation requires next-state value")
    adv = np.zeros(len(rewards), np.float32)
    carry = 0.0
    bootstrap = 0.0 if terminal else float(next_value)
    for t in reversed(range(len(rewards))):
        nv = values[t + 1] if t + 1 < len(values) else bootstrap
        delta = rewards[t] + discounts[t] * nv - values[t]
        carry = delta + discounts[t] * trace_discounts[t] * carry
        adv[t] = carry
    return adv, adv + values


class Episode:
    def __init__(self, path, policy_id):
        self.path = Path(path)
        self.meta = json.loads(self.path.read_text())
        m = self.meta
        if m.get("schema") != SCHEMA or m.get("policy_id") != policy_id:
            raise ValueError(f"stale/incompatible rollout: {path}")
        if (
            m.get("status") != "complete"
            or m.get("outcome") not in (0, 1)
            or m.get("end_reason") not in ("win", "loss", "cap", "draw")
            or not m.get("sample")
        ):
            raise ValueError(f"incomplete/invalid/greedy rollout: {path}")
        with np.load(self.path.with_suffix(".npz")) as a:
            self.a = {k: a[k] for k in a.files}
        n = len(self.a["logps"])
        if (
            n != m["steps"]
            or not n
            or not all(
                np.isfinite(self.a[k]).all() for k in ("logps", "values", "potentials")
            )
        ):
            raise ValueError("corrupt rollout")
        if np.any(np.diff(self.a["ticks"]) <= 0) or m["end_tick"] < self.a["ticks"][-1]:
            raise ValueError("invalid rollout timing")
        self.actions = []
        for i in range(n):
            off = self.a["action_offsets"]
            self.actions.append(
                unpack_action(self.a["actions"][off[i] : off[i + 1]].tobytes())
            )
        self.durations = (
            np.diff(np.r_[self.a["ticks"], m["end_tick"]]).clip(min=1) / 25.0
        )

    def context(self, i):
        off = self.a["offsets"]
        return parse_context(
            zlib.decompress(self.a["contexts"][off[i] : off[i + 1]].tobytes())
        )

    def sample(self, i):
        return self.context(i), self.actions[i], self.context(i - 1) if i else None


def update(net, episodes, args, reference=None, bc=None):
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=0.0)
    samples = []
    advantages = []
    returns = []
    old = []
    for e in episodes:
        discounts = args.gamma**e.durations
        rewards = compute_rewards(
            e.a["potentials"], e.meta["outcome"], discounts, args.shaping
        )
        adv, ret = gae(rewards, e.a["values"], discounts, args.lam**e.durations)
        advantages.extend(adv)
        returns.extend(ret)
        old.extend(e.a["logps"])
        samples.extend((e, i) for i in range(len(adv)))

    def sample_at(i):
        e, j = samples[i]
        return e.sample(j)

    advantages = np.array(advantages)
    advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)
    # Prove actor/learner agreement before the FIRST optimization step.
    with torch.no_grad():
        for i in range(len(samples)):
            c, a, p = sample_at(i)
            got = float(net.score_action(c, a, p)["logp"])
            if not np.isclose(got, old[i], atol=2e-3, rtol=1e-5):
                raise ValueError(
                    f"behavior likelihood mismatch at {i}: {got} != {old[i]}"
                )
    rng = np.random.default_rng(args.seed)
    stats = []
    stopped = False
    backtracks = 0
    updates = 0
    for epoch in range(args.epochs):
        indices = rng.permutation(len(samples))
        for start in range(0, len(indices), args.batch):
            batch = indices[start : start + args.batch]
            opt.zero_grad(set_to_none=True)
            batch_kl = []
            for i in batch:
                c, a, p = sample_at(i)
                ev = net.score_action(c, a, p)
                delta = ev["logp"] - float(old[i])
                # Clamp only the numerical exponent, never silently accept
                # large drift: the KL gate below stops before the update.
                ratio = delta.clamp(-20, 20).exp()
                adv = float(advantages[i])
                pg = -torch.minimum(
                    ratio * adv, ratio.clamp(1 - args.clip, 1 + args.clip) * adv
                )
                vl = (ev["value"] - float(returns[i])).square()
                ent = torch.stack(
                    [
                        v / (c.w * c.h if k == "area" else 1)
                        for k, v in ev["entropies"].items()
                    ]
                ).mean()
                loss = pg + args.vf_coef * vl - args.ent_coef * ent
                # A BC reference on learner-visited states, using common random
                # numbers to estimate forward KL without storing dense logits.
                if reference is not None and args.kl_coef:
                    with torch.no_grad():
                        ref = reference.score_action(c, previous=p)
                    new = net.score_action(c, ref["action"], p)
                    loss += args.kl_coef * (ref["logp"].detach() - new["logp"])
                (loss / len(batch)).backward()
                kl = float(((ratio - 1) - delta).detach())
                batch_kl.append(kl)
                stats.append(
                    [
                        float(pg.detach()),
                        float(vl.detach()),
                        float(ent.detach()),
                        kl,
                        float(abs(float(ratio.detach()) - 1) > args.clip),
                    ]
                )
            if np.mean(batch_kl) > args.target_kl:
                stopped = True
                opt.zero_grad(set_to_none=True)
                break
            if bc is not None and args.bc_coef:
                c, a, p = bc.sample(int(rng.integers(len(bc.records))))
                (args.bc_coef * imitation_loss(net.score_action(c, a, p), c)).backward()
            torch.nn.utils.clip_grad_norm_(
                net.parameters(), 0.5, error_if_nonfinite=True
            )
            # A pre-update KL check cannot prevent THIS step overshooting,
            # especially for a joint distribution containing an area mask.
            # Backtrack the actual Adam step before accepting new weights.
            before = copy.deepcopy(net.state_dict())
            before_opt = copy.deepcopy(opt.state_dict())
            base_lr = opt.param_groups[0]["lr"]
            accepted = False
            for retry in range(12):
                if retry:
                    net.load_state_dict(before)
                    opt.load_state_dict(before_opt)
                    opt.param_groups[0]["lr"] = base_lr * (0.5**retry)
                    backtracks += 1
                opt.step()
                with torch.no_grad():
                    ds = [
                        float(net.score_action(*sample_at(i))["logp"]) - float(old[i])
                        for i in batch
                    ]
                post_kl = float(
                    np.mean(np.expm1(np.clip(ds, -20, 20)) - np.asarray(ds))
                )
                if np.isfinite(post_kl) and post_kl <= args.target_kl:
                    accepted = True
                    updates += 1
                    break
            if not accepted:
                net.load_state_dict(before)
                opt.load_state_dict(before_opt)
                stopped = True
                break
        if stopped:
            break
    with torch.no_grad():
        final_delta = np.array(
            [
                float(net.score_action(*sample_at(i))["logp"]) - float(lp)
                for i, lp in enumerate(old)
            ]
        )
    final_kl = float(np.mean(np.expm1(np.clip(final_delta, -20, 20)) - final_delta))
    if not np.isfinite(final_kl) or final_kl > args.target_kl * 2:
        raise ValueError(
            f"global KL {final_kl} exceeds trust region; update not saved; reduce learning rate"
        )
    mean = np.mean(stats, axis=0)
    return dict(
        zip(
            ("policy_loss", "value_loss", "entropy", "approx_kl", "clip_fraction"),
            map(float, mean),
        ),
        samples=len(samples),
        episodes=len(episodes),
        kl_stopped=stopped,
        updates=updates,
        backtracks=backtracks,
        final_kl=final_kl,
        learning_rate=opt.param_groups[0]["lr"],
        win_rate=float(np.mean([e.meta["outcome"] for e in episodes])),
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--init", required=True)
    ap.add_argument("--rollouts", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--reference", required=True, help="immutable BC prior")
    ap.add_argument("--bc-corpus")
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    for name, default in [
        ("gamma", 1.0),
        ("lam", 0.99),
        ("shaping", 1.0),
        ("lr", 1e-5),
        ("clip", 0.2),
        ("vf-coef", 0.5),
        ("ent-coef", 0.001),
        ("target-kl", 0.02),
        ("kl-coef", 0.01),
        ("bc-coef", 0.1),
    ]:
        ap.add_argument("--" + name, type=float, default=default)
    ap.add_argument("--epochs", type=int, default=2)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    torch.manual_seed(args.seed)
    if not (0 < args.gamma <= 1 and 0 < args.lam <= 1):
        raise ValueError("discounts must be in (0,1]")
    net, ck = NeuroticaNet.load(args.init, args.device)
    ref, ref_ck = NeuroticaNet.load(args.reference, args.device)
    ref.eval()
    for p in ref.parameters():
        p.requires_grad = False
    episodes = [
        Episode(p, ck["policy_id"]) for p in sorted(Path(args.rollouts).glob("*.json"))
    ]
    if not episodes:
        raise ValueError("no completed rollouts")
    stats = update(
        net,
        episodes,
        args,
        ref,
        OrderCorpus(args.bc_corpus) if args.bc_corpus else None,
    )
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    atomic_save(
        net.checkpoint(
            training=dict(
                kind="ppo",
                args=vars(args),
                parent=ck["policy_id"],
                reference=ref_ck["policy_id"],
            ),
            metrics=stats,
        ),
        out / "policy.pt",
    )
    (out / "metrics.json").write_text(json.dumps(stats, indent=2))
    print(json.dumps(stats), flush=True)
    # Retain all source episodes and their exact configuration for audit.


if __name__ == "__main__":
    main()
