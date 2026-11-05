# SPDX-License-Identifier: GPL-3.0-or-later
"""The scoria blocks terrain material.

Wear: scoria is soft and full of holes, so it wears away rather than
chipping. Rims crumble back irregularly and shed loose cinders into the
joints; the walked stretches dish each block's face and open its pores,
which fill with pale ash; cracks run across the blocks' short side; oxidised
red blocks bleed rust into the joints beside them; and lost blocks leave
pits of loose cinder rather than soil.
"""
import math

from ._rock_paths import ASH_GROUND, grains, layout_pair, stamped
from ._paving import decay_fields, dress, rectangle_geometry, stone_colour, stone_fates
from terrain_core import (N, NATIVE_PX, RECIPES, Style, fbm, layout_field, mix, ramp, recipe, shade, smoothstep,
                          stamp_disc)

CELL = N  # one basketweave square: two blocks of one tile x half a tile


def basketweave(size, rng):
    per = size // CELL
    half = CELL / 2
    rects = []
    for j in range(per):
        for i in range(per):
            # Offset a quarter square so neither the outer nor the inner joint
            # of a square runs along a tile edge.
            x, y = i * CELL + CELL / 4, j * CELL + CELL / 4
            r = lambda: rng.uniform(6.0, 16.0)
            if (i + j) % 2 == 0:
                rects += [(x, y, CELL, half, r()), (x, y + half, CELL, half, r())]
            else:
                rects += [(x, y, half, CELL, r()), (x + half, y, half, CELL, r())]
    return rects


def pores(ctx, rng, density):
    """Vesicles, `density` clusters per tile: gas bubbles froze in swarms, so
    each cluster is a few larger holes among many pinholes, a little
    stretched along one flow direction."""
    mask = [0.0] * (ctx.size * ctx.size)
    for _ in range(int(density * ctx.area_scale)):
        x, y = rng.random() * ctx.size, rng.random() * ctx.size
        spread = rng.uniform(2.0, 6.0) * NATIVE_PX
        for _ in range(rng.randint(3, 9)):
            px, py = x + rng.gauss(0, spread) * 1.4, y + rng.gauss(0, spread) * 0.7
            stamp_disc(mask, px, py, (0.5 + 1.6 * rng.random() ** 3) * NATIVE_PX, softness=0.45)
    return mask


@recipe(name="scoria_blocks", group="paths", label="Scoria blocks", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={**ASH_GROUND, "dark": (44, 40, 38), "mid": (66, 60, 56), "light": (90, 84, 78),
                 "warm": (92, 64, 52), "cool": (54, 56, 60), "stain": (120, 66, 40), "pore": (12, 11, 11),
                 # Lost blocks leave loose cinder, dark with red grains.
                 "soil": (44, 40, 38), "rubble": (104, 62, 46), "grit": (96, 90, 84),
                 "cinder": (30, 27, 26), "cinder_red": (112, 58, 38), "rust": (126, 70, 42),
                 "ash_fill": (124, 118, 108), "joint": (90, 84, 78)},
        style=Style(luma=70, std=17, grain_max=16, match=0.8), periodic=True, grid=8, blocks=4)
