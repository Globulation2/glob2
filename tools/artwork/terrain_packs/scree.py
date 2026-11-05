# SPDX-License-Identifier: GPL-3.0-or-later
"""The scree terrain material."""
import numpy as np

from terrain_core import (
    RECIPES,
    Style,
    as_list,
    blend_fields,
    block_worley,
    cast,
    fbm,
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


def rock_layer(ctx, cells, metric, stretch, base, slope, cap, gap, keep, cap_spread=0.5, rounding=0.0):
    """One size class of angular rock fragments, in render-pixel heights.

    Each Worley cell, inset by its own gap, is a faceted roof: height grows
    linearly with F2-F1 toward the cell's middle, so every face toward an
    edge is planar and takes one light value, and is clipped at the cell's
    own flat top (`cap`). `rounding` bends that profile toward a dome, so a
    weathered stone's top is convex rather than a planar roof. A cell is
    present with probability `keep`; absent cells leave the ground showing. Per-cell values are drawn by id from the
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
        if rounding:
            rise = np.minimum(np.maximum(inside, 0) * slope / caps[i], 1.0)
            roof = caps[i] * ((1 - rounding) * rise + rounding * (1 - (1 - rise) ** 2))
        else:
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


# Talus size classes for the cliff ground (tools/artwork/terrain_packs/cliffs.py),
# smallest first: chips between the stones and cobbles about half a tile
# across. A block's lowest top (base 10 x 0.7 + cap 18 x 0.7) clears the
# highest cobble (base 5 x 1.3 + cap 8 x 1.5), so cobbles never pierce a block.
TALUS_LAYERS = [
    dict(cells=24, metric="euclid", stretch=1.0, base=2.0, slope=0.4, cap=4.0, gap=0.10, keep=0.85, cap_spread=0.6),
    dict(cells=6, metric="euclid", stretch=1.0, base=5.0, slope=0.3, cap=8.0, gap=0.07, keep=0.8, cap_spread=0.5),
    dict(cells=1, metric="angular", stretch=1.5, base=10.0, slope=0.25, cap=18.0, gap=0.05, keep=0.55, cap_spread=0.3),
]


# Scree size classes, smallest first: chips, cobbles about a quarter of a tile
# across, and slabs up to about half a tile, so the bed reads as loose rock
# rather than boulders. Gaps are wide enough that stones read as lying loose
# rather than fitted like paving.
# A slab's lowest top (9 x 0.7 + 8 x 0.7 = 11.9) clears the highest cobble
# (4 x 1.3 + 4.5 x 1.5 = 11.95, within a pixel), so cobbles do not pierce a
# slab; chips may lap onto cobble rims, as they do in a real talus.
SCREE_LAYERS = [
    dict(cells=40, metric="euclid", stretch=1.0, base=1.5, slope=0.5, cap=2.5, gap=0.12, keep=0.92, cap_spread=0.5, rounding=0.6),
    dict(cells=12, metric="euclid", stretch=1.0, base=4.0, slope=0.35, cap=4.5, gap=0.12, keep=0.75, cap_spread=0.5, rounding=0.6),
    dict(cells=4, metric="angular", stretch=1.25, base=9.0, slope=0.25, cap=8.0, gap=0.10, keep=0.55, cap_spread=0.3, rounding=0.7),
]


# Per-fragment tone and iron stain, looked up by (layer, id) in a fixed table
# drawn from the shared stream, so a boundary fragment keeps its tone.
SCREE_TONES = 4096


@recipe(name="scree", group="rough", label="Scree", profile="rock", border_class="solid", seam={"height": 1},
        palette={"void": (34, 31, 29), "fines": (104, 94, 82), "dark": (74, 72, 70),
                 "mid": (110, 108, 104), "light": (144, 140, 132), "warm": (136, 106, 80),
                 "lichen": (150, 148, 112)},
        style=Style(luma=90, std=16, grain_max=14, match=0.8), periodic=True, grid=8, blocks=4)
def render_scree(ctx):
    """Scree: a bed of loose, angular, weathered stone from chips to slabs
    about half a tile across, heaped so they rest on and lap over each other
    over ochre fines, softly lit with shaded gaps between. The bed itself
    rises and falls about a tile across, so lit heaps and shadowed hollows
    give the rough group's heavy relief, as broken rock. The stone is a cool
    neutral grey, a step lighter than the ochre talus under the cliffs."""
    p, size = RECIPES["scree"].palette, ctx.size
    top, common_top, layer, fragment = scree_fields(ctx, SCREE_LAYERS)
    rock = layer >= 0
    tones = np.array([ctx.shared_rng.random() for _ in range(SCREE_TONES)])
    stained = np.array([ctx.shared_rng.random() < 0.12 for _ in range(SCREE_TONES)], dtype=float)
    key = (fragment * 7 + layer * 1031 + 3) % SCREE_TONES
    # Each stone its own tone, mottled by weathering within the stone.
    mottle = np.array(layout_field(ctx, lambda rng: fbm(rng, 10, 3, size=size))).reshape(size, size)
    tone = np.where(rock, 0.2 + 0.65 * tones[key] + 0.3 * (mottle - 0.5), 0.0)
    rgb = ramp(as_list(tone), [(0.0, p["fines"]), (0.25, p["dark"]), (0.55, p["mid"]), (0.9, p["light"])])
    rgb = mix(rgb, p["warm"], as_list(stained[key] * rock), 0.3)
    # Fine grain everywhere, coarser on the open fines between stones.
    grit = np.array(layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))).reshape(size, size)
    fine = as_list(np.where(rock, 0.10, 0.18) * (grit - 0.5) * 2)
    rgb = [[c * (1 + f) for c, f in zip(channel, fine)] for channel in rgb]
    # The heap: a slow rise and fall about a tile across under the stones,
    # so the bed has lit heaps and shadowed hollows like the group's lumps.
    heap_common = np.array(fbm(ctx.shared_rng, 1.0, 2, size=size)).reshape(size, size)
    heap = np.array(blend_fields(as_list(heap_common), fbm(ctx.rng, 1.0, 2, size=size), ctx.interior_mask)).reshape(size, size)
    top = top + 9.0 * (heap - 0.5)
    common_top = common_top + 9.0 * (heap_common - 0.5)
    # A gentle bend only, so fracture edges stay straight and angular.
    (*rgb, height), (common,) = jag(ctx, [*rgb, as_list(top)], [as_list(common_top)], 3, cells=6)
    rgb = shade(rgb, height, 0.5, reference_height=common)
    # Lichen on the upper faces of a few stones.
    h = np.array(height).reshape(size, size)
    lichen = np.array(layout_field(ctx, lambda rng: fbm(rng, 6, 2, size=size))).reshape(size, size)
    lichen = np.clip((lichen - 0.68) / 0.12, 0, 1) * rock * np.clip((h - wrap_blur(h, 10)) / 4.0 + 0.5, 0, 1)
    rgb = mix(rgb, p["lichen"], as_list(lichen), 0.3)
    # Gaps: darken where neighbouring stones stand above this pixel.
    occlusion = np.clip((wrap_blur(h, 8) - h) / 7.0, 0, 1)
    rgb = mix(rgb, p["void"], as_list(occlusion), 0.6)
    return cast(rgb, height, p["void"], reach=4, strength=0.35), None
