# SPDX-License-Identifier: GPL-3.0-or-later
"""The arctic tundra terrain material."""
import numpy as np
from scipy.ndimage import gaussian_filter

from terrain_core import (
    RECIPES,
    Style,
    desaturate,
    fbm,
    flat,
    group_cracks,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shifted,
    smoothstep,
)
from ._heather import heather_cushions, soft_shadow
from .tundra import frost_polygons, tundra_stones


@recipe(name="arctic_tundra", group="barren", label="Arctic tundra", profile="brush", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (65, 60, 52), "mid": (79, 73, 64), "light": (94, 88, 78), "rim": (136, 136, 138),
                 "crack": (48, 45, 42), "fissure": (150, 160, 196), "shadow": (56, 54, 54), "rime": (190, 196, 212),
                 "stones": [(150, 148, 146), (120, 120, 122), (138, 136, 136), (110, 108, 110), (160, 158, 156)],
                 "stem": (164, 172, 204), "body": (208, 212, 234), "tip": (238, 240, 250),
                 "dead": (208, 212, 234), "deadtip": (238, 240, 250),
                 "sparkle": (252, 252, 255)},
        style=Style(luma=136, std=58, grain_max=16, match=0.7), periodic=True, grid=8, blocks=4)
def render_arctic_tundra(ctx):
    """Tundra's frost-patterned ground under snow: the same polygons about two
    cells across, but snow lies in scalloped patches where the moss pads would
    grow, leaving frozen peaty earth bare in the troughs, where the barren
    group's cracks run and rime frosts the raised rims. The cracks carry on
    faintly through the snow, and grey stone beds keep a dusting on top."""
    p = RECIPES["arctic_tundra"].palette
    size = ctx.size
    centre, _ = frost_polygons(ctx)
    rim = [1 - smoothstep(0.03, 0.12, d) for d in centre]
    rgb = frozen_ground(ctx, p, rim)
    stones, cover = tundra_stones(ctx, p)
    rgb = mix(rgb, p["shadow"], soft_shadow(cover), 0.25)
    rgb = mix(rgb, stones, cover, 1.0)
    # Snow lies exactly where tundra's moss pads grow: the same clumps of
    # rounded cushions, built of small lumps instead of sprigs, in snow tones.
    drift = layout_field(ctx, lambda rng: fbm(rng, 2, 3, size=size))
    patch = [0.7 * smoothstep(0.08, 0.3, d) + 0.3 * v for d, v in zip(centre, drift)]
    drifts, cushions = heather_cushions(ctx, p, cells=10, tufts=1400, threshold=0.47, spread=0.2, light=0.08,
                                        part=0.04, dead_share=0.0, patch=patch)
    # Drifted snow fills the hollows between neighbouring cushions, so a
    # clump reads as one scalloped drift rather than separate domes.
    merged = gaussian_filter(np.asarray(cushions).reshape(size, size), 5.0, mode="wrap").ravel()
    snow = [max(c, smoothstep(0.3, 0.6, m)) for c, m in zip(cushions, merged)]
    drifts = mix(drifts, p["body"], [s - c for s, c in zip(snow, cushions)], 0.8)
    rgb = mix(rgb, p["shadow"], soft_shadow(snow), 0.2)
    rgb = mix(rgb, snow_texture(ctx, p, drifts), snow, 1.0)
    # A thin dusting of snow caught on the stone tops.
    dust = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size))
    rgb = mix(rgb, p["tip"], [c * smoothstep(0.55, 0.8, v) for c, v in zip(cover, dust)], 0.55)
    # The barren group's cracks run through earth and snow alike: dark in the
    # bare troughs, blue shadowed fissures where they split the snow.
    crack = mix(flat(p["crack"], size), p["fissure"], snow, 1.0)
    return crack_mark(ctx, desaturate(rgb, 0.05), crack, [0.85 - 0.65 * s for s in snow]), None


def crack_mark(ctx, rgb, crack, strength):
    """barren_mark's shared cracks and lit lower lip, with a per-pixel crack
    colour and strength."""
    cracks = group_cracks(ctx)
    lip = [max(0.0, b - a) for a, b in zip(cracks, shifted(cracks, 1, 1))]
    rgb = mix(rgb, crack, [c * k for c, k in zip(cracks, strength)], 1.0)
    return [[c * (1 + 0.20 * l) for c, l in zip(channel, lip)] for channel in rgb]


def frozen_ground(ctx, p, rim):
    """Dark frozen earth, colder and greyer than tundra's peat, with the
    rime frost along the raised polygon rims."""
    size = ctx.size
    base = layout_field(ctx, lambda rng: fbm(rng, 5, 3, size=size))
    rgb = ramp(base, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    fines = layout_field(ctx, lambda rng: lattice_noise(rng, 96, size=size))
    rgb = mix(rgb, p["rim"], [r * (0.5 + 0.5 * f) for r, f in zip(rim, fines)], 0.6)
    grit = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, grit)] for ch in rgb]
    rime = layout_field(ctx, lambda rng: lattice_noise(rng, 160, size=size))
    rgb = mix(rgb, p["rime"], [smoothstep(0.86, 0.94, v) for v in rime], 0.45)
    return rgb


def snow_texture(ctx, p, rgb):
    """Fine snow grain and sparkle over the cushions."""
    size = ctx.size
    grain = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (1 + 0.03 * (v - 0.5) * 2) for c, v in zip(ch, grain)] for ch in rgb]
    speck = layout_field(ctx, lambda rng: lattice_noise(rng, 128, size=size))
    return mix(rgb, p["sparkle"], [smoothstep(0.9, 0.96, v) for v in speck], 0.6)