def render_scoria_blocks(ctx):
    p, size = RECIPES["scoria_blocks"].palette, ctx.size
    rects = basketweave(size, ctx.shared_rng)
    centres, ids, edge, _, _ = rectangle_geometry(size, rects, gap=0.9 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.8, 0.95))
    fates, common = stone_fates(ctx, decay, centres, 32 + CELL, lost=0.03, cracked=0.25, sink=0.8, heave=0.35)
    texture = layout_field(ctx, lambda rng: fbm(rng, 14, 4, size=size))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.25, own=0.5, staining=0.15)

    # Crumbling rims: the soft stone is bitten back one to three native px,
    # unevenly along each rim, widening the joint where it does.
    # Discrete bites, not an even fuzz. They come from the shared stream and the
    # common wear only: the rims are geometry, and every layout's bevel lighting
    # takes its scale from them, so they must be the same in every layout.
    erosion, _ = layout_pair(ctx, lambda rng: fbm(rng, 6, 3, size=size))
    edge = [e - 1.5 * NATIVE_PX * smoothstep(0.55, 0.62, r) * (1.0 + w)
            for e, r, w in zip(edge, erosion, decay.common_wear)]

    # Dished faces where the path is walked: lit as a shallow bowl, dusty.
    def dish_of(wear):
        return [-smoothstep(1.0 * NATIVE_PX, 8 * NATIVE_PX, e) * smoothstep(0.3, 0.8, w) for e, w in zip(edge, wear)]
    dish = dish_of(decay.wear)
    rgb = mix(rgb, p["dust"], [-d * 0.15 for d in dish], 1.0)
    rgb = shade(rgb, dish, 0.35, reference_height=dish_of(decay.common_wear))

    # Pores in clusters; on a few stretches they are packed with pale ash.
    holes = stamped(ctx, lambda rng: pores(ctx, rng, 7))
    rgb = mix(rgb, p["pore"], holes, 0.85)
    fill_c, fill = layout_pair(ctx, lambda rng: fbm(rng, 6, 2, size=size))
    rgb = mix(rgb, p["ash_fill"], [h * smoothstep(0.62, 0.75, f) for h, f in zip(holes, fill)], 0.5)

    # Cracks across each block's short side.
    across = [math.pi / 2 if w >= h else 0.0 for _, _, w, h, _ in rects]
    # A lost block is often only partly gone: a ragged pit in what remains. The
    # pit outline comes from the shared stream: `dress` also lights its common
    # reference surface with it, and that sets every layout's lighting scale.
    broken, _ = layout_pair(ctx, lambda rng: fbm(rng, 4, 2, size=size))
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, CELL * 0.25,
                joint=(0.0, 1.5), creep=0.8, chipping=1.6, bevel=1.8, bevel_strength=0.34, crack_width=1.0,
                crack_along=across, moss=0.0, weeds=0.15, lichen=0.25, grit=0.3, grit_cover=0.5, dust=0.12,
                lip=0.26, shadow=0.45, broken=broken)

    # Rust bleeding from oxidised blocks into the joint beside them.
    near_joint = [1 - smoothstep(-1.0 * NATIVE_PX, 2.5 * NATIVE_PX, e) for e in edge]
    redness = [1.0 if fates[k].hue > 0.6 else 0.0 for k in ids]
    streak = layout_field(ctx, lambda rng: fbm(rng, 16, 2, size=size))
    rgb = mix(rgb, p["rust"], [j * r * smoothstep(0.4, 0.6, s) for j, r, s in zip(near_joint, redness, streak)], 0.25)
    # Loose cinders shed from the rims, lying in the joints and on worn rims.
    crumbs, red_crumbs = stamped(ctx, lambda rng: (grains(ctx, rng, 2200, 0.35, 0.9), grains(ctx, rng, 200, 0.35, 0.8)))
    rgb = mix(rgb, p["cinder"], [c * j for c, j in zip(crumbs, near_joint)], 0.85)
    rgb = mix(rgb, p["cinder_red"], [c * j for c, j in zip(red_crumbs, near_joint)], 0.8)
    # Pits of lost blocks: a heap of loose cinder, lit as a low mound.
    lost = [1.0 if fates[k].lost and b > 0.5 else 0.0 for k, b in zip(ids, broken)]
    heap = layout_field(ctx, lambda rng: fbm(rng, 40, 3, size=size))
    cinder_bed = ramp(heap, [(0.0, p["cinder"]), (0.6, p["mid"]), (1.0, p["light"])])
    cinder_bed = mix(cinder_bed, p["cinder_red"], [c for c in red_crumbs], 0.9)
    cinder_bed = mix(cinder_bed, p["pore"], [c for c in crumbs], 0.6)
    rgb = mix(rgb, cinder_bed, [l * 0.85 for l in lost])
    return rgb, None
