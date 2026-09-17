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

# Swarm production mixes the policy may choose between, as
# (worker, explorer, warrior) weights. A discrete choice rather than a free
# proportion because PPO needs a log-prob it can trust, and because the real
# decision here is coarse: economy, or army, or the balance teachers strike
# (~1/3 warriors, measured). Cloning the teacher mix outright lost games in a
# small economy -- the affordability of a split is a precondition the ratio
# does not carry -- so the mix has to be chosen from the state, by something
# that learns from outcomes.
MIX_PRESETS = [
    (1, 0, 0),   # all workers: fastest economy, cannot ever win by force
    (3, 0, 1),   # light military
    (2, 0, 1),   # roughly the teacher split
    (1, 0, 1),   # heavy military
    (2, 1, 1),   # with explorers, for map control
]


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
        # Desired staffing (Building::maxUnitWorking) per cell. This is how the
        # teachers concentrate labour, and labour is what the early economy
        # turns on: a swarm only produces when wheat reaches it, so workers
        # spread across every building are workers not feeding the swarm.
        # Predicted in units, supervised only at building anchors.
        self.head_workers = nn.Conv2d(head_ch, 1, 1)
        # Swarm unit-production mix: worker / explorer / warrior, as logits
        # over the three. A swarm defaults to workers-only and the policy had
        # no way to change it, so Neurotica could never field an army --
        # warriors=0 in every telemetry read regardless of checkpoint. Unlike
        # absolute staffing, a ratio is scale-free and should survive being
        # applied to a much smaller economy than the teachers had.
        self.head_ratio = nn.Conv2d(head_ch, 3, 1)
        # Which production mix to run, as a global choice off the pooled
        # bottleneck. This is an ACTION, not a prediction: the per-cell ratio
        # head above receives no policy gradient, because PPO only credits the
        # placements, so it can never be improved by playing. Making the mix a
        # sampled action is what lets RL discover the economy/army tradeoff the
        # way it discovered to stop building flags.
        self.head_mix = nn.Sequential(nn.Linear(bottleneck, 64), nn.SiLU(),
                                      nn.Linear(64, len(MIX_PRESETS)))
        # How many of each building type the team should HOLD. The per-cell
        # building head is a marginal -- it says where inn-ness is high, never
        # how many inns to own -- so decoding it by threshold or top-k turns
        # "inn-ness everywhere" into dozens of inns. Measured: swarm=48 inn=57
        # against a teacher's 4 and 7. Count is a global judgement, so it reads
        # the pooled bottleneck like value does, and is trained on log1p counts
        # so the loss is not dominated by the commonest types.
        self.head_count = nn.Sequential(nn.Linear(bottleneck, 128), nn.SiLU(),
                                        nn.Linear(128, NUM_BUILDING_CLASSES - 1))

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
            "workers": self.head_workers(x).squeeze(1),
            "ratio": self.head_ratio(x),
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
        # Detached: the count head is auxiliary, and must not reshape the
        # trunk. Trained attached at count_weight=1.0 it dominated the shared
        # encoder -- its loss is ~0.1 against the building head's ~0.004 -- and
        # drove novel_precision from 0.185 down to 0.035 while learning counts
        # almost exactly (count_mae 0.116). Counts are worth having only if
        # placement survives them.
        out["count"] = self.head_count(pooled.float().detach())
        out["mix"] = self.head_mix(pooled.float())
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
    def placement_distribution(building_logits, existing_mask, allowed=None,
                               temperature=1.0):
        """Categorical over empty cells, weighted by P(any building there).

        temperature < 1 sharpens the draw toward the greedy top-k. Measured on
        the same weights: greedy top-8 scored 78% against numbi, sampled k=8
        scored 18% -- the distribution is diffuse enough (novel precision
        ~0.18) that sampling mostly draws bad cells, so PPO was training a
        policy far from the one being evaluated. Like the allowed mask, the
        temperature used to act is stored per step and passed back here for
        evaluation, so the scored distribution is the one that acted.
        temperature may be a float or a (B,) tensor.
        """
        probs = torch.softmax(building_logits.float(), dim=1)
        occupied = 1.0 - probs[:, 0]
        cand = occupied.masked_fill(existing_mask, 0.0).flatten(1)
        if not (isinstance(temperature, (int, float)) and temperature == 1.0):
            inv = (1.0 / torch.as_tensor(temperature, dtype=cand.dtype,
                                          device=cand.device)).reshape(-1, 1)
            cand = cand.clamp(min=1e-12).pow(inv)
        if allowed is not None:
            cand = cand * allowed.float()
        # A row with no legal candidate would make a degenerate distribution.
        # Fall back to the EMPTY cells, never to every cell: a uniform over
        # covered cells puts placements on non-anchor footprint cells, which the
        # reconciler reads as requests for further buildings -- the anchor
        # hazard re-entering through the back door.
        empty_rows = cand.sum(dim=1, keepdim=True) <= 0
        free = (~existing_mask).flatten(1).float()
        free = torch.where(free.sum(dim=1, keepdim=True) <= 0, torch.ones_like(free), free)
        cand = torch.where(empty_rows, free, cand)
        return torch.distributions.Categorical(probs=cand / cand.sum(dim=1, keepdim=True))

    def act_placements(self, x, existing_mask, k: int = 8, allowed=None,
                       decide_mix=None, out=None, temperature=1.0):
        """Sample the joint action.

        allowed: (B, H*W) bool, computed by the caller from the observation and
        stored with the trajectory, so evaluation scores exactly the
        distribution that acted. decide_mix: (B,) bool -- on steps where the
        mix is held rather than re-chosen, no mix term enters the log-prob.
        out: a forward already computed on x, to avoid running the trunk twice.
        """
        if out is None:
            out = self.forward(x)
        dist = self.placement_distribution(out["building"], existing_mask, allowed,
                                           temperature)
        idx = dist.sample((k,)).T.contiguous()            # (B, k)
        mix_dist = torch.distributions.Categorical(logits=out["mix"].float())
        mix = mix_dist.sample()                           # (B,)
        if decide_mix is None:
            decide_mix = torch.ones_like(mix, dtype=torch.bool)
        zero = torch.zeros(mix.shape[0], device=mix.device)
        out["placements"] = idx
        out["mix_choice"] = mix
        out["logp"] = (dist.log_prob(idx.T).sum(dim=0)
                       + torch.where(decide_mix, mix_dist.log_prob(mix), zero))
        out["entropy"] = dist.entropy() + torch.where(decide_mix, mix_dist.entropy(), zero)
        return out

    def evaluate_placements(self, x, idx, existing_mask, allowed=None,
                            mix=None, decide_mix=None, temperature=1.0):
        """Re-score stored placements under the current policy, for PPO.

        allowed and decide_mix must be the STORED values from when the action
        was taken. Recomputing the mask here from the current trunk scores a
        different distribution than the one that acted: a stored cell that has
        since become disallowed gets probability ~0, its ratio ~0, and it drops
        out of the gradient silently.
        """
        out = self.forward(x)
        dist = self.placement_distribution(out["building"], existing_mask, allowed,
                                           temperature)
        logp = dist.log_prob(idx.T).sum(dim=0)
        entropy = dist.entropy()
        if mix is not None:
            mix_dist = torch.distributions.Categorical(logits=out["mix"].float())
            if decide_mix is None:
                decide_mix = torch.ones_like(mix, dtype=torch.bool)
            zero = torch.zeros_like(logp)
            logp = logp + torch.where(decide_mix, mix_dist.log_prob(mix), zero)
            entropy = entropy + torch.where(decide_mix, mix_dist.entropy(), zero)
        return dict(logp=logp, entropy=entropy, value=out["value"])

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
