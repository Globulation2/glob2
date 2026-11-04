# SPDX-License-Identifier: GPL-3.0-or-later
"""The overgrown road terrain material."""
import math
import numpy as np

from terrain_core import (
    NATIVE_PX,
    RECIPES,
    Style,
    TAU,
    blend_fields,
    cast,
    clamp01,
    domain_warp,
    fbm,
    flat,
    lattice_noise,
    layout_field,
    mix,
    normalize_like,
    ramp,
    recipe,
    shade,
    smoothstep,
    stamp_disc,
    stamp_stroke,
    worn_edge,
)


def hex_paving(size, columns, gap):
    """Flat-topped hexagonal setts over a periodic block, in render pixels.

    `columns` (even) columns of hexes cross the block; the row count is the
    nearest whole number, so hexes are squashed or stretched vertically by
    under a few percent and the lattice repeats exactly. Returns the stone
    centres, and per pixel the stone index and the distance from the stone
    face's edge (negative inside the `gap`-wide joint).
    """
    assert columns % 2 == 0
    step_x = size / columns
    radius = step_x / 1.5
    rows = max(1, round(size / (math.sqrt(3) * radius)))
    step_y = size / rows
    squash = math.sqrt(3) * radius / step_y
    apothem = math.sqrt(3) / 2 * radius
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    best = np.full((size, size), -np.inf)
    ids = np.zeros((size, size), dtype=np.int64)
    col0 = np.rint(xs / step_x).astype(np.int64)
    for dc in (-1, 0, 1):
        col = col0 + dc
        offset = 0.5 * (col % 2)
        row0 = np.rint(ys / step_y - offset).astype(np.int64)
        for dr in (-1, 0, 1):
            row = row0 + dr
            dx = np.abs(xs - col * step_x)
            dy = np.abs(ys - (row + offset) * step_y) * squash
            e = apothem - np.maximum(dy, dx * math.sqrt(3) / 2 + dy / 2)
            take = e > best
            best = np.where(take, e, best)
            ids = np.where(take, (row % rows) * columns + col % columns, ids)
    centres = [((c * step_x) % size, ((r + 0.5 * (c % 2)) * step_y) % size)
               for r in range(rows) for c in range(columns)]
    return centres, radius, ids.ravel().tolist(), (best - gap / 2).ravel().tolist()


@recipe(name="overgrown_road", group="paths", label="Overgrown road", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (95, 102, 84), "mid": (135, 144, 118), "light": (169, 178, 150),
                 "warm": (148, 144, 116), "cool": (116, 130, 128), "joint": (54, 42, 32),
                 "soil": (62, 52, 40), "moss_dark": (58, 66, 38), "moss": (88, 98, 52), "moss_light": (122, 128, 70),
                 "leaf": (104, 126, 58), "lichen": (156, 150, 96), "shadow": (34, 32, 26), "crack": (64, 58, 50)},
        style=Style(luma=120, std=24, grain_max=16, match=0.8), periodic=True, grid=8, blocks=4)
