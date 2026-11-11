# SPDX-License-Identifier: GPL-3.0-or-later
"""The sandstone terrain material."""
import numpy as np

from ._paving import decay_fields, dress, per_stone, stone_fates, stone_field
from terrain_core import (
    N,
    RECIPES,
    Style,
    clamp01,
    fbm,
    group_cracks,
    hypot,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    smoothstep,
)


FLAGS_PER_ROW = 23  # flags across the 8-tile block: about 0.36 of a tile each


def flag_geometry(size, rng):
    """Square flags on an even grid, FLAGS_PER_ROW across the block, each
    with its own rounded corners, in joints about a native pixel wide.
    Returns the flag centres and, per pixel, the flag index and the distance
    from the flag's face (negative in the joint)."""
    per_row = FLAGS_PER_ROW * size // (8 * N)
    flag = size / per_row
    # Each flag was cut its own size, so the joints run from one to three
    # native pixels wide.
    cut = np.array([(rng.uniform(2.0, 12.0), rng.uniform(1.5, 5.5)) for _ in range(per_row * per_row)])
    centre = np.arange(size) + 0.5
    cell = (centre / flag).astype(np.int64)
    local = centre - cell * flag
    near = np.minimum(local, flag - local)
    fy, fx = cell[:, None], cell[None, :]
    dy, dx = np.broadcast_arrays(near[:, None], near[None, :])
    k = fy * per_row + fx
    r, inset = cut[k, 0], cut[k, 1]
    corner = (dx < r) & (dy < r)
    e = np.minimum(dx, dy)
    e[corner] = r[corner] - hypot(np.maximum(0.0, r - dx)[corner], np.maximum(0.0, r - dy)[corner])
    centres = [((fx + 0.5) * flag, (fy + 0.5) * flag) for fy in range(per_row) for fx in range(per_row)]
    return centres, k.ravel(), (e - inset).ravel()


@recipe(name="sandstone", group="paths", label="Sandstone tiles", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"ochre": (186, 156, 92), "mid": (156, 142, 104), "grey": (116, 116, 106), "joint": (92, 80, 62),
                 "crack": (86, 76, 62), "speck": (96, 86, 70), "warm": (178, 136, 104), "cool": (126, 136, 118),
                 "dust": (146, 130, 102), "soil": (96, 84, 64), "grit": (138, 122, 96), "stain": (158, 112, 70),
                 "rubble": (150, 140, 118)},
        style=Style(luma=130, std=18, grain_max=11, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_sandstone(ctx):
    """Small square sandstone flags, about three across a cell, each its own shade between
    ochre and grey as the bed it was cut from ran, set in dark joints and worn:
    settled, chipped and sunken, a few split or lost to the soil, sand and
    grit in the joints and lichen on the stone. The flag grid is the same in
    every layout."""
    p, size = RECIPES["sandstone"].palette, ctx.size
    centres, ids, edge = flag_geometry(size, ctx.shared_rng)
    decay = decay_fields(ctx, reclaim=(0.85, 0.97))
    reach = 32 + N // 2  # past a flag from the block edge
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.012, cracked=0.16, sink=0.45, heave=0.25)
    # Shades run across the path in broad bands, as the quarry bed ran, with
    # each flag a little off its neighbours.
    bed = stone_field(ctx, centres, reach, lambda rng: fbm(rng, 3, 2, size=size, array=True))
    # Two families, warm ochre and cool grey-tan, with the bed's drift.
    tone = np.array([clamp01(0.3 * b + 0.25 * f.tone + (0.38 if f.cast > 0 else 0.0)) for b, f in zip(bed, fates)])
    rgb = ramp(tone[ids], [(0.0, p["grey"]), (0.5, p["mid"]), (1.0, p["ochre"])])
    # Each flag drifts a little pinker or greener, and a few carry rust stains.
    hue = per_stone(fates, lambda f: f.hue)[ids]
    rgb = mix(rgb, p["warm"], np.maximum(0.0, hue), 0.3)
    rgb = mix(rgb, p["cool"], np.maximum(0.0, -hue), 0.3)
    rgb = mix(rgb, p["stain"], smoothstep(0.82, 0.95, per_stone(fates, lambda f: f.stain))[ids], 0.4)
    mottle = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size, array=True))
    rgb = rgb * (0.9 + 0.2 * mottle)
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size, array=True))
    rgb = mix(rgb, p["speck"], smoothstep(0.8, 0.92, speck), 0.4)
    veins = group_cracks(ctx, cells=5, width=0.04, gaps=0.5, array=True)
    rgb = mix(rgb, p["crack"], veins, 0.45)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, N // 6,
                joint=(0.0, 2.0), creep=0.5, chipping=2.2, bevel=2.5, bevel_strength=0.3, crack_width=1.1,
                moss=0.4, weeds=0.4, lichen=0.7, grit=0.7, dust=0.15, lip=0.22)
    return rgb, None
