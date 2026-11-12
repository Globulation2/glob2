# SPDX-License-Identifier: GPL-3.0-or-later
"""Wear shared by the laid paths: trail, brick path, sandstone tiles and boardwalk.

Each path keeps its own lattice and colour. This module makes a laid surface
look lived on rather than freshly laid: every stone (or brick, flag, plank)
settles with its own tilt and sinks a little, rims chip, a few stones crack
or are lost to the soil, the joints fill with grit, soil and moss, and in
reclaimed patches the ground creeps back over the surface. Every field keeps
the block contract: the shared stream decides the block edge, each layout's
own stream the interior, and lighting uses the shared surface as reference.

Fields are flat NumPy arrays (see terrain_core); stone ids index per-stone
attribute arrays, so no step walks the block pixel by pixel.
"""
import math
from dataclasses import dataclass

import numpy as np

from terrain_core import (
    record_stones,
    NATIVE_PX,
    TAU,
    as_field,
    blend_fields,
    cast,
    clamp01,
    desaturate,
    domain_warp,
    fbm,
    flat,
    hypot,
    lattice_noise,
    layout_field,
    mix,
    normalize_like,
    ramp,
    shade,
    smoothstep,
    stamp_disc,
    stamp_stroke,
    worn_edge,
)


# Ground that reclaims a path, common to every path: dark soil in the joints,
# paler grit, olive moss and leaf-green weeds, pale lichen, and the dust that
# dulls the stone toward the earth.
GROUND_PALETTE = {
    "joint": (54, 42, 32), "grit": (124, 108, 86), "soil": (62, 52, 40),
    "moss_dark": (56, 72, 36), "moss": (84, 112, 52), "moss_light": (118, 140, 66),
    "leaf": (96, 134, 56), "lichen": (156, 150, 96), "shadow": (34, 32, 26),
    "crack": (64, 58, 50), "dust": (138, 120, 94), "damp": (52, 50, 44), "rubble": (136, 128, 112),
    "stain": (142, 102, 62),
}


@dataclass
class Decay:
    """Broad fields that decide where a path is reclaimed, where the ground
    under it has subsided, and the chip noise."""
    growth: np.ndarray
    common_growth: np.ndarray
    patch: np.ndarray
    common_patch: np.ndarray
    chip: np.ndarray
    common_chip: np.ndarray
    subside: np.ndarray
    common_subside: np.ndarray
    wear: np.ndarray
    common_wear: np.ndarray
    damp: np.ndarray
    common_damp: np.ndarray


@dataclass
class Stone:
    """One stone's settled state: its tone, warm or cool cast, tilt, how far it
    has sunk, whether it is lost or cracked, and the crack's heading, segments
    and kink."""
    tone: float
    cast: float
    hue: float
    stain: float
    tilt: tuple
    sink: float
    bevel: float
    lost: bool
    cracked: bool
    angle: float
    segments: list
    kink: float


def uneven_rows(size, rng, heights=(6, 7, 8, 9, 10), offset=0):
    """Row heights in native px wrapping the block exactly: courses of setts
    or runs of planks laid by hand, not on a ruler. `offset` is where the
    first row starts (render px); the last row wraps back to it."""
    rows, total = [], 0
    cell = 32 * NATIVE_PX
    while True:
        h = rng.choice(heights) * NATIVE_PX
        if total + h > size - min(heights) * NATIVE_PX:
            rows.append(size - total)
            break
        # A row boundary on a cell cut would look like a tile seam: nudge it.
        if (offset + total + h) % cell == 0:
            h += NATIVE_PX
        rows.append(h)
        total += h
    return rows


def stone_field(ctx, centres, reach, make):
    """One value per stone from a broad field: the shared stream's field for
    stones within `reach` of the block edge, each layout's blend inside."""
    size = ctx.size
    common = as_field(make(ctx.shared_rng))
    blended = blend_fields(common, as_field(make(ctx.rng)), ctx.interior_mask)
    out = []
    for cx, cy in centres:
        at = int(cy) % size * size + int(cx) % size
        out.append(float(common[at] if min(cx, cy, size - cx, size - cy) < reach else blended[at]))
    return out


