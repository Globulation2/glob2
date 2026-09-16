#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Behaviour cloning for Neurotica.

Trains the policy to predict a team's desired state — what its map looked like
delta ticks later — from its fog-limited view now. Pooled across teachers and
not conditioned on which one, by design: the point is a diffuse prior that has
not committed to one AI's dogma, which is a better thing to hand PPO than a
sharp single-teacher clone.

The class imbalance is the thing that decides whether this works at all. About
25 of 16,384 cells hold a building, so unweighted cross-entropy converges
immediately to "nothing anywhere" and reports 99.8% accuracy while having
learned nothing. Hence the weight on the empty class, and hence the metrics
below being per-class recall rather than accuracy.
"""

from __future__ import annotations

import argparse
import os
import time

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, random_split

from neurotica_data import NeuroticaBC, collate
from neurotica_net import NeuroticaNet, NUM_BUILDING_CLASSES, count_params


def build_loss_weights(empty_weight: float, device) -> torch.Tensor:
    w = torch.ones(NUM_BUILDING_CLASSES, device=device)
    w[0] = empty_weight
    return w


# Index of the first "my building" plane inside the observation stack: the
# 4 static planes come first, then the 8 resource planes, then 13 own-building
# planes. Used to separate copying from predicting.
MY_BUILDING_FIRST = 4 + 8
MY_BUILDING_COUNT = 13


@torch.no_grad()
def evaluate(net, loader, device, weights, limit_batches=40):
    net.eval()
    tot_loss = tot_n = 0
    # Counts for the only metrics that mean anything here.
    tp = torch.zeros(NUM_BUILDING_CLASSES, device=device)
    fp = torch.zeros(NUM_BUILDING_CLASSES, device=device)
    fn = torch.zeros(NUM_BUILDING_CLASSES, device=device)
    area_correct = area_total = 0
    # The metric that actually matters. The observation already shows the team's
    # current buildings and the label is mostly those same buildings, so a net
    # that simply copies its input scores ~0.9 overall while having predicted
    # nothing. Novel cells are the ones the label has and the observation does
    # not — the only place a prediction is being made at all.
    novel_tp = novel_fn = novel_fp = 0
    copy_tp = copy_fn = 0
    for i, (obs, building, areas) in enumerate(loader):
        if i >= limit_batches:
            break
        obs = obs.to(device, non_blocking=True).to(memory_format=torch.channels_last)
        building = building.to(device, non_blocking=True)
        areas = areas.to(device, non_blocking=True)
        with torch.autocast("cuda", dtype=torch.float16):
            out = net(obs)
            loss = F.cross_entropy(out["building"].float(), building, weight=weights)
        pred = out["building"].float().argmax(1)
        for c in range(NUM_BUILDING_CLASSES):
            p, t = (pred == c), (building == c)
            tp[c] += (p & t).sum()
            fp[c] += (p & ~t).sum()
            fn[c] += (~p & t).sum()
        # Which cells already held one of this team's buildings at observation
        # time (the planes are 0/1 scaled to 0..1).
        existing = obs[:, MY_BUILDING_FIRST:MY_BUILDING_FIRST + MY_BUILDING_COUNT]
        existing = existing.amax(dim=1) > 0.5
        label_occ = building > 0
        pred_occ = pred > 0
        novel = label_occ & ~existing
        kept = label_occ & existing
        novel_tp += (pred_occ & novel).sum().item()
        novel_fn += (~pred_occ & novel).sum().item()
        novel_fp += (pred_occ & ~label_occ & ~existing).sum().item()
        copy_tp += (pred_occ & kept).sum().item()
        copy_fn += (~pred_occ & kept).sum().item()

        area_pred = (out["areas"].float() > 0)
        area_correct += (area_pred == (areas > 0.5)).sum().item()
        area_total += areas.numel()
        tot_loss += loss.item() * obs.size(0)
        tot_n += obs.size(0)
    net.train()
    occupied_tp = tp[1:].sum()
    occupied_fn = fn[1:].sum()
    occupied_fp = fp[1:].sum()
    recall = (occupied_tp / (occupied_tp + occupied_fn).clamp(min=1)).item()
    precision = (occupied_tp / (occupied_tp + occupied_fp).clamp(min=1)).item()
    novel_recall = novel_tp / max(novel_tp + novel_fn, 1)
    novel_precision = novel_tp / max(novel_tp + novel_fp, 1)
    return dict(loss=tot_loss / max(tot_n, 1), recall=recall, precision=precision,
                f1=2 * recall * precision / max(recall + precision, 1e-9),
                area_acc=area_correct / max(area_total, 1),
                novel_recall=novel_recall, novel_precision=novel_precision,
                novel_n=novel_tp + novel_fn,
                copy_recall=copy_tp / max(copy_tp + copy_fn, 1))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default=os.path.expanduser("~/neurotica/corpus"))
    ap.add_argument("--delta", type=int, default=500)
    ap.add_argument("--epochs", type=int, default=8)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--width", type=int, default=48)
    ap.add_argument("--empty-weight", type=float, default=0.02,
                    help="loss weight for the 'no building' class")
    ap.add_argument("--novel-weight", type=float, default=20.0,
                    help="extra per-cell loss weight on cells the label occupies "
                         "but the observation does not — the only cells where a "
                         "prediction is actually being made")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--limit-games", type=int, default=None)
    ap.add_argument("--all-teams", action="store_true",
                    help="train on losing teams too (default: winners only)")
    ap.add_argument("--out", default=os.path.expanduser("~/neurotica/ckpt"))
    args = ap.parse_args()

    device = "cuda"
    torch.backends.cudnn.benchmark = True

    print(f"indexing corpus {args.corpus} (delta={args.delta}) ...", flush=True)
    t0 = time.time()
    ds = NeuroticaBC(args.corpus, delta=args.delta,
                     winners_only=not args.all_teams, limit_games=args.limit_games)
    print(f"  {len(ds)} samples, shape {ds.shape}, {time.time()-t0:.0f}s", flush=True)
    if len(ds) < 100:
        print("not enough samples")
        return 1

    n_val = max(200, len(ds) // 20)
    train_ds, val_ds = random_split(ds, [len(ds) - n_val, n_val],
                                    generator=torch.Generator().manual_seed(0))
    train_loader = DataLoader(train_ds, batch_size=args.batch, shuffle=True,
                              num_workers=args.workers, collate_fn=collate,
                              pin_memory=True, drop_last=True, persistent_workers=True)
    val_loader = DataLoader(val_ds, batch_size=args.batch, shuffle=False,
                            num_workers=2, collate_fn=collate, pin_memory=True)

    in_planes = ds.shape[0] + 4 + 2  # dynamic + static + the two tick planes
    net = NeuroticaNet(in_planes, width=args.width).to(device).to(memory_format=torch.channels_last)
    print(f"net: {count_params(net)/1e6:.2f}M params, {in_planes} input planes", flush=True)

    opt = torch.optim.AdamW(net.parameters(), lr=args.lr, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.OneCycleLR(
        opt, max_lr=args.lr, total_steps=args.epochs * len(train_loader))
    # fp16, not bf16: torch.cuda.is_bf16_supported() returns True on sm_75 but
    # cuDNN has no bf16 convolution engine there and fails at runtime.
    scaler = torch.amp.GradScaler("cuda")
    weights = build_loss_weights(args.empty_weight, device)

    os.makedirs(args.out, exist_ok=True)
    best_f1 = 0.0
    step = 0
    for epoch in range(args.epochs):
        t_epoch = time.time()
        run_loss, run_n = 0.0, 0
        for obs, building, areas in train_loader:
            obs = obs.to(device, non_blocking=True).to(memory_format=torch.channels_last)
            building = building.to(device, non_blocking=True)
            areas = areas.to(device, non_blocking=True)
            # Per-cell weighting toward the novel cells. Without it the loss is
            # dominated by cells whose answer is already sitting in the input,
            # and the net learns to copy: measured, that gives COPY recall 0.998
            # and NOVEL recall 0.125.
            with torch.no_grad():
                existing = obs[:, MY_BUILDING_FIRST:MY_BUILDING_FIRST + MY_BUILDING_COUNT]
                existing = existing.amax(dim=1) > 0.5
                novel = (building > 0) & ~existing
                cell_w = 1.0 + args.novel_weight * novel.float()
            with torch.autocast("cuda", dtype=torch.float16):
                out = net(obs)
                per_cell = F.cross_entropy(out["building"].float(), building,
                                           weight=weights, reduction="none")
                loss_b = (per_cell * cell_w).sum() / cell_w.sum()
                loss_a = F.binary_cross_entropy_with_logits(out["areas"].float(), areas)
                loss = loss_b + 0.3 * loss_a
            opt.zero_grad(set_to_none=True)
            scaler.scale(loss).backward()
            scaler.unscale_(opt)
            torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0)
            scaler.step(opt)
            scaler.update()
            sched.step()
            run_loss += loss.item() * obs.size(0)
            run_n += obs.size(0)
            step += 1
            if step % 100 == 0:
                print(f"  e{epoch} step {step} loss {run_loss/max(run_n,1):.4f} "
                      f"({run_n/(time.time()-t_epoch):.0f} samp/s)", flush=True)
        m = evaluate(net, val_loader, device, weights)
        print(f"epoch {epoch}: train {run_loss/max(run_n,1):.4f} | val {m['loss']:.4f} "
              f"| all f1 {m['f1']:.3f} | COPY recall {m['copy_recall']:.3f} "
              f"| NOVEL recall {m['novel_recall']:.3f} prec {m['novel_precision']:.3f} "
              f"(n={m['novel_n']}) | area {m['area_acc']:.4f} | {time.time()-t_epoch:.0f}s",
              flush=True)
        torch.save({"model": net.state_dict(), "args": vars(args),
                    "in_planes": in_planes, "metrics": m}, f"{args.out}/last.pt")
        # Select on novel recall, not overall f1: overall f1 rewards copying.
        selector = m["novel_recall"]
        if selector > best_f1:
            best_f1 = selector
            torch.save({"model": net.state_dict(), "args": vars(args),
                        "in_planes": in_planes, "metrics": m}, f"{args.out}/best.pt")
    print(f"best novel recall {best_f1:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
