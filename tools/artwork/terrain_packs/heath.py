# SPDX-License-Identifier: GPL-3.0-or-later
"""The heath terrain material."""
import math
import random

from terrain_core import (
    RECIPES,
    TAU,
    Style,
    barren_mark,
    blend_fields,
    block_object_field,
    clamp01,
    fbm,
    lattice_noise,
    layout_field,
    mix,
    ramp,
    recipe,
    shifted,
    smoothstep,
)


@recipe(name="heath", group="barren", label="Heath", profile="brush", border_class="solid",
        seam={"height": 2, "cast_q8": 48, "cast_width_q8": 480},
        palette={"dark": (108, 98, 86), "mid": (126, 116, 102), "light": (138, 128, 114),
                 "crack": (74, 62, 54), "shadow": (78, 68, 60),
                 # Heather shrubs: woody stems in the gaps, then foliage by kind.
                 "stem": (52, 42, 36),
                 "olive": (72, 72, 54), "olive_tip": (118, 116, 86),
                 "rust": (86, 66, 52), "rust_tip": (130, 104, 82),
                 "dead": (90, 84, 74), "dead_tip": (136, 128, 112),
                 "flower": (104, 80, 90), "flower_tip": (148, 116, 128)},
        style=Style(luma=100, std=10.5, grain_max=14, match=0.7), periodic=True, grid=8, blocks=4)
def render_heath(ctx):
    """Dry heath: scattered stands of low heather shrubs over pale, faded,
    cracked sandy peat. Each shrub is a rounded, irregular dome of small
    pointed leaves over woody stems, lit toward the upper left and shaded
    below; shrubs are olive, bronze or dead grey, the largest dying back to
    a grey centre, and some still carry faded mauve flower beads."""
    p = RECIPES["heath"].palette
    rgb = heath_ground(ctx, p)
    mat, cover = heather_shrubs(ctx, p)
    rgb = mix(rgb, p["shadow"], cast_shadow(cover), 0.55)
    return [[g * (1 - a) + m for g, m, a in zip(gc, mc, cover)] for gc, mc in zip(rgb, mat)], None


def cast_shadow(cover):
    """A long soft shadow down-right of each shrub, grounding it."""
    out = [0.0] * len(cover)
    for dx, dy, w in ((3, 4, 0.3), (6, 8, 0.35), (9, 12, 0.35)):
        out = [o + w * v for o, v in zip(out, shifted(cover, dx, dy))]
    return [o * (1 - c) for o, c in zip(out, cover)]


def heath_ground(ctx, p):
    """Pale, faded sandy peat under the barren group's mark."""
    size = ctx.size
    base = layout_field(ctx, lambda rng: fbm(rng, 4, 3, size=size))
    rgb = ramp(base, [(0.0, p["dark"]), (0.5, p["mid"]), (1.0, p["light"])])
    sand = layout_field(ctx, lambda rng: lattice_noise(rng, 48, size=size))
    rgb = [[c * (0.93 + 0.14 * v) for c, v in zip(ch, sand)] for ch in rgb]
    return barren_mark(ctx, rgb, p["crack"], fade=0.15)


# Shrub kinds: (share, leaf colour, sunlit tip colour). Muted, as heather is
# outside its flowering weeks.
KINDS = (
    (4, "olive"),
    (3, "rust"),
    (2, "dead"),
    (2, "flower"),
)
LIGHT3 = (-0.5, -0.5, math.sqrt(0.5))  # toward the upper left and up


def heather_shrubs(ctx, p, count=3, radius=(18.0, 34.0), threshold=0.52):
    """Heather shrubs: rounded mounds of small pointed leaves, `count` sites
    per tile of `radius` render px, kept whole where a broad stand field
    passes `threshold` at their centre. Returns (premultiplied colour,
    cover).

    Boundary shrubs and the stand field there come from the shared stream,
    and each shrub's leaves from its own seed, so every layout draws the same
    shrubs across the block edge."""
    size = ctx.size
    common_stand = fbm(ctx.shared_rng, 1.5, 3, size=size)
    stand = blend_fields(common_stand, fbm(ctx.rng, 1.5, 3, size=size), ctx.interior_mask)
    shared, own = block_object_field(ctx, count, radius, spacing=0.8)

    def at(field, o):
        return field[int(o[1]) % size * size + int(o[0]) % size]

    shrubs = [o for o in shared if at(common_stand, o) > threshold] + [o for o in own if at(stand, o) > threshold]
    colour = [[0.0] * (size * size) for _ in range(3)]
    alpha = [0.0] * (size * size)
    for shrub in sorted(shrubs, key=lambda o: o[1]):
        draw_shrub(colour, alpha, shrub, p, size)
    return colour, alpha


