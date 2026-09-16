#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Neurotica policy network: a circular U-Net over the whole map.

Design notes that are load-bearing rather than taste:

* CIRCULAR padding everywhere. Glob2 maps wrap (Map::coordToIndex masks the
  coordinates), so the map is a torus. Zero padding would invent an edge that
  does not exist and break translation equivariance at the seam.

* Depthwise-separable convolutions at full resolution. A dense 3x3 at 48
  channels on 128x128 is ~300 MFLOP; the separable equivalent is roughly a
  seventh of that for nearly the same capacity on what is mostly local
  structure. Dense convs are kept at the low-resolution levels where they are
  cheap and where the long-range reasoning lives.

* U-Net depth is tied to the map size so the bottleneck is always 16x16. Maps
  are powers of two (Map::setSize takes log2 dimensions), so this is exact.

* The output is a set of full-resolution planes, not a coordinate. The
  reconciler turns those planes into orders (see NeuroticaReconciler.cpp), and
  placement is the argmax of the score over cells the engine says are legal —
  the same score/feasibility split AIEcho used, with the score learned.
"""

from __future__ import annotations

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

NUM_BUILDING_CLASSES = 14  # none + 13 building types


def circ_pad(x: torch.Tensor, p: int) -> torch.Tensor:
    return F.pad(x, (p, p, p, p), mode="circular")


class SepConv(nn.Module):
    """Depthwise 3x3 (circular) + pointwise 1x1, with norm and activation."""

    def __init__(self, cin: int, cout: int):
        super().__init__()
        self.dw = nn.Conv2d(cin, cin, 3, padding=0, groups=cin, bias=False)
        self.pw = nn.Conv2d(cin, cout, 1, bias=False)
        self.norm = nn.GroupNorm(8, cout)

    def forward(self, x):
        x = self.dw(circ_pad(x, 1))
        x = self.pw(x)
        return F.silu(self.norm(x))


class DenseConv(nn.Module):
    def __init__(self, cin: int, cout: int):
        super().__init__()
        self.conv = nn.Conv2d(cin, cout, 3, padding=0, bias=False)
        self.norm = nn.GroupNorm(8, cout)

    def forward(self, x):
        return F.silu(self.norm(self.conv(circ_pad(x, 1))))


class Block(nn.Module):
    """Residual pair, separable at high resolution and dense at low."""

    def __init__(self, ch: int, separable: bool):
        super().__init__()
        Conv = SepConv if separable else DenseConv
        self.a = Conv(ch, ch)
        self.b = Conv(ch, ch)

    def forward(self, x):
        return x + self.b(self.a(x))


class FiLM(nn.Module):
    """Feature-wise modulation of a plane stack by a global vector.

    Used to condition the decoder on the exploration latent. Modulating
    channels rather than concatenating a tiled vector keeps the latent's
    influence global and coherent — it shifts what KIND of map the decoder
    draws, rather than perturbing individual cells.
    """

    def __init__(self, latent_dim: int, ch: int):
        super().__init__()
        self.to_scale_shift = nn.Linear(latent_dim, ch * 2)
        nn.init.zeros_(self.to_scale_shift.weight)
        nn.init.zeros_(self.to_scale_shift.bias)

    def forward(self, x, z):
        scale, shift = self.to_scale_shift(z).chunk(2, dim=-1)
        scale = scale[:, :, None, None]
        shift = shift[:, :, None, None]
        return x * (1 + scale) + shift


class NeuroticaNet(nn.Module):
    """Policy and value for Neurotica.

    Behaviour cloning uses only the field heads. Self-play additionally uses
    the latent and the value head:

      * The field is a DETERMINISTIC function of (observation, z). Exploration
        happens by sampling z from the policy's own Gaussian, not by sampling
        each cell. Per-cell sampling would make log pi(a|s) a sum over ~16k
        terms, so PPO's importance ratio exp(sum of log-prob deltas) explodes
        on essentially every update — and it explores in a direction that is
        not strategically meaningful anyway. Flipping one tile is noise;
        shifting the latent is "expand north instead of teching".

      * That makes the RL action space `latent_dim` continuous dimensions
        rather than 13 x H x W discrete ones, which is what makes PPO
        tractable here at all.
    """

    def __init__(self, in_planes: int, width: int = 48, levels: int = 3,
                 latent_dim: int = 32):
        super().__init__()
        self.levels = levels
        self.latent_dim = latent_dim
        chans = [width * (2 ** i) for i in range(levels + 1)]

        self.stem = nn.Sequential(nn.Conv2d(in_planes, chans[0], 1, bias=False),
                                  nn.GroupNorm(8, chans[0]), nn.SiLU())
        self.enc = nn.ModuleList()
        self.down = nn.ModuleList()
        for i in range(levels):
            # Separable only at the two highest resolutions, where dense convs
            # would dominate the FLOP budget.
            self.enc.append(Block(chans[i], separable=(i < 2)))
            self.down.append(nn.Conv2d(chans[i], chans[i + 1], 2, stride=2, bias=False))

        self.mid = nn.Sequential(Block(chans[levels], separable=False),
                                 Block(chans[levels], separable=False))

        self.up = nn.ModuleList()
        self.dec = nn.ModuleList()
        for i in reversed(range(levels)):
            self.up.append(nn.ConvTranspose2d(chans[i + 1], chans[i], 2, stride=2, bias=False))
            self.dec.append(Block(chans[i], separable=(i < 2)))

        # Policy over the latent, and the critic, both read the pooled
        # bottleneck: they are global judgements, not per-cell ones.
        bottleneck = chans[levels]
        self.latent_mu = nn.Sequential(nn.Linear(bottleneck, 128), nn.SiLU(),
                                       nn.Linear(128, latent_dim))
        self.latent_logstd = nn.Parameter(torch.zeros(latent_dim))
        self.value = nn.Sequential(nn.Linear(bottleneck, 128), nn.SiLU(),
                                   nn.Linear(128, 1))
        self.film = nn.ModuleList([FiLM(latent_dim, chans[i]) for i in reversed(range(levels))])

        head_ch = chans[0]
        # One logit per building class per cell, plus the score that ranks
        # cells for the chosen class, plus the three area layers.
        self.head_building = nn.Conv2d(head_ch, NUM_BUILDING_CLASSES, 1)
        self.head_score = nn.Conv2d(head_ch, 1, 1)
        self.head_areas = nn.Conv2d(head_ch, 3, 1)

    def encode(self, x):
        x = self.stem(x)
        skips = []
        for i in range(self.levels):
            x = self.enc[i](x)
            skips.append(x)
            x = self.down[i](x)
        return self.mid(x), skips

    def decode(self, x, skips, z):
        for j, i in enumerate(reversed(range(self.levels))):
            x = self.up[j](x)
            x = x + skips[i]
            x = self.film[j](x, z)
            x = self.dec[j](x)
        return {
            "building": self.head_building(x),
            "score": self.head_score(x),
            "areas": self.head_areas(x),
        }

    def forward(self, x, z=None):
        """z=None means the zero latent, which is what behaviour cloning uses:
        FiLM is zero-initialised, so a zero latent is exactly the unconditioned
        network and a BC checkpoint stays a valid starting point for RL."""
        mid, skips = self.encode(x)
        pooled = mid.mean(dim=(2, 3))
        if z is None:
            z = torch.zeros(x.shape[0], self.latent_dim, device=x.device, dtype=x.dtype)
        out = self.decode(mid, skips, z)
        out["latent_mu"] = self.latent_mu(pooled.float())
        out["latent_logstd"] = self.latent_logstd.expand_as(out["latent_mu"])
        out["value"] = self.value(pooled.float()).squeeze(-1)
        return out

    def act(self, x, deterministic: bool = False):
        """Sample a latent from the policy and decode the field it implies."""
        mid, skips = self.encode(x)
        pooled = mid.mean(dim=(2, 3))
        mu = self.latent_mu(pooled.float())
        logstd = self.latent_logstd.expand_as(mu)
        std = logstd.exp()
        z = mu if deterministic else mu + std * torch.randn_like(mu)
        logp = (-0.5 * (((z - mu) / std) ** 2) - logstd
                - 0.5 * float(np.log(2 * np.pi))).sum(dim=-1)
        out = self.decode(mid, skips, z.to(x.dtype))
        out.update(latent=z, logp=logp, value=self.value(pooled.float()).squeeze(-1),
                   latent_mu=mu, latent_logstd=logstd)
        return out

    # --- placement actions -------------------------------------------------
    #
    # The action is WHICH PLACEMENTS TO MAKE, not the whole field and not the
    # latent.
    #
    # Sampling every cell independently makes log pi(a|s) a sum over ~200k
    # terms and PPO's ratio explodes. Making the latent the action avoids that
    # but is worse: the decoder never appears in log pi(a|s), so it receives no
    # gradient at all and the policy's PLAY can never change. Verified — the
    # policy loss sent exactly 0.0 gradient to FiLM, the decoder and the
    # building head.
    #
    # The reconciler never acts on the whole field anyway: it ranks cells by
    # score and drains a capped queue, so a handful of placements per step is
    # what actually happens. Sampling k of them gives a k-term log-prob, which
    # is tractable, and the gradient flows through the building head into the
    # decoder — the part that plays.
    #
    # Cells the team already occupies are excluded from the draw. Reproducing
    # an existing building is a copy, not a decision, and BC already does it at
    # 0.999 recall; leaving it deterministic keeps the sampled action pure
    # novelty.

    @staticmethod
    def placement_distribution(building_logits, existing_mask):
        """Categorical over empty cells, weighted by P(any building there)."""
        probs = torch.softmax(building_logits.float(), dim=1)
        occupied = 1.0 - probs[:, 0]
        cand = occupied.masked_fill(existing_mask, 0.0).flatten(1)
        # A row with no legal candidate would make a degenerate distribution;
        # fall back to uniform so sampling stays defined.
        empty_rows = cand.sum(dim=1, keepdim=True) <= 0
        cand = torch.where(empty_rows, torch.ones_like(cand), cand)
        return torch.distributions.Categorical(probs=cand / cand.sum(dim=1, keepdim=True))

    def act_placements(self, x, existing_mask, k: int = 8):
        out = self.forward(x)
        dist = self.placement_distribution(out["building"], existing_mask)
        idx = dist.sample((k,)).T.contiguous()            # (B, k)
        out["placements"] = idx
        out["logp"] = dist.log_prob(idx.T).sum(dim=0)     # (B,)
        out["entropy"] = dist.entropy()
        return out

    def evaluate_placements(self, x, idx, existing_mask):
        """Re-score stored placements under the current policy, for PPO."""
        out = self.forward(x)
        dist = self.placement_distribution(out["building"], existing_mask)
        return dict(logp=dist.log_prob(idx.T).sum(dim=0),
                    entropy=dist.entropy(),
                    value=out["value"])

    def evaluate_latent(self, x, z):
        """Re-score a stored latent under the current policy, for PPO."""
        mid, skips = self.encode(x)
        pooled = mid.mean(dim=(2, 3))
        mu = self.latent_mu(pooled.float())
        logstd = self.latent_logstd.expand_as(mu)
        std = logstd.exp()
        logp = (-0.5 * (((z - mu) / std) ** 2) - logstd
                - 0.5 * float(np.log(2 * np.pi))).sum(dim=-1)
        entropy = (logstd + 0.5 * float(np.log(2 * np.pi * np.e))).sum(dim=-1)
        return dict(logp=logp, entropy=entropy,
                    value=self.value(pooled.float()).squeeze(-1))


def count_params(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


if __name__ == "__main__":
    import time
    net = NeuroticaNet(60).cuda().to(memory_format=torch.channels_last)
    print(f"params: {count_params(net)/1e6:.2f} M")
    x = torch.randn(8, 60, 128, 128, device="cuda").to(memory_format=torch.channels_last)
    for dtype in (torch.float16, torch.bfloat16):
        with torch.autocast("cuda", dtype=dtype):
            for _ in range(3):
                net(x)
            torch.cuda.synchronize()
            t0 = time.time()
            for _ in range(20):
                net(x)
            torch.cuda.synchronize()
        dt = (time.time() - t0) / 20
        print(f"{str(dtype):>16}: {dt*1000:6.1f} ms/batch8  "
              f"{8/dt:7.0f} samples/s")
