# SPDX-License-Identifier: GPL-3.0-or-later
"""The sandstone terrain material."""
import math

from ._paving import decay_fields, dress, stone_fates, stone_field
from terrain_core import (
    N,
    NATIVE_PX,
    RECIPES,
    Style,
    clamp01,
    fbm,
    group_cracks,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


def flag_geometry(size, rng, merge=0.12):
    """Square flags, two by two per cell, a few pairs cut as one double flag,
    each with its own rounded corners, in joints about a native pixel wide.
    Returns the flag centres and, per pixel, the flag index and the distance
    from the flag's face (negative in the joint)."""
    flag = N // 2
    per_row = size // flag
    taken = [False] * (per_row * per_row)
    rects = []
    for k in range(per_row * per_row):
        if taken[k]:
            continue
        fx, fy = k % per_row, k // per_row
        w = h = 1
        u = rng.random()
        right, down = fy * per_row + (fx + 1) % per_row, ((fy + 1) % per_row) * per_row + fx
        if u < merge and not taken[right] and fx + 1 < per_row:
            w, taken[right] = 2, True
        elif u < 2 * merge and not taken[down] and fy + 1 < per_row:
            h, taken[down] = 2, True
        taken[k] = True
        rects.append((fx * flag, fy * flag, w * flag, h * flag, rng.uniform(3.0, 18.0), rng.uniform(1.5, 5.5)))
    ids, edge = [0] * (size * size), [0.0] * (size * size)
    # Each flag was cut its own size, so the joints run from one to three
    # native pixels wide.
    for k, (x0, y0, w, h, r, inset) in enumerate(rects):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                lx, ly = x - x0 + 0.5, y - y0 + 0.5
                dx, dy = min(lx, w - lx), min(ly, h - ly)
                cx, cy = max(0.0, r - dx), max(0.0, r - dy)
                e = r - math.hypot(cx, cy) if dx < r and dy < r else min(dx, dy)
                i = (y % size) * size + x % size
                ids[i] = k
                edge[i] = e - inset
    centres = [((x0 + w / 2) % size, (y0 + h / 2) % size) for x0, y0, w, h, _, _ in rects]
    return centres, ids, edge


@recipe(name="sandstone", group="paths", label="Sandstone tiles", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"ochre": (186, 156, 92), "mid": (156, 142, 104), "grey": (116, 116, 106), "joint": (92, 80, 62),
                 "crack": (86, 76, 62), "speck": (96, 86, 70), "warm": (178, 136, 104), "cool": (126, 136, 118),
                 "dust": (146, 130, 102), "soil": (96, 84, 64), "grit": (138, 122, 96), "stain": (158, 112, 70),
                 "rubble": (150, 140, 118)},
        style=Style(luma=130, std=18, grain_max=10, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_sandstone(ctx):
    """Square sandstone flags, two by two per cell, each its own shade between
    ochre and grey as the bed it was cut from ran, set in dark joints and worn:
    settled, chipped and sunken, a few split or lost to the soil, sand and
    grit in the joints and lichen on the stone. The flag grid is the same in
    every layout."""
    p, size = RECIPES["sandstone"].palette, ctx.size
    centres, ids, edge = flag_geometry(size, ctx.shared_rng)
    decay = decay_fields(ctx, reclaim=(0.85, 0.97))
    reach = 32 + N // 2
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.012, cracked=0.16, sink=0.45, heave=0.25)
    # Shades run across the path in broad bands, as the quarry bed ran, with
    # each flag a little off its neighbours.
    bed = stone_field(ctx, centres, reach, lambda rng: fbm(rng, 3, 2, size=size))
    # Two families, warm ochre and cool grey-tan, with the bed's drift.
    tone = [clamp01(0.3 * bed[k] + 0.25 * fates[k].tone + (0.38 if fates[k].cast > 0 else 0.0)) for k in ids]
    rgb = ramp(tone, [(0.0, p["grey"]), (0.5, p["mid"]), (1.0, p["ochre"])])
    # Each flag drifts a little pinker or greener, and a few carry rust stains.
    hue = [fates[k].hue for k in ids]
    rgb = mix(rgb, p["warm"], [max(0.0, h) for h in hue], 0.3)
    rgb = mix(rgb, p["cool"], [max(0.0, -h) for h in hue], 0.3)
    rgb = mix(rgb, p["stain"], [smoothstep(0.82, 0.95, fates[k].stain) for k in ids], 0.4)
    mottle = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = [[c * (0.9 + 0.2 * v) for c, v in zip(ch, mottle)] for ch in rgb]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))
    rgb = mix(rgb, p["speck"], [smoothstep(0.8, 0.92, v) for v in speck], 0.4)
    veins = group_cracks(ctx, cells=5, width=0.04, gaps=0.5)
    rgb = mix(rgb, p["crack"], veins, 0.45)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, N // 4,
                joint=(0.0, 2.0), creep=0.5, chipping=2.2, bevel=2.5, bevel_strength=0.3, crack_width=1.1,
                moss=0.4, weeds=0.4, lichen=0.7, grit=0.7, dust=0.15, lip=0.22)
    return rgb, None