def rectangle_geometry(size, rects, gap=NATIVE_PX):
    """Rasterise stones given as (x0, y0, width, height, corner radius) in
    render px, with `gap` of joint on each stone's right and bottom, wrapping
    the block. Returns the centres and, per pixel, the stone index, the
    distance from the stone's face (negative in the joint), the distance to
    the nearer end along x and the position across the stone (0..1)."""
    ids = np.full((size, size), -1, dtype=np.int64)
    edge = np.zeros((size, size))
    axial = np.zeros((size, size))
    across = np.zeros((size, size))
    for k, (x0, y0, w, h, r) in enumerate(rects):
        fw, fh = w - gap, h - gap
        py = math.floor(y0) + np.arange(int(math.ceil(h)) + 1)
        py = py[(py + 0.5 >= y0) & (py + 0.5 < y0 + h)]
        px = math.floor(x0) + np.arange(int(math.ceil(w)) + 1)
        px = px[(px + 0.5 >= x0) & (px + 0.5 < x0 + w)]
        dx = np.minimum(px + 0.5 - x0, x0 + fw - px - 0.5)[None, :]
        dy = np.minimum(py + 0.5 - y0, y0 + fh - py - 0.5)[:, None]
        dx, dy = np.broadcast_arrays(dx, dy)
        corner = (0 < dx) & (dx < r) & (0 < dy) & (dy < r)
        e = np.minimum(dx, dy)
        if corner.any():
            e[corner] = r - hypot(r - dx[corner], r - dy[corner])
        rows, cols = (py % size)[:, None], (px % size)[None, :]
        ids[rows, cols] = k
        edge[rows, cols] = e
        axial[rows, cols] = dx
        across[rows, cols] = np.broadcast_to(((py + 0.5 - y0) / fh)[:, None], e.shape)
    assert ids.min() >= 0
    centres = [((x0 + w / 2) % size, (y0 + h / 2) % size) for x0, y0, w, h, _ in rects]
    return centres, ids.ravel(), edge.ravel(), axial.ravel(), across.ravel()


def decay_fields(ctx, reclaim=(0.68, 0.9)):
    """The reclaimed patches and the rim chipping, shared at the block edge."""
    size = ctx.size

    def noise(rng, cells, octaves):
        return fbm(rng, cells, octaves, size=size, array=True)

    def growth_field(rng):
        return domain_warp(noise(rng, 3, 3), noise(rng, 3, 2), noise(rng, 3, 2), 12)
    common_raw = growth_field(ctx.shared_rng)
    common_growth = normalize_like(common_raw, common_raw, 0.02, 0.98)
    growth = normalize_like(blend_fields(common_raw, growth_field(ctx.rng), ctx.interior_mask), common_raw, 0.02, 0.98)
    common_chip = noise(ctx.shared_rng, 48, 2)
    chip = blend_fields(common_chip, noise(ctx.rng, 48, 2), ctx.interior_mask)
    # Subsidence: the ground gives way in hollows a couple of cells across, so
    # neighbouring stones sink together and steps appear at the hollows' rims.
    common_subside = noise(ctx.shared_rng, 6, 2)
    subside = blend_fields(common_subside, noise(ctx.rng, 6, 2), ctx.interior_mask)
    # Wear is not even along a path: some stretches are broken up and trodden
    # to dust, others hardly touched; damp stretches go dark, mossy and lichened.
    def wear_field(rng):
        return domain_warp(noise(rng, 2, 3), noise(rng, 3, 2), noise(rng, 3, 2), 16)
    common_wear_raw = wear_field(ctx.shared_rng)
    common_wear = normalize_like(common_wear_raw, common_wear_raw, 0.02, 0.98)
    wear = normalize_like(blend_fields(common_wear_raw, wear_field(ctx.rng), ctx.interior_mask), common_wear_raw, 0.02, 0.98)
    common_damp_raw = noise(ctx.shared_rng, 4, 3)
    common_damp = normalize_like(common_damp_raw, common_damp_raw, 0.02, 0.98)
    damp = normalize_like(blend_fields(common_damp_raw, noise(ctx.rng, 4, 3), ctx.interior_mask), common_damp_raw, 0.02, 0.98)
    return Decay(growth, common_growth, smoothstep(*reclaim, growth), smoothstep(*reclaim, common_growth),
                 chip, common_chip, subside, common_subside, wear, common_wear, damp, common_damp)


