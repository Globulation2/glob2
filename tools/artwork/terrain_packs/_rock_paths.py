# SPDX-License-Identifier: GPL-3.0-or-later
"""Ground and wear helpers for the rock paths: slate ashlar, basalt causeway
and scoria blocks.

They are laid paths and use `_paving` for settling, chipping, joints and
reclaim; this module adds the regional ground their joints fill with (grey
upland grit, or pale volcanic ash so near-black stone still reads as laid)
and the stamp helpers their own wear is drawn with. Every helper keeps the
block contract: forked shared and per-layout streams, blended by the block's
interior mask, so all layouts agree at the perimeter.
"""
import math
import random

from terrain_core import NATIVE_PX, TAU, blend_fields, stamp_disc

# Volcanic ground for the joints of dark paths: pale ash and grit instead of
# soil and green moss.
ASH_GROUND = {
    "joint": (118, 112, 104), "grit": (140, 134, 124), "soil": (86, 80, 74),
    "moss_dark": (82, 80, 70), "moss": (96, 94, 80), "moss_light": (112, 110, 94),
    "leaf": (92, 92, 74), "lichen": (150, 148, 132), "shadow": (16, 15, 15),
    "crack": (20, 19, 19), "dust": (104, 98, 92), "damp": (34, 34, 36), "rubble": (78, 74, 70),
    "stain": (118, 78, 54),
}

# Grey upland ground: dark grit joints, sparse grey-olive moss.
SLATE_GROUND = {
    "joint": (44, 44, 46), "grit": (98, 98, 96), "soil": (58, 54, 50),
    "moss_dark": (56, 60, 50), "moss": (70, 76, 60), "moss_light": (86, 92, 72),
    "leaf": (76, 84, 60), "lichen": (156, 158, 146), "shadow": (22, 22, 24),
    "crack": (30, 30, 34), "dust": (112, 110, 106), "damp": (40, 42, 46), "rubble": (104, 106, 108),
}


def stamped(ctx, make):
    """Stamp masks from the shared stream at the block edge, varied inside.
    `make(rng)` may return one mask or a tuple of masks."""
    common = make(random.Random(ctx.shared_rng.random()))
    own = make(random.Random(ctx.rng.random()))
    if isinstance(common, tuple):
        return tuple(blend_fields(c, o, ctx.interior_mask) for c, o in zip(common, own))
    return blend_fields(common, own, ctx.interior_mask)


def layout_pair(ctx, make):
    """(common field, blended field) from forked streams, so one field can
    drive both colour and relief, with the common field as the relief's
    lighting reference."""
    common = make(random.Random(ctx.shared_rng.random()))
    own = make(random.Random(ctx.rng.random()))
    return common, blend_fields(common, own, ctx.interior_mask)


def rosettes(ctx, rng, count, r_min, r_max):
    """Crustose lichen: round patches with a brighter, lobed rim, `count` per
    8x8 block. Returns (body, rim) masks."""
    size = ctx.size
    body, rim = [0.0] * (size * size), [0.0] * (size * size)
    for _ in range(int(count * ctx.area_scale / 64)):
        x, y = rng.random() * size, rng.random() * size
        r = rng.uniform(r_min, r_max) * NATIVE_PX
        stamp_disc(body, x, y, r, 0.8, softness=0.3)
        for _ in range(rng.randint(5, 9)):
            a = rng.random() * TAU
            stamp_disc(rim, x + 0.8 * r * math.cos(a), y + 0.8 * r * math.sin(a),
                       rng.uniform(0.3, 0.5) * r, 1.0, softness=0.5)
    rim = [min(r, 1.0) * (1.0 if b > 0.05 else 0.0) for r, b in zip(rim, body)]
    return body, rim


def grains(ctx, rng, count, r_min, r_max):
    """Loose grains (cinders, grit), `count` per 8x8 block."""
    size = ctx.size
    mask = [0.0] * (size * size)
    for _ in range(int(count * ctx.area_scale / 64)):
        stamp_disc(mask, rng.random() * size, rng.random() * size, rng.uniform(r_min, r_max) * NATIVE_PX,
                   softness=0.4)
    return mask