def render_overgrown_road(ctx):
    """Overgrown road: neat hexagonal stone setts in dark joints, decaying.
    Moss tints stretches of the joints, some setts are cracked or lost to soil
    and weeds, and in a few reclaimed patches the moss creeps over the stone.
    The hex lattice is the same in every layout; boundary setts take the
    shared stream's tone, tilt and fate, interior setts their layout's own.
    """
    p, size = RECIPES["overgrown_road"].palette, ctx.size
    gap = 1.0 * NATIVE_PX
    columns = 2 * max(1, round(size / (2 * 9 * NATIVE_PX)))
    centres, radius, ids, edge = hex_paving(size, columns, gap)
    interior = ctx.interior_mask
    # Reclaimed patches: where this broad field is high, setts are lost and
    # moss creeps over the stone; elsewhere the road is mostly intact.
    def growth_field(rng):
        return domain_warp(fbm(rng, 3, 3, size=size), fbm(rng, 3, 2, size=size), fbm(rng, 3, 2, size=size), 12)
    common_raw = growth_field(ctx.shared_rng)
    common_growth = normalize_like(common_raw, common_raw, 0.02, 0.98)
    growth = normalize_like(blend_fields(common_raw, growth_field(ctx.rng), interior), common_raw, 0.02, 0.98)
    patch = [smoothstep(0.68, 0.9, g) for g in growth]

    # Per-sett attributes: tone, warm/cool cast, tilt as the sett has settled,
    # whether it is lost or cracked, and the crack's heading, segments and kink. Setts reaching the
    # shared band use the shared stream, so every layout agrees at the block edge.
    def setts(rng, field):
        out = []
        for cx, cy in centres:
            g = smoothstep(0.68, 0.9, field[int(cy) % size * size + int(cx) % size])
            out.append((rng.random(), rng.random() * 2 - 1,
                        (rng.uniform(-1, 1), rng.uniform(-1, 1)),
                        rng.random() < 0.06 + 0.4 * g,
                        rng.random() < 0.1, rng.random() * TAU,
                        [rng.uniform(0.3, 0.5) for _ in range(rng.randint(2, 3))], rng.uniform(-1, 1)))
        return out
    common, own = setts(ctx.shared_rng, common_growth), setts(ctx.rng, growth)
    reach = 32 + radius + NATIVE_PX
    stones = [c if min(x, y, size - x, size - y) < reach else o for (x, y), c, o in zip(centres, common, own)]

    # Slightly chipped rims, the joint between setts, moss creeping 1-2 native
    # px over the stone in reclaimed patches, and each sett's settled height.
    # The shared stream's own surface is the common reference that keeps the
    # bevel's lighting scale, and so the block perimeter, identical in every layout.
    def surface(chip, setts_of, patch_of):
        face = [e - 1.2 * (c - 0.5) * 2 for e, c in zip(edge, chip)]
        lost = [1.0 if setts_of[k][3] else 0.0 for k in ids]
        joint = [1 - smoothstep(0.5, 2.0, f) for f in face]
        creep = [1 - smoothstep(c * 2 * NATIVE_PX, c * 2 * NATIVE_PX + 1.5, f) if c > 0.05 else 0.0
                 for f, c in zip(face, patch_of)]
        height = []
        for i in range(size * size):
            x, y, k = i % size + 0.5, i // size + 0.5, ids[i]
            cx, cy = centres[k]
            ox = (x - cx + size / 2) % size - size / 2
            oy = (y - cy + size / 2) % size - size / 2
            tx, ty = setts_of[k][2]
            h = smoothstep(-0.5, 2.0 * NATIVE_PX, face[i]) * (1 + 0.012 * (tx * ox + ty * oy))
            height.append(0.0 if lost[i] else h)
        return joint, [max(l, c) for l, c in zip(lost, creep)], height

    common_chip = fbm(ctx.shared_rng, 48, 2, size=size)
    chip = blend_fields(common_chip, fbm(ctx.rng, 48, 2, size=size), interior)
    joint, covered, height = surface(chip, stones, patch)
    common_joint, common_covered, common_height = surface(
        common_chip, common, [smoothstep(0.68, 0.9, g) for g in common_growth])

    # Stone colour, bevel and settling tilt.
    texture = layout_field(ctx, lambda rng: fbm(rng, 10, 3, size=size))
    tone = [clamp01(0.5 * t + 0.5 * stones[k][0]) for t, k in zip(texture, ids)]
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    cast_w = [stones[k][1] for k in ids]
    rgb = mix(rgb, p["warm"], [max(0.0, c) for c in cast_w], 0.35)
    rgb = mix(rgb, p["cool"], [max(0.0, -c) for c in cast_w], 0.35)
    rgb = shade(rgb, height, 0.3, reference_height=common_height)
    # A jagged crack running in from the rim of some setts, with a kink or two.
    cracks = [0.0] * (size * size)
    for (cx, cy), s in zip(centres, stones):
        if s[4] and not s[3]:
            angle, x, y = s[5], cx - math.cos(s[5]) * 0.85 * radius, cy - math.sin(s[5]) * 0.85 * radius
            for length in s[6]:
                angle += s[7] * 0.6
                stamp_stroke(cracks, x, y, angle, length * radius, 0.7 * NATIVE_PX)
                x, y = x + math.cos(angle) * length * radius, y + math.sin(angle) * length * radius
    rgb = mix(rgb, p["crack"], cracks, 0.75)
    # Lichen blotches on the stone, heavier in reclaimed patches.
    lichen_patch = layout_field(ctx, lambda rng: fbm(rng, 14, 3, size=size))
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 72, size=size))
    lichen = [smoothstep(0.6 - 0.15 * g, 0.8 - 0.15 * g, a) * smoothstep(0.35, 0.65, s)
              for a, s, g in zip(lichen_patch, speck, growth)]
    rgb = mix(rgb, p["lichen"], lichen, 0.4)

    # Joints: dark soil, with moss along a third to half of their length.
    joint_moss = layout_field(ctx, lambda rng: fbm(rng, 20, 2, size=size))
    joint_rgb = mix(flat(p["joint"], size), p["moss_dark"], [smoothstep(0.45, 0.6, v) for v in joint_moss], 0.8)
    rgb = mix(rgb, joint_rgb, joint)
    # Lost setts and creeping moss: soil under olive moss.
    moss_tex = layout_field(ctx, lambda rng: fbm(rng, 40, 3, size=size))
    veg = ramp([clamp01(0.7 * m + 0.3 * g) for m, g in zip(moss_tex, growth)],
               [(0.0, p["soil"]), (0.35, p["moss_dark"]), (0.7, p["moss"]), (1.0, p["moss_light"])])

    def clumps(rng):
        mask = [0.0] * (size * size)
        for _ in range(int(260 * ctx.area_scale / 64)):
            x, y = rng.random() * size, rng.random() * size
            for _ in range(rng.randint(3, 6)):
                a, d = rng.random() * TAU, rng.uniform(0, 2.0 * NATIVE_PX)
                stamp_disc(mask, x + d * math.cos(a), y + d * math.sin(a), rng.uniform(1.0, 1.6) * NATIVE_PX,
                           softness=0.5)
        return mask
    # Weeds grow from lost setts and the joints of reclaimed patches.
    def weeds(clump, covered_of, joint_of, growth_of):
        return [l * max(c, j * smoothstep(0.4, 0.8, g)) for l, c, j, g in zip(clump, covered_of, joint_of, growth_of)]
    common_clumps = clumps(ctx.shared_rng)
    leaves = weeds(blend_fields(common_clumps, clumps(ctx.rng), interior), covered, joint, growth)
    common_leaves = weeds(common_clumps, common_covered, common_joint, common_growth)
    veg = mix(veg, p["leaf"], leaves, 0.8)
    rgb = mix(rgb, veg, covered)
    rgb = mix(rgb, p["leaf"], leaves, 0.8)
    rgb = shade(rgb, leaves, 0.2, reference_height=common_leaves)
    # Raised setts and weeds shadow the ground beside them; the Paths mark's
    # worn lip runs beside every joint.
    rgb = cast(rgb, [h + 0.6 * l for h, l in zip(height, leaves)], p["shadow"], reach=NATIVE_PX, strength=0.4)
    rgb = worn_edge(rgb, joint, light=(NATIVE_PX, NATIVE_PX), strength=0.18)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.08 * (v - 0.5) * 2) for c, v in zip(channel, grit)] for channel in rgb]
    return rgb, None