def stone_fates(ctx, decay, centres, reach, *, lost=0.04, cracked=0.08, sink=0.3, heave=0.0):
    """Per-stone settled state. Stones within `reach` render px of the block
    edge take the shared stream, so every layout agrees at the perimeter.

    `lost` is the share of stones lost to the soil where the path is intact;
    reclaimed patches lose up to ten times as many. `cracked` is the share
    with a crack, `sink` the deepest settling as a fraction of the bevel and
    `heave` the share of stones pushed up instead, by up to half as much.
    Half of each stone's settling follows the subsidence field, so hollows
    sink whole groups of stones together."""
    size = ctx.size
    at = [int(cy) % size * size + int(cx) % size for cx, cy in centres]

    def settle(rng, patch, subside, wear):
        patch, subside, wear = (as_field(f)[at].tolist() for f in (patch, subside, wear))
        out = []
        for g, sub, wr in zip(patch, subside, wear):
            w = 0.4 + 1.6 * wr
            depth = (0.5 * rng.random() + 0.5 * smoothstep(0.35, 0.75, sub)) * sink * (0.5 + wr)
            if rng.random() < heave:
                depth = -0.5 * depth
            out.append(Stone(rng.random(), rng.random() * 2 - 1, rng.random() * 2 - 1, rng.random(),
                             (rng.uniform(-1, 1), rng.uniform(-1, 1)),
                             depth, rng.uniform(0.4, 1.2),
                             rng.random() < lost * (1 + 9 * g) * w,
                             rng.random() < cracked * w, rng.random() * TAU,
                             [rng.uniform(0.3, 0.5) for _ in range(rng.randint(2, 3))], rng.uniform(-1, 1)))
        return out
    common = settle(ctx.shared_rng, decay.common_patch, decay.common_subside, decay.common_wear)
    own = settle(ctx.rng, decay.patch, decay.subside, decay.wear)
    stones = [c if min(x, y, size - x, size - y) < reach else o for (x, y), c, o in zip(centres, common, own)]
    return stones, common


def per_stone(stones, value):
    """`value(stone)` for every stone, as an array indexed by stone id."""
    return np.array([value(s) for s in stones], dtype=np.float64)


