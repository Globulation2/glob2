# SPDX-License-Identifier: GPL-3.0-or-later
"""The tundra terrain material."""
import math

from terrain_core import (
    RECIPES,
    Style,
    TAU,
    barren_mark,
    blend_fields,
    block_object_field,
    block_worley,
    domain_warp,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    render_block_domes,
    smoothstep,
)
from ._heather import heather_cushions, soft_shadow


@recipe(name="tundra", group="barren", label="Tundra", profile="brush", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (66, 60, 50), "mid": (80, 73, 62), "light": (95, 88, 75), "rim": (122, 116, 106),
                 "crack": (50, 45, 40), "shadow": (58, 53, 46), "lichen": (160, 160, 144),
                 "stones": [(150, 145, 138), (120, 118, 115), (138, 132, 126), (110, 106, 102), (160, 154, 146)],
                 "stem": (76, 80, 52), "body": (94, 98, 62), "tip": (126, 120, 84),
                 "dead": (114, 80, 62), "deadtip": (130, 94, 72)},
        style=Style(luma=86, std=9.1, grain_max=12, match=0.7), periodic=True, grid=8, blocks=4)
def render_tundra(ctx):
    """Cold counterpart of heath on frost-patterned ground: polygons about two
    cells across outlined by faint pale raised rims, dull flat olive moss pads
    (a few bearberry dabs) in their centres, beds of grey stones, and the
    barren group's packed grit in the exposed dark peaty earth."""
    p = RECIPES["tundra"].palette
    size = ctx.size
    centre, _ = frost_polygons(ctx)
    rim = [1 - smoothstep(0.03, 0.12, d) for d in centre]
    rgb = tundra_ground(ctx, p, rim)
    stones, cover = tundra_stones(ctx, p)
    rgb = mix(rgb, p["shadow"], soft_shadow(cover), 0.25)
    rgb = mix(rgb, stones, cover, 1.0)
    drift = layout_field(ctx, lambda rng: fbm(rng, 2, 3, size=size))
    patch = [0.7 * smoothstep(0.08, 0.3, d) + 0.3 * v for d, v in zip(centre, drift)]
    mat, moss = heather_cushions(ctx, p, cells=10, tufts=1400, threshold=0.82, spread=0.2, light=0.15,
                                 part=0.1, dead_share=0.05, patch=patch)
    rgb = mix(rgb, p["shadow"], soft_shadow(moss), 0.12)
    return mix(rgb, mat, moss, 1.0), None


def frost_polygons(ctx, per_cell=0.25, warp=14):
    """Patterned ground: Worley polygons about two cells across. Returns the
    warped distance to the nearest polygon border (F2-F1, in polygon units)
    and its unwarped common reference."""
    size = ctx.size
    f1, f2, _, common1, common2, _ = block_worley(ctx, per_cell)
    wx = layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size))
    wy = layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size))
    centre = domain_warp([b - a for a, b in zip(f1, f2)], wx, wy, warp)
    return centre, [b - a for a, b in zip(common1, common2)]


def tundra_ground(ctx, p, rim):
    """Dark peaty earth under the barren group's mark, with a faint pale
    raised rim along the polygon borders and sparse pale lichen."""
    size = ctx.size
    base = layout_field(ctx, lambda rng: fbm(rng, 5, 3, size=size))
    rgb = ramp(base, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fines = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size))
    rgb = mix(rgb, p["rim"], [r * (0.5 + 0.5 * f) for r, f in zip(rim, fines)], 0.5)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, grit)] for ch in rgb]
    lichen = layout_field(ctx, lambda rng: lattice_noise(rng, 160, size=size))
    rgb = mix(rgb, p["lichen"], [smoothstep(0.88, 0.95, v) for v in lichen], 0.4)
    return barren_mark(ctx, rgb, p["crack"], strength=0.85, fade=0.1)


def tundra_stones(ctx, p, count=24, radius=(2.0, 12.0)):
    """Whole grey stones gathered in irregular beds, softly lit from the upper
    left. Boundary stones are kept by the common bed field, so every layout
    keeps the same ones. Returns (colour, cover)."""
    size = ctx.size
    common_bed = fbm(ctx.shared_rng, 4, 3, size=size)
    bed = blend_fields(common_bed, fbm(ctx.rng, 4, 3, size=size), ctx.interior_mask)
    shared, own = block_object_field(ctx, count, radius, spacing=1.05)

    def at(field, o):
        return field[int(o[1]) % size * size + int(o[0]) % size]

    shared = [o for o in shared if at(common_bed, o) > 0.62]
    own = [o for o in own if at(bed, o) > 0.62]
    tones = p["stones"]

    def tone_of(u):
        return tones[int(u * 7.0 * len(tones)) % len(tones)]

    def lumps_of(u):
        return ((2, 0.14, u * TAU), (3, 0.08, u * 17.0))

    def texture(u, angle, h):
        # Lit toward the upper left, darkest at the lower-right rim.
        return 1 + 0.16 * math.cos(angle + 0.75 * math.pi) * (1 - h) - 0.1 * (1 - h)

    height, rgb = render_block_domes(shared + own, tone_of, lumps_of, texture, size=size)
    return rgb, [smoothstep(0.0, 0.25, h) for h in height]
