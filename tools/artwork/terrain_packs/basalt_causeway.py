# SPDX-License-Identifier: GPL-3.0-or-later
"""The basalt causeway terrain material.

Wear: the hard stone outlasts its bed. Traffic polishes the crowns smooth,
stones tip and sink unevenly, some shatter into angular fragments along
cooling joints, and wind-blown ash drifts across the causeway: first filling
the joints, then the hollows between tilted stones, until only polished
crowns poke through.
"""
import numpy as np
from scipy.spatial import cKDTree

from ._rock_paths import ASH_GROUND, grains, layout_pair, stamped
from ._paving import decay_fields, dress, per_stone, stone_colour, stone_fates, stone_field
from terrain_core import (N, NATIVE_PX, RECIPES, Style, clamp01, domain_warp, fbm, hypot, lattice_noise, layout_field,
                          mix, ramp, recipe, shade, smoothstep)

SPACING = 15 * NATIVE_PX  # about half a tile between stone centres
RELAX_STEPS = 1  # Lloyd steps: evens out stone sizes, as a mason would


def relax(pts, size, steps, sample=4):
    """Periodic Lloyd relaxation on a coarse sample grid: move every seed to
    its cell's centroid, so stones come out similar in size and shape while
    keeping irregular 5-7 sided outlines."""
    n = size // sample
    g = (np.arange(n) + 0.5) * sample
    gx, gy = np.meshgrid(g, g)
    q = np.stack([gx.ravel(), gy.ravel()], axis=1)
    for _ in range(steps):
        tiled = np.concatenate([pts + (dx * size, dy * size) for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
        owner = np.tile(np.arange(len(pts)), 9)
        _, idx = cKDTree(tiled).query(q)
        own = owner[idx]
        # Offsets from the seed, so cells that wrap the block stay whole.
        off = tiled[idx] - q
        count = np.bincount(own, minlength=len(pts))
        mean = np.stack([np.bincount(own, -off[:, 0], len(pts)), np.bincount(own, -off[:, 1], len(pts))], axis=1)
        pts = (pts + mean / np.maximum(count, 1)[:, None]) % size
    return pts


def polygons(size, rng, gap):
    """Periodic Voronoi of a jittered offset grid: irregular 5-7 sided stones.
    Returns centres, per-pixel ids and the exact distance to the stone's own
    polygon edge (minus half the joint)."""
    per = size // SPACING
    step = size / per
    pts = []
    for j in range(per):
        for i in range(per):
            x = (i + 0.5 * (j % 2) + rng.uniform(-0.34, 0.34)) * step
            y = (j + 0.5 + rng.uniform(-0.34, 0.34)) * step
            pts.append((x % size, y % size))
    pts = relax(np.array(pts), size, RELAX_STEPS)
    tiled = np.concatenate([pts + (dx * size, dy * size) for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    owner = np.tile(np.arange(len(pts)), 9)
    tree = cKDTree(tiled)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    q = np.stack([xs.ravel(), ys.ravel()], axis=1)
    dist, idx = tree.query(q, k=8)
    p0 = tiled[idx[:, 0]]
    d0 = dist[:, 0] ** 2
    edge = np.full(len(q), np.inf)
    for k in range(1, 8):
        pk = tiled[idx[:, k]]
        sep = np.linalg.norm(pk - p0, axis=1)
        e = (dist[:, k] ** 2 - d0) / (2 * np.maximum(sep, 1e-6))
        edge = np.minimum(edge, e)
    ids = owner[idx[:, 0]]
    return [tuple(c) for c in pts.tolist()], ids, edge - gap / 2, step


def fragments(size, rng, spacing):
    """A finer periodic Voronoi: the fracture lines a shattered stone breaks
    along. Returns per-pixel distance to the nearest fracture and fragment id."""
    per = int(size // spacing)
    step = size / per
    pts = np.array([((i + rng.uniform(0.1, 0.9)) * step, (j + rng.uniform(0.1, 0.9)) * step)
                    for j in range(per) for i in range(per)])
    tiled = np.concatenate([pts + (dx * size, dy * size) for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    owner = np.tile(np.arange(len(pts)), 9)
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float64) + 0.5
    q = np.stack([xs.ravel(), ys.ravel()], axis=1)
    dist, idx = cKDTree(tiled).query(q, k=2)
    sep = np.linalg.norm(tiled[idx[:, 1]] - tiled[idx[:, 0]], axis=1)
    edge = (dist[:, 1] ** 2 - dist[:, 0] ** 2) / (2 * np.maximum(sep, 1e-6))
    return edge, owner[idx[:, 0]]


@recipe(name="basalt_causeway", group="paths", label="Basalt causeway", profile="paved", border_class="built",
        seam={"height": 3, "cast_q8": 48, "cast_width_q8": 512},
        palette={**ASH_GROUND, "dark": (30, 32, 36), "mid": (52, 54, 58), "light": (80, 82, 86),
                 "warm": (66, 58, 54), "cool": (44, 52, 64), "sheen": (112, 116, 122),
                 "ash_dark": (64, 60, 57), "ash_light": (94, 89, 83), "cinder": (30, 28, 28)},
        style=Style(luma=60, std=16, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4, stone_edges=True)
def render_basalt_causeway(ctx):
    p, size = RECIPES["basalt_causeway"].palette, ctx.size
    centres, ids, edge, step = polygons(size, ctx.shared_rng, gap=1.2 * NATIVE_PX)
    decay = decay_fields(ctx, reclaim=(0.8, 0.95))
    reach = 32 + step
    fates, common = stone_fates(ctx, decay, centres, reach, lost=0.035, cracked=0.3, sink=0.6, heave=0.35)
    texture = layout_field(ctx, lambda rng: fbm(rng, 6, 3, size=size, array=True))
    rgb = stone_colour(lambda t: ramp(t, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])]),
                       ids, fates, texture, p, strength=0.4, own=0.55, staining=0.25)
    # Faint cooling rings around each column's heart.
    i = np.arange(size * size)
    x, y = i % size + 0.5, i // size + 0.5
    dx = (x - np.array([c[0] for c in centres])[ids] + size / 2) % size - size / 2
    dy = (y - np.array([c[1] for c in centres])[ids] + size / 2) % size - size / 2
    ring = 0.5 + 0.5 * np.cos(hypot(dx, dy) / (2.2 * NATIVE_PX) + 6 * per_stone(fates, lambda f: f.tone)[ids])
    rgb = mix(rgb, p["dark"], 0.06 * ring, 1.0)
    # Worn smooth by traffic: a soft polish on the crown of each stone.
    crown = smoothstep(1.5 * NATIVE_PX, 6 * NATIVE_PX, edge)
    rgb = mix(rgb, p["sheen"], crown * smoothstep(0.35, 0.85, decay.wear), 0.35)
    # Shattered stones: a few broke along a finer web of cooling joints into
    # angular fragments, each settled a little differently.
    frac_edge, frac_id = fragments(size, ctx.shared_rng, 7 * NATIVE_PX)
    shattered = per_stone(fates, lambda f: 1.0 if f.stain > 0.82 and not f.lost else 0.0)[ids]
    frag_tone = np.array([((f * 2654435761) % 1000) / 1000.0 for f in range(int(frac_id.max()) + 1)])
    rgb = mix(rgb, p["light"], shattered * 0.25 * frag_tone[frac_id], 1.0)
    fracture = (shattered * (1 - smoothstep(0.3 * NATIVE_PX, 0.9 * NATIVE_PX, frac_edge))
                * smoothstep(0, NATIVE_PX, edge))
    rgb = mix(rgb, p["crack"], fracture, 0.85)
    pores = layout_field(ctx, lambda rng: lattice_noise(rng, 36, size=size, array=True))
    rgb = mix(rgb, p["crack"], smoothstep(0.84, 0.9, pores), 0.5)
    rgb = dress(ctx, rgb, p, decay, ids, edge, centres, fates, common, step * 0.5,
                joint=(0.0, 2.2), creep=0.8, tilt=0.02, chipping=1.8, bevel=2.2, bevel_strength=0.4, crack_width=1.1,
                moss=0.0, weeds=0.1, lichen=0.3, grit=0.8, grit_cover=0.35, dust=0.1, lip=0.3, shadow=0.5)

    # Ash drifts: broad wind-shaped tongues that fill joints first, then the
    # stone rims, and bury all but the crowns where they are deepest.
    def drift(rng):
        f = fbm(rng, 2, 3, size=size, array=True)
        return domain_warp(f, fbm(rng, 3, 2, size=size, array=True), fbm(rng, 3, 2, size=size, array=True), 40)
    depth_c, depth = layout_pair(ctx, drift)

    def cover_of(depth_of, stones):
        # Sunken stones sit low and collect ash first.
        crown_of = (smoothstep(0.0, 11 * NATIVE_PX, edge) * per_stone(stones, lambda s: 0.0 if s.lost else 1.0)[ids]
                    * (1 - smoothstep(0.2, 0.5, per_stone(stones, lambda s: s.sink)))[ids])
        return smoothstep(crown_of * 0.9 - 0.1, crown_of * 0.9 + 0.25, smoothstep(0.72, 0.95, depth_of))
    # The drift's outline is broken up by the ash's own grain.
    grain_c, grain_b = layout_pair(ctx, lambda rng: fbm(rng, 20, 2, size=size, array=True))
    depth = depth + 0.15 * (grain_b - 0.5)
    depth_c = depth_c + 0.15 * (grain_c - 0.5)
    cover, cover_c = cover_of(depth, fates), cover_of(depth_c, common)
    ash_tex = layout_field(ctx, lambda rng: fbm(rng, 24, 4, size=size, array=True))
    ash_grain = layout_field(ctx, lambda rng: fbm(rng, 64, 2, size=size, array=True))
    ash = ramp(0.6 * ash_tex + 0.4 * ash_grain, [(0.0, p["ash_dark"]), (1.0, p["ash_light"])])
    # A thin veil of dust over the stones around each drift.
    veil = 0.2 * smoothstep(0.55, 0.85, depth) * (1 - cover)
    rgb = mix(rgb, p["ash_light"], veil, 1.0)
    specks = stamped(ctx, lambda rng: grains(ctx, rng, 1200, 0.3, 0.8))
    ash = mix(ash, p["cinder"], specks, 0.7)
    # The drift is lit as a soft mound of its own.
    rgb = mix(rgb, ash, cover)
    rgb = shade(rgb, 0.6 * cover, 0.18, reference_height=0.6 * cover_c)
    return rgb, None