def draw_shrub(colour, alpha, shrub, p, size):
    """One shrub: a woody base, then leaves from the rim inward, lit as a dome.

    Big shrubs are in heather's degenerate phase: a grey, dead centre inside
    a living ring. Flowering shrubs carry tiny mauve beads on their sunlit
    shoot tips."""
    cx, cy, r, u = shrub
    rng = random.Random(int(u * 2 ** 40) ^ (int(cx * 64) << 20) ^ int(cy * 64))
    kind = rng.choices([name for _, name in KINDS], weights=[w for w, _ in KINDS])[0]
    lumps = [(h, rng.uniform(0.05, 0.14), rng.random() * TAU) for h in (2, 3, 5)]
    hollow = r > 26

    def outline(angle):
        return r * (1 + sum(a * math.sin(h * angle + ph) for h, a, ph in lumps))

    # Woody base under the leaves, so the gaps between them read dark; lit
    # like the dome above it.
    base = tuple(c * 1.15 for c in p["stem"])
    reach = int(r * 1.5) + 2
    for y in range(int(cy) - reach, int(cy) + reach + 1):
        for x in range(int(cx) - reach, int(cx) + reach + 1):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            d = math.hypot(dx, dy) / (0.9 * outline(math.atan2(dy, dx)))
            a = smoothstep(1.0, 0.8, d)
            if a > 0:
                lam = (dx * LIGHT3[0] + dy * LIGHT3[1]) / r
                composite(colour, alpha, (y % size) * size + x % size, tuple(c * (1 + 0.5 * lam) for c in base), a)

    # Leaves: scattered over the mound, more of them than would just cover it.
    leaves = []
    for _ in range(int(math.pi * r * r / 8)):
        angle = rng.random() * TAU
        d = min(1.0, math.sqrt(rng.random()) * 1.02)
        o = outline(angle)
        lx, ly = cx + math.cos(angle) * d * o, cy + math.sin(angle) * d * o
        h = math.sqrt(max(0.0, 1 - d * d))
        # Leaves point out and down the mound, loosely near the top.
        heading = angle + rng.gauss(0, 0.4 + 1.0 * h)
        leaves.append((h + 0.1 * rng.random(), lx, ly, heading, d, angle, rng.random(), rng.random()))
    for h, lx, ly, heading, d, angle, tone_u, pick in sorted(leaves):
        # Smaller near the top, where the shoots are seen end-on.
        half_length = (3.0 + 2.5 * tone_u) * (0.6 + 0.5 * d)
        half_width = 0.32 * half_length + 0.3
        # The dome's surface turned to the light: lit upper left, shaded lower right.
        lam = math.cos(angle) * d * LIGHT3[0] + math.sin(angle) * d * LIGHT3[1] + h * LIGHT3[2]
        name = "olive" if kind == "flower" else kind
        if hollow and d < 0.5 + 0.1 * math.sin(3 * angle):
            name = "dead"
        t = clamp01(0.1 + 0.35 * tone_u + 0.7 * (lam - 0.3))
        light = 0.72 + 0.38 * clamp01(lam + 0.2) + 0.12 * h
        tint = tuple(light * (a + (b - a) * t) for a, b in zip(p[name], p[name + "_tip"]))
        draw_leaf(colour, alpha, lx, ly, heading, half_length, half_width, tint, size)
        if kind == "flower" and lam > 0.45 and pick < 0.5:
            tip = 0.7 * half_length
            bead = tuple(a + (b - a) * clamp01(lam) for a, b in zip(p["flower"], p["flower_tip"]))
            draw_dot(colour, alpha, lx + math.cos(heading) * tip, ly + math.sin(heading) * tip, 1.2, bead, size)


def draw_leaf(colour, alpha, cx, cy, heading, half_length, half_width, tint, size):
    """A small pointed leaf, paler toward its tip, with a faint midrib and
    halves just turned to and away from the light, casting a light shadow
    onto what lies under it."""
    ca, sa = math.cos(heading), math.sin(heading)
    reach = int(half_length) + 3
    for pass_shadow in (True, False):
        ox, oy = (0.8, 1.1) if pass_shadow else (0.0, 0.0)
        for y in range(int(cy) - reach, int(cy) + reach + 2):
            for x in range(int(cx) - reach, int(cx) + reach + 2):
                dx, dy = x + 0.5 - cx - ox, y + 0.5 - cy - oy
                along = dx * ca + dy * sa
                across = -dx * sa + dy * ca
                t = along / half_length
                if abs(t) >= 1:
                    continue
                # Rounder at the stalk end, pointed at the tip.
                w = half_width * (1 - t) ** 0.8 * (1 + t) ** 0.4 / 1.1
                if w <= 0.15:
                    continue
                a = clamp01(w - abs(across) + 0.5)
                if a <= 0:
                    continue
                i = (y % size) * size + x % size
                if pass_shadow:
                    for k in range(3):
                        colour[k][i] *= 1 - 0.18 * a
                    continue
                s = across / w
                facing = (-sa * LIGHT3[0] + ca * LIGHT3[1]) * (1 if s > 0 else -1)
                f = (0.9 + 0.09 * (t + 1)) * (1 + 0.06 * facing - 0.08 * smoothstep(0.2, 0.0, abs(s)) * (1 - abs(t)))
                composite(colour, alpha, i, tuple(c * f for c in tint), a)


def draw_dot(colour, alpha, cx, cy, radius, rgb, size):
    """A tiny round bead, such as a faded flower."""
    for y in range(int(cy) - 2, int(cy) + 3):
        for x in range(int(cx) - 2, int(cx) + 3):
            a = clamp01(radius - math.hypot(x + 0.5 - cx, y + 0.5 - cy) + 0.5)
            if a > 0:
                composite(colour, alpha, (y % size) * size + x % size, rgb, a)


def composite(colour, alpha, i, rgb, a):
    """Lay `rgb` at coverage `a` over the premultiplied canvas."""
    for k in range(3):
        colour[k][i] = colour[k][i] * (1 - a) + rgb[k] * a
    alpha[i] = alpha[i] * (1 - a) + a
