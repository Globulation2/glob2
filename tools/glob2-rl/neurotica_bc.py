#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Behavior cloning of exactly the semantic orders executed by NPS6."""

import argparse
import json
import os
from pathlib import Path
import numpy as np
import torch
from neurotica_actions import pack_action
from neurotica_data import OrderCorpus
from neurotica_net import NeuroticaNet


def imitation_loss(ev, c):
    # Holds must not get a larger opcode weight just because they have fewer
    # parameters. Keep opcode/delay weights fixed, with a separately normalized
    # parameter loss. Area size cannot drown the discrete controls.
    terms = ev["terms"]
    params = [
        -v / (c.w * c.h if k == "area" else 1)
        for k, v in terms.items()
        if k not in ("op", "delay")
    ]
    return (
        -terms["op"] - terms["delay"] + (torch.stack(params).mean() if params else 0)
    ) / 3


@torch.no_grad()
def evaluate(net, ds, indices):
    net.eval()
    loss = []
    correct = []
    by_op = {}
    for i in indices:
        c, a, p = ds.sample(i)
        ev = net.score_action(c, a, p)
        loss.append(float(imitation_loss(ev, c)))
        pred = net.score_action(c, previous=p, deterministic=True)["action"]
        ok = pack_action(pred) == pack_action(a)
        correct.append(ok)
        by_op.setdefault(str(a["op"]), []).append(ok)
    return dict(
        loss=float(np.mean(loss)),
        exact_order=float(np.mean(correct)),
        by_op={k: dict(n=len(v), exact=float(np.mean(v))) for k, v in by_op.items()},
    )


def atomic_save(ck, path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    torch.save(ck, tmp)
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument(
        "--init",
        help="start BC from a compatible order checkpoint; optimizer starts fresh",
    )
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--width", type=int, default=32)
    ap.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--validation-fraction", type=float, default=0.2)
    ap.add_argument(
        "--overfit",
        type=int,
        default=0,
        help="diagnostic: train/evaluate the same N records; NOT a validation result",
    )
    args = ap.parse_args()
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    ds = OrderCorpus(args.corpus)
    if args.overfit:
        train = val = list(range(min(args.overfit, len(ds.records))))
        groups = {"diagnostic_only": True}
    else:
        train, val, groups = ds.split(args.validation_fraction, args.seed)
    out = Path(args.out)
    if out.exists() and any(out.iterdir()):
        raise ValueError("use a fresh output directory; restart explicitly with --init")
    out.mkdir(parents=True, exist_ok=True)
    (out / "split.json").write_text(json.dumps(groups, indent=2))
    c, _, _ = ds.sample(train[0])
    net = (
        NeuroticaNet.load(args.init, args.device)[0]
        if args.init
        else NeuroticaNet(c.planes().shape[0], args.width).to(args.device)
    )
    atomic_save(net.checkpoint(), out / "initial.pt")
    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=1e-4)
    best = float("inf")
    for epoch in range(args.epochs):
        net.train()
        rng.shuffle(train)
        total = 0
        for start in range(0, len(train), args.batch):
            batch = train[start : start + args.batch]
            opt.zero_grad(set_to_none=True)
            for i in batch:
                c, a, p = ds.sample(i)
                loss = imitation_loss(net.score_action(c, a, p), c)
                (loss / len(batch)).backward()
                total += float(loss.detach())
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            opt.step()
        metrics = evaluate(net, ds, val)
        ck = net.checkpoint(
            training=dict(kind="bc", args=vars(args), epoch=epoch, split=groups),
            metrics=metrics,
        )
        atomic_save(ck, out / "last.pt")
        if metrics["loss"] < best:
            best = metrics["loss"]
            atomic_save(ck, out / "best.pt")
        print(
            json.dumps(dict(epoch=epoch, train_loss=total / len(train), **metrics)),
            flush=True,
        )


if __name__ == "__main__":
    main()
