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
#! Tolerance for "close enough", in tiles, matching the reconciler's default
#! placementSearchRadius.
NEAR_RADIUS = 6


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
    # Precision@k is the metric that matches deployment. The reconciler does
    # not threshold the field: it ranks cells by score and acts on the top of
    # the queue (maxQueuedOrders). So what matters is whether the few cells the
    # net is most confident about are right, not whether a global threshold
    # separates 1 building from 16,383 empty cells — at that base rate,
    # thresholded precision is punishing and uninformative.
    topk_hits = {1: 0, 5: 0, 20: 0}
    topk_total = {1: 0, 5: 0, 20: 0}
    # Exact-cell precision understates usefulness: the reconciler relocates a
    # blocked or slightly-off desire to the best legal cell nearby, so a
    # prediction two tiles out still puts a building roughly where it belongs.
    # near_hits counts a top-1 pick as correct if any novel label cell lies
    # within NEAR_RADIUS tiles of it.
    near_hits = 0
    near_total = 0
    count_err = count_n = 0.0
    for i, (obs, building, areas, counts) in enumerate(loader):
        if i >= limit_batches:
            break
        obs = obs.to(device, non_blocking=True).to(memory_format=torch.channels_last)
        building = building.to(device, non_blocking=True)
        areas = areas.to(device, non_blocking=True)
        counts = counts.to(device, non_blocking=True)
        with torch.autocast("cuda", dtype=torch.float16):
            out = net(obs)
            loss = F.cross_entropy(out["building"].float(), building, weight=weights)
        # Mean absolute error in actual buildings, not log space: the number a
        # human can sanity-check against "a teacher holds 4-7 inns".
        pred_counts = torch.expm1(out["count"].float().clamp(max=6.0))
        count_err += (pred_counts - counts).abs().mean().item()
        count_n += 1
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

        # Rank novel candidates by the probability the net assigns to any
        # building at that cell, restricted to cells it does not already hold.
        probs = torch.softmax(out["building"].float(), dim=1)
        occupied_prob = 1.0 - probs[:, 0]
        cand = occupied_prob.masked_fill(existing, -1.0).flatten(1)
        truth = novel.flatten(1)
        for k in topk_hits:
            n_avail = min(k, cand.shape[1])
            idx = cand.topk(n_avail, dim=1).indices
            hits = truth.gather(1, idx).sum(dim=1)
            # Only score samples that actually have something novel to find;
            # a sample whose label adds nothing makes precision@k meaningless.
            has_novel = truth.any(dim=1)
            topk_hits[k] += hits[has_novel].sum().item()
            topk_total[k] += int(has_novel.sum().item()) * k

        # Dilate the novel-truth mask; wrap-aware would be better but the
        # error at the seam is negligible next to what this is measuring.
        dilated = F.max_pool2d(novel.float().unsqueeze(1),
                               kernel_size=2 * NEAR_RADIUS + 1,
                               stride=1, padding=NEAR_RADIUS).squeeze(1) > 0
        top1 = cand.argmax(dim=1)
        dil_flat = dilated.flatten(1)
        has_novel_1 = truth.any(dim=1)
        near_hits += dil_flat.gather(1, top1[:, None]).squeeze(1)[has_novel_1].sum().item()
        near_total += int(has_novel_1.sum().item())

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
    prec_at = {k: topk_hits[k] / max(topk_total[k], 1) for k in topk_hits}
    return dict(count_mae=count_err / max(count_n, 1),
                p1=prec_at[1], p5=prec_at[5], p20=prec_at[20],
                near1=near_hits / max(near_total, 1),
                loss=tot_loss / max(tot_n, 1), recall=recall, precision=precision,
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
    ap.add_argument("--count-weight", type=float, default=1.0,
                    help="weight on the per-type building-count loss")
    ap.add_argument("--novel-weight", type=float, default=4.0,
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
        for obs, building, areas, counts in train_loader:
            obs = obs.to(device, non_blocking=True).to(memory_format=torch.channels_last)
            building = building.to(device, non_blocking=True)
            areas = areas.to(device, non_blocking=True)
            counts = counts.to(device, non_blocking=True)
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
                # log1p so the loss is not dominated by the commonest types --
                # which is precisely the failure the count head exists to fix.
                loss_c = F.smooth_l1_loss(out["count"].float(),
                                          torch.log1p(counts))
                loss = loss_b + 0.3 * loss_a + args.count_weight * loss_c
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
        print(f"epoch {epoch}: train {run_loss/max(run_n,1):.4f} val {m['loss']:.4f} "
              f"| COPY {m['copy_recall']:.3f} | NOVEL rec {m['novel_recall']:.3f} "
              f"| P@1 {m['p1']:.3f} P@5 {m['p5']:.3f} near@1 {m['near1']:.3f} "
              f"| area {m['area_acc']:.4f} | {time.time()-t_epoch:.0f}s", flush=True)
        torch.save({"model": net.state_dict(), "args": vars(args),
                    "in_planes": in_planes, "metrics": m}, f"{args.out}/last.pt")
        # Select on precision@5: it is the only metric that reflects how the
        # field is consumed, and unlike recall it cannot be gamed by predicting
        # buildings everywhere.
        selector = m["p5"]
        if selector > best_f1:
            best_f1 = selector
            torch.save({"model": net.state_dict(), "args": vars(args),
                        "in_planes": in_planes, "metrics": m}, f"{args.out}/best.pt")
    print(f"best P@5 {best_f1:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