def surface(ctx, ids, edge, centres, stones, chip, patch, wear, *, joint=(0.5, 2.0), creep=2.0, tilt=0.012,
            chipping=1.2, bevel=2.0, broken=None):
    """Chipped rims, the joint between stones, moss creeping `creep` native px
    over the stone in reclaimed patches, and each stone's settled height.

    `edge` is the distance from each pixel to its stone's face (negative in
    the joint). Returns the joint mask, the covered mask (lost stones and the
    creep) and the height field."""
    size = ctx.size
    ids, edge, chip, patch, wear = np.asarray(ids), as_field(edge), as_field(chip), as_field(patch), as_field(wear)
    face = edge - chipping * (0.5 + wear) * (chip - 0.5) * 2
    # A lost stone is gone entirely, or only where `broken` marks it (a
    # plank's rotten end).
    gone = per_stone(stones, lambda s: s.lost)[ids] != 0
    if broken is not None:
        gone &= as_field(broken) > 0.5
    lost = np.where(gone, 1.0, 0.0)
    joint_mask = 1 - smoothstep(joint[0], joint[1], face)
    start = patch * creep * NATIVE_PX
    covered = np.maximum(lost, np.where(patch > 0.05, 1 - smoothstep(start, start + 1.5, face), 0.0))
    i = np.arange(size * size)
    x, y = (i % size) + 0.5, (i // size) + 0.5
    cx = np.array([c[0] for c in centres])[ids]
    cy = np.array([c[1] for c in centres])[ids]
    ox = (x - cx + size / 2) % size - size / 2
    oy = (y - cy + size / 2) % size - size / 2
    # Each stone's own bevel width, so no two rims catch the light alike.
    rim = per_stone(stones, lambda s: s.bevel * bevel * NATIVE_PX)[ids]
    tx = per_stone(stones, lambda s: s.tilt[0])[ids]
    ty = per_stone(stones, lambda s: s.tilt[1])[ids]
    sink = per_stone(stones, lambda s: s.sink)[ids]
    h = smoothstep(-0.5, rim, face) * (1 + tilt * (tx * ox + ty * oy)) * (1 - sink)
    return joint_mask, covered, np.where(gone, 0.0, h)


def crack_mask(ctx, centres, stones, radius, *, along=None, width=0.7):
    """A jagged crack running in from the rim of each cracked stone, with a
    kink or two. `along` fixes the heading, one angle for all or one per
    stone (planks split along their grain, bricks across it); otherwise each
    stone's own heading is used."""
    cracks = np.zeros(ctx.size * ctx.size)
    for k, ((cx, cy), s) in enumerate(zip(centres, stones)):
        if not s.cracked or s.lost:
            continue
        heading = along[k] if isinstance(along, list) else along
        angle = s.angle if heading is None else heading + (s.kink * 0.15)
        x, y = cx - math.cos(angle) * 0.85 * radius, cy - math.sin(angle) * 0.85 * radius
        for length in s.segments:
            angle += s.kink * (0.6 if heading is None else 0.25)
            stamp_stroke(cracks, x, y, angle, length * radius, width * NATIVE_PX)
            x, y = x + math.cos(angle) * length * radius, y + math.sin(angle) * length * radius
    return cracks


def clumps(ctx, rng, density=260):
    """Weed clumps: a few soft leaves around a point, `density` per 8x8 block."""
    size = ctx.size
    mask = np.zeros(size * size)
    for _ in range(int(density * ctx.area_scale / 64)):
        x, y = rng.random() * size, rng.random() * size
        for _ in range(rng.randint(3, 6)):
            a, d = rng.random() * TAU, rng.uniform(0, 2.0 * NATIVE_PX)
            stamp_disc(mask, x + d * math.cos(a), y + d * math.sin(a), rng.uniform(1.0, 1.6) * NATIVE_PX, softness=0.5)
    return mask


def dress(ctx, rgb, p, decay, ids, edge, centres, stones, common_stones, radius, *,
          joint=(0.5, 2.0), creep=2.0, tilt=0.012, chipping=1.2, bevel=2.0, bevel_strength=0.3,
          crack_along=None, crack_width=0.7, moss=1.0, weeds=1.0, lichen=1.0, grit=0.5, grit_cover=0.5,
          dust=0.25, lip=0.18, shadow=0.4, finish_grain=0.07, broken=None, damp=1.0):
    """Wear a laid surface: settle and bevel the stones, crack some, fill the
    joints with grit, soil and moss, lose stones to soil and weeds, let moss
    creep in reclaimed patches, cast the raised stones' shadows, draw the
    Paths group's worn lip and dull everything with dust.

    `rgb` is the stones' own colour. The knobs scale each kind of wear."""
    record_stones(ctx, ids, edge, centres)
    size = ctx.size
    p = {**GROUND_PALETTE, **p}
    ids = np.asarray(ids)

    def noise(make):
        return layout_field(ctx, lambda rng: make(rng))

    joint_mask, covered, height = surface(ctx, ids, edge, centres, stones, decay.chip, decay.patch, decay.wear,
                                          joint=joint, creep=creep, tilt=tilt, chipping=chipping, bevel=bevel,
                                          broken=broken)
    common_joint, common_covered, common_height = surface(ctx, ids, edge, centres, common_stones, decay.common_chip,
                                                          decay.common_patch, decay.common_wear, joint=joint,
                                                          creep=creep, tilt=tilt, chipping=chipping, bevel=bevel,
                                                          broken=broken)
    # Damp stretches: the stone goes darker and richer where water lingers.
    wet = np.maximum(decay.growth, 0.85 * decay.damp)
    damp_mask = smoothstep(0.6, 0.9, decay.damp) * damp
    rgb = np.asarray(rgb, dtype=np.float64) * (1 - 0.22 * damp_mask)
    rgb = mix(rgb, p["damp"], damp_mask, 0.25)
    rgb = shade(rgb, height, bevel_strength, reference_height=common_height)
    # Sunken stones sit in shadow at their rims and collect dirt.
    sunk = np.maximum(0.0, per_stone(stones, lambda s: s.sink))[ids]
    rgb = mix(rgb, p["dust"], sunk * 0.6, 0.5)
    cracks = crack_mask(ctx, centres, stones, radius, along=crack_along, width=crack_width)
    rgb = mix(rgb, p["crack"], cracks, 0.75)
    # Lichen blotches, heavier in reclaimed patches.
    lichen_patch = noise(lambda rng: fbm(rng, 14, 3, size=size, array=True))
    speck = noise(lambda rng: lattice_noise(rng, 72, size=size, array=True))
    lichen_mask = smoothstep(0.62 - 0.2 * wet, 0.82 - 0.2 * wet, lichen_patch) * smoothstep(0.35, 0.65, speck)
    rgb = mix(rgb, p["lichen"], lichen_mask, 0.4 * lichen)
    # Joints: dark soil, pale grit where the path is walked, moss where it is not.
    joint_grit = noise(lambda rng: fbm(rng, 12, 2, size=size, array=True))
    joint_moss = noise(lambda rng: fbm(rng, 20, 2, size=size, array=True))
    joint_rgb = mix(flat(p["joint"], size, array=True), p["grit"],
                    smoothstep(grit_cover, grit_cover + 0.2, joint_grit) * (1 - decay.growth), grit)
    joint_rgb = mix(joint_rgb, p["moss_dark"], smoothstep(0.45, 0.6, joint_moss) * smoothstep(0.35, 0.8, wet), 0.8 * moss)
    rgb = mix(rgb, joint_rgb, joint_mask)
    # Lost stones and creeping moss: soil under olive moss.
    moss_tex = noise(lambda rng: fbm(rng, 40, 3, size=size, array=True))
    veg = ramp(clamp01(0.7 * moss_tex + 0.3 * decay.growth),
               [(0.0, p["soil"]), (0.35, p["moss_dark"]), (0.7, p["moss"]), (1.0, p["moss_light"])])
    if moss < 1.0:
        veg = mix(veg, p["soil"], np.full(size * size, 1 - moss), 1.0)
    # Dry pockets are bare: soil and grit with a few broken fragments of the stone.
    dry = 1 - smoothstep(0.3, 0.6, wet)
    bare = ramp(moss_tex, [(0.0, p["soil"]), (0.6, p["soil"]), (1.0, p["grit"])])
    rubble_c = clumps(ctx, ctx.shared_rng, density=160)
    rubble = blend_fields(rubble_c, clumps(ctx, ctx.rng, density=160), ctx.interior_mask)
    bare = mix(bare, p["rubble"], rubble * 0.8, 1.0)
    veg = mix(veg, bare, dry)

    def grow(clump, covered_of, joint_of, growth_of):
        return clump * np.maximum(covered_of, joint_of * smoothstep(0.4, 0.8, growth_of)) * weeds
    common_clumps = clumps(ctx, ctx.shared_rng)
    leaves = grow(blend_fields(common_clumps, clumps(ctx, ctx.rng), ctx.interior_mask), covered, joint_mask, decay.growth)
    common_leaves = grow(common_clumps, common_covered, common_joint, decay.common_growth)
    veg = mix(veg, p["leaf"], leaves, 0.8)
    rgb = mix(rgb, veg, covered)
    rgb = mix(rgb, p["leaf"], leaves, 0.8)
    rgb = shade(rgb, leaves, 0.2, reference_height=common_leaves)
    # Raised stones and weeds shadow the ground beside them; the Paths mark's
    # worn lip runs beside every joint.
    rgb = cast(rgb, height + 0.6 * leaves, p["shadow"], reach=NATIVE_PX, strength=shadow)
    rgb = worn_edge(rgb, joint_mask, light=(NATIVE_PX, NATIVE_PX), strength=lip)
    # Dust: a broad wash toward the earth that dulls the stone unevenly.
    if dust:
        wash = noise(lambda rng: fbm(rng, 5, 2, size=size, array=True))
        trodden = decay.wear * (1 - decay.damp)
        rgb = mix(rgb, p["dust"], dust * (0.3 + 0.5 * wash + 0.8 * trodden) * (1 - covered), 1.0)
        rgb = desaturate(rgb, dust * 0.3)
    if finish_grain:
        fine = noise(lambda rng: lattice_noise(rng, 48, size=size, array=True))
        rgb = rgb * (1 + finish_grain * (fine - 0.5) * 2)
    return rgb


def stone_colour(rgb_of_tone, ids, stones, texture, p, *, warm="warm", cool="cool", stain="stain", strength=0.35,
                 own=0.5, staining=0.5):
    """Each stone's own tone (weight `own`) over the texture, through the
    ramp, with its own warm or cool hue drift, and a mineral stain on a few."""
    ids = np.asarray(ids)
    tone = clamp01((1 - own) * as_field(texture) + own * per_stone(stones, lambda s: s.tone)[ids])
    rgb = rgb_of_tone(tone)
    hue = per_stone(stones, lambda s: s.hue)[ids]
    if warm in p:
        rgb = mix(rgb, p[warm], np.maximum(0.0, hue), strength)
    if cool in p:
        rgb = mix(rgb, p[cool], np.maximum(0.0, -hue), strength)
    if stain in p and staining:
        rgb = mix(rgb, p[stain], smoothstep(0.8, 0.95, per_stone(stones, lambda s: s.stain)[ids]), staining)
    return rgb
