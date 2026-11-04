# SPDX-License-Identifier: GPL-3.0-or-later
"""The scree terrain material."""
import numpy as np

from terrain_core import (
    RECIPES,
    Style,
    as_list,
    block_worley,
    cast,
    jag,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shade,
)


def wrap_blur(a, r):
    """Wrapping box blur of a square numpy field of any size (`box_blur` is
    fixed at N), from running sums along each axis."""
    if r <= 0:
        return a
    k = 2 * r + 1
    out = a.copy()
    for axis in (0, 1):
        padded = np.concatenate([np.take(out, range(-r - 1, 0), axis=axis), out,
                                 np.take(out, range(0, r), axis=axis)], axis=axis)
        sums = np.cumsum(padded, axis=axis)
        n = out.shape[axis]
        out = (np.take(sums, range(k, k + n), axis=axis) - np.take(sums, range(0, n), axis=axis)) / k
    return out


def rock_layer(ctx, cells, metric, stretch, base, slope, cap, gap, keep, cap_spread=0.5):
    """One size class of angular rock fragments, in render-pixel heights.

    Each Worley cell, inset by its own gap, is a faceted roof: height grows
    linearly with F2-F1 toward the cell's middle, so every face toward an
    edge is planar and takes one light value, and is clipped at the cell's
    own flat top (`cap`). A cell is present with probability `keep`; absent
    cells leave the ground showing. Per-cell values are drawn by id from the
    shared stream, as in `plates`, so boundary fragments match every layout.

    Returns (height, common height, ids, common ids, mask, common mask) as
    size x size arrays; height is -1e3 where no fragment stands.
    """
    size = ctx.size
    f1, f2, ids, c1, c2, cids = block_worley(ctx, cells, metric, stretch)
    count = cells * ctx.area_scale
    r = ctx.shared_rng
    gaps = np.array([gap * (0.6 + 0.8 * r.random()) for _ in range(count)])
    caps = np.array([cap * (1 - cap_spread + 2 * cap_spread * r.random()) for _ in range(count)])
    lift = np.array([r.random() for _ in range(count)])
    present = np.array([r.random() < keep for _ in range(count)])
    cell_px = size / np.sqrt(count)

    def build(a, b, i):
        a, b, i = np.asarray(a), np.asarray(b), np.asarray(i)
        inside = (b - a - gaps[i]) * cell_px  # render px inside the fragment's edge
        roof = np.minimum(np.maximum(inside, 0) * slope, caps[i])
        h = base * (0.7 + 0.6 * lift[i]) + roof
        m = (inside > 0) & present[i]
        return np.where(m, h, -1e3).reshape(size, size), m.reshape(size, size), i.reshape(size, size)

    h, m, i = build(f1, f2, ids)
    ch, cm, ci = build(c1, c2, cids)
    return h, ch, i, ci, m, cm


def scree_fields(ctx, layers):
    """Heap the size classes in a z-buffer: at each pixel the highest fragment
    wins, so larger fragments rest on and lap over smaller ones.

    Returns (height, common height, layer index, fragment id); the layer is
    -1 on open ground. Painting layers in order instead tessellates them
    into crazy paving; the z-buffer lets a cobble lap over a block's low rim
    without piercing its middle, because each block's lowest top clears the
    highest cobble (see SCREE_LAYERS).
    """
    size = ctx.size
    top, common_top = np.zeros((size, size)), np.zeros((size, size))
    layer = np.full((size, size), -1)
    fragment = np.zeros((size, size), dtype=np.int64)
    for k, spec in enumerate(layers):
        h, ch, ids, _, m, cm = rock_layer(ctx, **spec)
        over = m & (h > top)
        top = np.where(over, h, top)
        layer = np.where(over, k, layer)
        fragment = np.where(over, ids, fragment)
        common_top = np.where(cm & (ch > common_top), ch, common_top)
    return top, common_top, layer, fragment


# Talus size classes, smallest first: chips between the stones, cobbles about
# half a tile across, and a few dominant blocks a tile or more across. A
# block's lowest top (base 10 x 0.7 + cap 18 x 0.7) clears the highest cobble
# (base 5 x 1.3 + cap 8 x 1.5), so cobbles never pierce a block.
SCREE_LAYERS = [
    dict(cells=24, metric="euclid", stretch=1.0, base=2.0, slope=0.4, cap=4.0, gap=0.10, keep=0.85, cap_spread=0.6),
    dict(cells=6, metric="euclid", stretch=1.0, base=5.0, slope=0.3, cap=8.0, gap=0.07, keep=0.8, cap_spread=0.5),
    dict(cells=1, metric="angular", stretch=1.5, base=10.0, slope=0.25, cap=18.0, gap=0.05, keep=0.55, cap_spread=0.3),
]


# Per-fragment tone and iron stain, looked up by (layer, id) in a fixed table
# drawn from the shared stream, so a boundary fragment keeps its tone.
SCREE_TONES = 4096


@recipe(name="scree", group="rough", label="Scree", profile="rock", border_class="solid", seam={"height": 1},
        palette={"void": (30, 28, 27), "ground": (70, 64, 58), "dark": (82, 80, 77),
                 "mid": (112, 108, 102), "light": (142, 138, 130), "warm": (128, 110, 92)},
        style=Style(luma=86, std=20, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4)
def render_scree(ctx):
    """Talus: angular, faceted fragments from chips to blocks a tile or more
    across, heaped so they rest on and lap over each other, with occluded
    voids between and shadows cast down-right: the rough group's heavy
    relief as broken rock."""
    p, size = RECIPES["scree"].palette, ctx.size
    top, common_top, layer, fragment = scree_fields(ctx, SCREE_LAYERS)
    rock = layer >= 0
    tones = np.array([ctx.shared_rng.random() for _ in range(SCREE_TONES)])
    stained = np.array([ctx.shared_rng.random() < 0.08 for _ in range(SCREE_TONES)], dtype=float)
    key = (fragment * 7 + layer * 1031 + 3) % SCREE_TONES
    tone = np.where(rock, 0.3 + 0.55 * tones[key], 0.0)
    rgb = ramp(as_list(tone), [(0.0, p["ground"]), (0.3, p["dark"]), (0.58, p["mid"]), (0.85, p["light"])])
    rgb = mix(rgb, p["warm"], as_list(stained[key] * rock), 0.3)
    # Coarse grit on the open ground between stones, faint texture on faces.
    grit = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))).reshape(size, size)
    fine = as_list(np.where(rock, 0.06, 0.22) * (grit - 0.5) * 2)
    rgb = [[c * (1 + f) for c, f in zip(channel, fine)] for channel in rgb]
    # A gentle bend only, so fracture edges stay straight and angular.
    (*rgb, height), (common,) = jag(ctx, [*rgb, as_list(top)], [as_list(common_top)], 3, cells=6)
    # Planar roofs give each facet one tone, with crisp creases between.
    rgb = shade(rgb, height, 0.85, reference_height=common)
    # Voids: darken where neighbouring fragments stand above this pixel.
    h = np.array(height).reshape(size, size)
    occlusion = np.clip((wrap_blur(h, 6) - h) / 6.0, 0, 1)
    rgb = mix(rgb, p["void"], as_list(occlusion), 0.9)
    return cast(rgb, height, p["void"], reach=5, strength=0.55), None
