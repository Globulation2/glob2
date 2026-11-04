# SPDX-License-Identifier: GPL-3.0-or-later
"""The pumice terrain material."""
from terrain_core import (
    N,
    RECIPES,
    Style,
    apply_cell_jitter,
    cell_jitter,
    fbm,
    layout_field,
    lumps,
    mix,
    ramp,
    recipe,
    shade,
    smoothstep,
    stamp_disc,
)


@recipe(name="pumice", group="rough", label="Pumice field", profile="rock", border_class="solid", seam={"height": 1},
        palette={"dark": (42, 40, 40), "mid": (70, 67, 65), "light": (102, 98, 95), "hole": (12, 10, 10),
                 "rust": (104, 72, 48)},
        style=Style(luma=56, std=14, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_pumice(ctx):
    """Pumice field: the rough group's heavy lumps as rounded blocks of dark
    porous volcanic rock, split by deep black crevices, pitted with vesicles."""
    p, size = RECIPES["pumice"].palette, ctx.size
    height, common, pit, ids = lumps(ctx, 3, pits=0.1, warp=20)
    tone = layout_field(ctx, lambda rng: fbm(rng, 8, 3, size=size))
    rgb = ramp(tone, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    rgb = apply_cell_jitter(rgb, ids, cell_jitter(ctx.shared_rng, 3 * ctx.area_scale, 0.15))

    def vesicles(rng):
        holes = [0.0] * (size * size)
        for _ in range(110 * (size // N) ** 2):
            x, y = rng.random() * size, rng.random() * size
            r = rng.choice((2.5, 3.0, 3.5, 4.5, 4.5, 6.5)) * rng.uniform(0.8, 1.2)
            stamp_disc(holes, x, y, r, softness=0.35)
        return holes

    holes = layout_field(ctx, vesicles)
    rock = [smoothstep(0.08, 0.3, h) for h in height]
    rgb = mix(rgb, p["hole"], [v * r for v, r in zip(holes, rock)], 0.55)
    rgb = shade(rgb, height, 0.6, reference_height=common)
    rgb = mix(rgb, p["hole"], [1 - smoothstep(0.0, 0.2, h) for h in height], 0.92)
    rgb = mix(rgb, p["hole"], pit, 0.9)
    rust = layout_field(ctx, lambda rng: fbm(rng, 14, 2, size=size))
    return mix(rgb, p["rust"], [smoothstep(0.74, 0.88, v) * r for v, r in zip(rust, rock)], 0.4), None
