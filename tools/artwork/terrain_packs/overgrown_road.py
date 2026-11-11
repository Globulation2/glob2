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
    domain_warp,
    fbm,
    flat,
    lattice_noise,
    layout_field,
    mix,
    normalize_like,
    ramp,
    record_stones,
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
    return centres, radius, ids.ravel(), (best - gap / 2).ravel()


@recipe(name="overgrown_road", group="paths", label="Mossy setts", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={"dark": (95, 102, 84), "mid": (135, 144, 118), "light": (169, 178, 150),
                 "warm": (148, 144, 116), "cool": (116, 130, 128), "joint": (54, 42, 32),
                 "soil": (62, 52, 40), "moss_dark": (58, 66, 38), "moss": (88, 98, 52), "moss_light": (122, 128, 70),
                 "leaf": (104, 126, 58), "lichen": (156, 150, 96), "shadow": (34, 32, 26), "crack": (64, 58, 50)},
        style=Style(luma=120, std=24, grain_max=16, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_overgrown_road(ctx):
    """Mossy setts (material key overgrown_road): neat hexagonal stone setts
    in dark joints, decaying.
    Moss tints stretches of the joints, some setts are cracked or lost to soil
    and weeds, and in a few reclaimed patches the moss creeps over the stone.
    The hex lattice is the same in every layout; boundary setts take the
    shared stream's tone, tilt and fate, interior setts their layout's own.
    """
    p, size = RECIPES["overgrown_road"].palette, ctx.size
    gap = 1.0 * NATIVE_PX
    columns = 2 * max(1, round(size / (2 * 9 * NATIVE_PX)))
    centres, radius, ids, edge = hex_paving(size, columns, gap)
    record_stones(ctx, ids, edge, centres)
    interior = ctx.interior
    # Reclaimed patches: where this broad field is high, setts are lost and
    # moss creeps over the stone; elsewhere the road is mostly intact.
    def growth_field(rng):
        return domain_warp(fbm(rng, 3, 3, size=size, array=True), fbm(rng, 3, 2, size=size, array=True),
                           fbm(rng, 3, 2, size=size, array=True), 12)
    common_raw = growth_field(ctx.shared_rng)
    common_growth = normalize_like(common_raw, common_raw, 0.02, 0.98)
    growth = normalize_like(blend_fields(common_raw, growth_field(ctx.rng), interior), common_raw, 0.02, 0.98)
    patch = smoothstep(0.68, 0.9, growth)

    # Per-sett attributes: tone, warm/cool cast, tilt as the sett has settled,
    # whether it is lost or cracked, and the crack's heading, segments and kink. Setts reaching the
    # shared band use the shared stream, so every layout agrees at the block edge.
    def setts(rng, field):
        out = []
        for cx, cy in centres:
            g = smoothstep(0.68, 0.9, float(field[int(cy) % size * size + int(cx) % size]))
            out.append((rng.random(), rng.random() * 2 - 1,
                        (rng.uniform(-1, 1), rng.uniform(-1, 1)),
                        rng.random() < 0.06 + 0.4 * g,
                        rng.random() < 0.1, rng.random() * TAU,
                        [rng.uniform(0.3, 0.5) for _ in range(rng.randint(2, 3))], rng.uniform(-1, 1)))
        return out
    common, own = setts(ctx.shared_rng, common_growth), setts(ctx.rng, growth)
    reach = 32 + radius + NATIVE_PX
    stones = [c if min(x, y, size - x, size - y) < reach else o for (x, y), c, o in zip(centres, common, own)]
    centre_x = np.array([c[0] for c in centres])[ids]
    centre_y = np.array([c[1] for c in centres])[ids]
    xs = (np.arange(size * size) % size) + 0.5
    ys = (np.arange(size * size) // size) + 0.5

    def per_sett(setts_of, value):
        return np.array([value(s) for s in setts_of], dtype=np.float64)[ids]

    # Slightly chipped rims, the joint between setts, moss creeping 1-2 native
    # px over the stone in reclaimed patches, and each sett's settled height.
    # The shared stream's own surface is the common reference that keeps the
    # bevel's lighting scale, and so the block perimeter, identical in every layout.
    def surface(chip, setts_of, patch_of):
        face = edge - 1.2 * (chip - 0.5) * 2
        lost = per_sett(setts_of, lambda s: 1.0 if s[3] else 0.0)
        joint = 1 - smoothstep(0.5, 2.0, face)
        low = patch_of * 2 * NATIVE_PX
        creep = np.where(patch_of > 0.05, 1 - smoothstep(low, low + 1.5, face), 0.0)
        ox = (xs - centre_x + size / 2) % size - size / 2
        oy = (ys - centre_y + size / 2) % size - size / 2
        tx, ty = per_sett(setts_of, lambda s: s[2][0]), per_sett(setts_of, lambda s: s[2][1])
        h = smoothstep(-0.5, 2.0 * NATIVE_PX, face) * (1 + 0.012 * (tx * ox + ty * oy))
        return joint, np.maximum(lost, creep), np.where(lost != 0, 0.0, h)

    common_chip = fbm(ctx.shared_rng, 48, 2, size=size, array=True)
    chip = blend_fields(common_chip, fbm(ctx.rng, 48, 2, size=size, array=True), interior)
    joint, covered, height = surface(chip, stones, patch)
    common_joint, common_covered, common_height = surface(
        common_chip, common, smoothstep(0.68, 0.9, common_growth))

    # Stone colour, bevel and settling tilt.
    texture = layout_field(ctx, lambda rng: fbm(rng, 10, 3, size=size, array=True))
    tone = np.clip(0.5 * texture + 0.5 * per_sett(stones, lambda s: s[0]), 0.0, 1.0)
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    cast_w = per_sett(stones, lambda s: s[1])
    rgb = mix(rgb, p["warm"], np.maximum(0.0, cast_w), 0.35)
    rgb = mix(rgb, p["cool"], np.maximum(0.0, -cast_w), 0.35)
    rgb = shade(rgb, height, 0.3, reference_height=common_height)
    # A jagged crack running in from the rim of some setts, with a kink or two.
    cracks = np.zeros(size * size)
    for (cx, cy), s in zip(centres, stones):
        if s[4] and not s[3]:
            angle, x, y = s[5], cx - math.cos(s[5]) * 0.85 * radius, cy - math.sin(s[5]) * 0.85 * radius
            for length in s[6]:
                angle += s[7] * 0.6
                stamp_stroke(cracks, x, y, angle, length * radius, 0.7 * NATIVE_PX)
                x, y = x + math.cos(angle) * length * radius, y + math.sin(angle) * length * radius
    rgb = mix(rgb, p["crack"], cracks, 0.75)
    # Lichen blotches on the stone, heavier in reclaimed patches.
    lichen_patch = layout_field(ctx, lambda rng: fbm(rng, 14, 3, size=size, array=True))
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 72, size=size, array=True))
    lichen = smoothstep(0.6 - 0.15 * growth, 0.8 - 0.15 * growth, lichen_patch) * smoothstep(0.35, 0.65, speck)
    rgb = mix(rgb, p["lichen"], lichen, 0.4)

    # Joints: dark soil, with moss along a third to half of their length.
    joint_moss = layout_field(ctx, lambda rng: fbm(rng, 20, 2, size=size, array=True))
    joint_rgb = mix(flat(p["joint"], size, array=True), p["moss_dark"], smoothstep(0.45, 0.6, joint_moss), 0.8)
    rgb = mix(rgb, joint_rgb, joint)
    # Lost setts and creeping moss: soil under olive moss.
    moss_tex = layout_field(ctx, lambda rng: fbm(rng, 40, 3, size=size, array=True))
    veg = ramp(np.clip(0.7 * moss_tex + 0.3 * growth, 0.0, 1.0),
               [(0.0, p["soil"]), (0.35, p["moss_dark"]), (0.7, p["moss"]), (1.0, p["moss_light"])])

    def clumps(rng):
        mask = np.zeros(size * size)
        for _ in range(int(260 * ctx.area_scale / 64)):
            x, y = rng.random() * size, rng.random() * size
            for _ in range(rng.randint(3, 6)):
                a, d = rng.random() * TAU, rng.uniform(0, 2.0 * NATIVE_PX)
                stamp_disc(mask, x + d * math.cos(a), y + d * math.sin(a), rng.uniform(1.0, 1.6) * NATIVE_PX,
                           softness=0.5)
        return mask
    # Weeds grow from lost setts and the joints of reclaimed patches.
    def weeds(clump, covered_of, joint_of, growth_of):
        return clump * np.maximum(covered_of, joint_of * smoothstep(0.4, 0.8, growth_of))
    common_clumps = clumps(ctx.shared_rng)
    leaves = weeds(blend_fields(common_clumps, clumps(ctx.rng), interior), covered, joint, growth)
    common_leaves = weeds(common_clumps, common_covered, common_joint, common_growth)
    veg = mix(veg, p["leaf"], leaves, 0.8)
    rgb = mix(rgb, veg, covered)
    rgb = mix(rgb, p["leaf"], leaves, 0.8)
    rgb = shade(rgb, leaves, 0.2, reference_height=common_leaves)
    # Raised setts and weeds shadow the ground beside them; the Paths mark's
    # worn lip runs beside every joint.
    rgb = cast(rgb, height + 0.6 * leaves, p["shadow"], reach=NATIVE_PX, strength=0.4)
    rgb = worn_edge(rgb, joint, light=(NATIVE_PX, NATIVE_PX), strength=0.18)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size, array=True))
    return rgb * (1 + 0.08 * (grit - 0.5) * 2), None
