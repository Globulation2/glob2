# SPDX-License-Identifier: GPL-3.0-or-later
"""Heather cushions shared by heath and tundra."""
import numpy as np

from terrain_core import (
    apply_cell_jitter,
    block_worley,
    cell_jitter,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    shade,
    shifted,
    smoothstep,
)


def soft_shadow(cover):
    """A soft shadow down-right of each covered area."""
    taps = ((3, 4, 0.35), (5, 7, 0.4), (7, 10, 0.25))
    cover = np.asarray(cover, dtype=np.float64)
    out = np.zeros(len(cover))
    for dx, dy, w in taps:
        out = out + w * shifted(cover, dx, dy)
    return out * (1 - cover)


def heather_cushions(ctx, p, cells=100, tufts=700, threshold=0.5, spread=0.12, light=0.5, part=0.55, dead_share=0.4,
                     patch=None):
    """Heath bushes: clumps of rounded cushions built of lit sprig tufts,
    a share of them dead grey. `patch` replaces the noise that decides where
    clumps grow. Returns (colour, cover)."""
    size, shared = ctx.size, ctx.shared_rng
    f1, _, ids, c1, _, cids = block_worley(ctx, cells, array=True)
    t1, _, _, ct1, _, _ = block_worley(ctx, tufts, array=True)
    count = cells * ctx.area_scale
    bias = np.array([shared.random() for _ in range(count)])
    kind = np.array([shared.random() for _ in range(count)])
    reach = np.array([shared.uniform(0.55, 0.8) for _ in range(count)])
    if patch is None:
        patch = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size, array=True))
    fray = layout_field(ctx, lambda rng: lattice_noise(rng, 40, size=size, array=True))
    twig = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size, array=True))

    def dome(a, i, fr):
        # Rounded but with a finite rim slope, so the tiny field differences
        # just inside the exact block edge cannot jump the lit rim.
        d = a / (reach[i] * (0.75 + 0.5 * fr))
        return np.where(d < 1, (1 - d * d) * (1.5 - 0.5 * d * d), 0.0)

    def tuft(t):
        return np.maximum(0.0, 1 - t / 0.75)

    grow = smoothstep(threshold - 0.04, threshold + 0.04, patch + spread * (bias[ids] - 0.5))
    body = dome(f1, ids, fray) * grow
    height = body * (0.55 + 0.45 * tuft(t1))
    common_height = dome(c1, cids, 0.5) * (0.55 + 0.45 * tuft(ct1))
    cover = smoothstep(0.0, 0.35, body)
    tone = 0.5 * height + 0.25 * tuft(t1) + 0.25 * twig
    live = ramp(tone, [(0.0, p["stem"]), (0.5, p["body"]), (1.0, p["tip"])])
    dead = ramp(tone, [(0.0, p["stem"]), (0.5, p["dead"]), (1.0, p["deadtip"])])
    mat = mix(live, dead, np.where(kind < dead_share, 1.0, 0.0)[ids], 1.0)
    mat = apply_cell_jitter(mat, ids, cell_jitter(shared, count, 0.07))
    mat = shade(mat, height, light, reference_height=common_height)
    # Dark gaps where tufts part, deepest toward the bush rim.
    gaps = smoothstep(0.25, 0.0, tuft(t1)) * (1 - 0.6 * body)
    mat = mix(mat, p["stem"], gaps, part)
    return mat, cover
