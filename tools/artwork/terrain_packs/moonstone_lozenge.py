# SPDX-License-Identifier: GPL-3.0-or-later
"""The moonstone lozenge terrain material."""
import math
import numpy as np

from ._frost_paths import FROST, SEAM, weather
from ._paving import decay_fields, dress, stone_colour, stone_fates
from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    clamp01,
    domain_warp,
    fbm,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


def lozenge_paving(size, diag, key, gap):
    """Diamond flags on a 45 degree lattice of diagonal `diag`, with a square
    keystone of half-side `key` where four diamonds meet (render px)."""
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    n = size // diag
    best = np.full((size, size), -np.inf)
    ids = np.zeros((size, size), dtype=np.int64)
    centres = []

    def consider(cx, cy, metric, base):
        nonlocal best, ids
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                gx = np.floor((xs - cx) / diag + 0.5) + ox
                gy = np.floor((ys - cy) / diag + 0.5) + oy
                dx = np.abs(xs - (cx + gx * diag))
                dy = np.abs(ys - (cy + gy * diag))
                e = metric(dx, dy)
                take = e > best
                best = np.where(take, e, best)
                ids = np.where(take, base + (gy.astype(np.int64) % n) * n + gx.astype(np.int64) % n, ids)

    half = diag / 2
    diamond = lambda dx, dy: (half - (dx + dy)) / math.sqrt(2)
    square = lambda dx, dy: key - np.maximum(dx, dy)
    consider(0.0, 0.0, diamond, 0)
    consider(half, half, diamond, n * n)
    consider(half, 0.0, square, 2 * n * n)
    consider(0.0, half, square, 3 * n * n)
    for base_x, base_y in ((0, 0), (half, half), (half, 0), (0, half)):
        centres += [((base_x + i * diag) % size, (base_y + j * diag) % size) for j in range(n) for i in range(n)]
    return centres, ids.ravel().tolist(), (best - gap / 2).ravel().tolist(), 2 * n * n


@recipe(name="moonstone_lozenge", group="paths", label="Moonstone lozenge", profile="paved", border_class="built",
        seam=SEAM,
        palette={**FROST, "dark": (196, 194, 200), "mid": (220, 218, 222), "light": (238, 236, 238),
                 "warm": (230, 222, 216), "cool": (206, 208, 230), "vein": (146, 150, 176),
                 "key": (108, 120, 176), "key_light": (170, 180, 226)},
        style=Style(luma=192, std=20, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4)
def render_moonstone_lozenge(ctx):
    """Polished white marble lozenges with grey-blue veins and blue moonstone
    keystones: a kept, formal floor with hairline cracks and a frost sheen."""
    p, size = RECIPES["moonstone_lozenge"].palette, ctx.size
    centres, ids, edge, first_key = lozenge_paving(size, 32 * NATIVE_PX, 4.0 * NATIVE_PX, 1.25 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.68, 0.9))
    reach = 32 + 20 * NATIVE_PX
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.03, cracked=0.2, sink=0.4, heave=0.25)
    texture = layout_field(ctx, lambda rng: fbm(rng, 10, 3, size=size))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.3, own=0.6, staining=0.0)
    # Veins: a warped band field, each flag cut from a different part of the
    # block so its veining is strong, faint or absent.
    def vein_field(rng):
        f = fbm(rng, 5, 4, size=size)
        return domain_warp(f, fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 40)
    v = layout_field(ctx, vein_field)
    amount = [smoothstep(0.35, 0.9, fates[k].stain) for k in ids]
    veins = [a * (1 - smoothstep(0.0, 0.05, abs(x - 0.5))) for x, a in zip(v, amount)]
    rgb = mix(rgb, p["vein"], veins, 0.75)
    # Keystones: deep blue moonstone with a pale sheen toward the light.
    # About half the keystones are frosted over, so they do not form a dot grid.
    keys = [(1.0 if fates[k].tone > 0.45 else 0.25) if k >= first_key else 0.0 for k in ids]
    gem = ramp([clamp01(0.5 * t + 0.5 * smoothstep(0.0, 2.5 * NATIVE_PX, e)) for t, e in zip(texture, edge)],
               [(0.0, p["key"]), (1.0, p["key_light"])])
    rgb = mix(rgb, gem, keys)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, 12 * NATIVE_PX,
                joint=(0.0, 1.8), creep=2.5, chipping=1.0, bevel=1.6, bevel_strength=0.3,
                moss=1.0, weeds=0.35, lichen=0.4, grit=0.6, dust=0.15, lip=0.2, shadow=0.3, damp=0.5,
                finish_grain=0.05)
    rgb = weather(ctx, rgb, edge, decay, streaks=0.45, glaze=0.3)
    return rgb, None
