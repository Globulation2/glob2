# SPDX-License-Identifier: GPL-3.0-or-later
"""Heather cushions shared by heath and tundra."""
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
    out = [0.0] * len(cover)
    for dx, dy, w in taps:
        out = [o + w * v for o, v in zip(out, shifted(cover, dx, dy))]
    return [o * (1 - c) for o, c in zip(out, cover)]


def heather_cushions(ctx, p, cells=100, tufts=700, threshold=0.5, spread=0.12, light=0.5, part=0.55, dead_share=0.4,
                     patch=None):
    """Heath bushes: clumps of rounded cushions built of lit sprig tufts,
    a share of them dead grey. `patch` replaces the noise that decides where
    clumps grow. Returns (colour, cover)."""
    size, shared = ctx.size, ctx.shared_rng
    f1, _, ids, c1, _, cids = block_worley(ctx, cells)
    t1, _, _, ct1, _, _ = block_worley(ctx, tufts)
    count = cells * ctx.area_scale
    bias = [shared.random() for _ in range(count)]
    kind = [shared.random() for _ in range(count)]
    reach = [shared.uniform(0.55, 0.8) for _ in range(count)]
    if patch is None:
        patch = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size))
    fray = layout_field(ctx, lambda rng: lattice_noise(rng, 40, size=size))
    twig = layout_field(ctx, lambda rng: lattice_noise(rng, 64, size=size))

    def dome(a, i, fr):
        # Rounded but with a finite rim slope, so the tiny field differences
        # just inside the exact block edge cannot jump the lit rim.
        d = a / (reach[i] * (0.75 + 0.5 * fr))
        return (1 - d * d) * (1.5 - 0.5 * d * d) if d < 1 else 0.0

    def tuft(t):
        return max(0.0, 1 - t / 0.75)

    grow = [smoothstep(threshold - 0.04, threshold + 0.04, v + spread * (bias[i] - 0.5))
            for v, i in zip(patch, ids)]
    body = [dome(a, i, fr) * g for a, i, fr, g in zip(f1, ids, fray, grow)]
    height = [b * (0.55 + 0.45 * tuft(t)) for b, t in zip(body, t1)]
    common_height = [dome(a, i, 0.5) * (0.55 + 0.45 * tuft(t)) for a, i, t in zip(c1, cids, ct1)]
    cover = [smoothstep(0.0, 0.35, b) for b in body]
    tone = [0.5 * h + 0.25 * tuft(t) + 0.25 * w for h, t, w in zip(height, t1, twig)]
    live = ramp(tone, [(0.0, p["stem"]), (0.5, p["body"]), (1.0, p["tip"])])
    dead = ramp(tone, [(0.0, p["stem"]), (0.5, p["dead"]), (1.0, p["deadtip"])])
    mat = mix(live, dead, [1.0 if kind[i] < dead_share else 0.0 for i in ids], 1.0)
    mat = apply_cell_jitter(mat, ids, cell_jitter(shared, count, 0.07))
    mat = shade(mat, height, light, reference_height=common_height)
    # Dark gaps where tufts part, deepest toward the bush rim.
    gaps = [smoothstep(0.25, 0.0, tuft(t)) * (1 - 0.6 * b) for t, b in zip(t1, body)]
    mat = mix(mat, p["stem"], gaps, part)
    return mat, cover
