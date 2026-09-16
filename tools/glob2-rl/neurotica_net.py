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


class NeuroticaNet(nn.Module):
    def __init__(self, in_planes: int, width: int = 48, levels: int = 3):
        super().__init__()
        self.levels = levels
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

        head_ch = chans[0]
        # One logit per building class per cell, plus the score that ranks
        # cells for the chosen class, plus the three area layers.
        self.head_building = nn.Conv2d(head_ch, NUM_BUILDING_CLASSES, 1)
        self.head_score = nn.Conv2d(head_ch, 1, 1)
        self.head_areas = nn.Conv2d(head_ch, 3, 1)

    def forward(self, x):
        x = self.stem(x)
        skips = []
        for i in range(self.levels):
            x = self.enc[i](x)
            skips.append(x)
            x = self.down[i](x)
        x = self.mid(x)
        for j, i in enumerate(reversed(range(self.levels))):
            x = self.up[j](x)
            x = x + skips[i]
            x = self.dec[j](x)
        return {
            "building": self.head_building(x),
            "score": self.head_score(x),
            "areas": self.head_areas(x),
        }


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
