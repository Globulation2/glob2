#!/usr/bin/env python3
"""Generates the proposed farm-area sprites: data/gfx/area-farm0..7.png and gamegui58.png.

A lighter, sparser overlay in the style of the forbidden, guard and clearing
areas: two small seedlings per tile, growing and opening over the 8 frames.
Everything is drawn at 8x and downsampled, which gives the soft edges the
existing area sprites have. Tweak SPOTS, ALPHA or SIZE and rerun.

    python3 generate.py [output-directory]      # default: ./data/gfx
"""
import math
import os
import sys

from PIL import Image, ImageDraw

SS = 8                      # supersampling factor
T = 32                      # tile size in pixels
GREEN = (0, 200, 80)        # the farm area's colour, as in GameRenderTerrain.cpp
SPOTS = [(10, 14, 0), (23, 28, 4)]   # (centre x, base y, animation phase) per seedling
ALPHA = 200
SIZE = 1.0


def s(v):
    return v * SS


def ellipse(d, cx, cy, rx, ry, angle, fill):
    pts = []
    ca, sa = math.cos(angle), math.sin(angle)
    for i in range(24):
        t = 2 * math.pi * i / 24
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((s(cx + x * ca - y * sa), s(cy + x * sa + y * ca)))
    d.polygon(pts, fill=fill)


def seedling(d, cx, by, grow, alpha, size):
    col = GREEN + (alpha,)
    stem = (3.5 + 2.5 * grow) * size
    d.line([(s(cx), s(by)), (s(cx), s(by - stem))], fill=col, width=int(SS * (0.9 + 0.2 * size)))
    top = by - stem
    spread = 0.55 + 0.35 * grow
    for side in (-1, 1):
        a = -math.pi / 2 + side * (math.pi / 2 - spread)
        length = (2.4 + 1.2 * grow) * size
        ellipse(d, cx + math.cos(a) * length * 0.9, top + math.sin(a) * length * 0.6,
                length, 1.0 * size + 0.1, a, col)
    d.line([(s(cx - 2.0 * size), s(by + 0.6)), (s(cx + 2.0 * size), s(by + 0.6))],
           fill=GREEN + (int(alpha * 0.55),), width=int(SS * 0.8))


def overlay_frame(frame):
    img = Image.new("RGBA", (T * SS, T * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for cx, by, phase in SPOTS:
        seedling(d, cx, by, 0.5 + 0.5 * math.sin(2 * math.pi * (frame + phase) / 8), ALPHA, SIZE)
    return img.resize((T, T), Image.LANCZOS)


def zone_button():
    """Zone-strip button: square outline around one large seedling, like gamegui13/14/25."""
    frame = Image.new("RGBA", (T * SS, T * SS), (0, 0, 0, 0))
    ImageDraw.Draw(frame).rectangle([s(1), s(1), s(31) - 1, s(31) - 1],
                                    outline=GREEN + (255,), width=int(SS * 1.6))
    button = frame.resize((T, T), Image.LANCZOS)

    motif = Image.new("RGBA", (T * SS, T * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(motif)
    col = GREEN + (255,)
    d.line([(s(16), s(27)), (s(16), s(15))], fill=col, width=int(SS * 2))
    for side in (-1, 1):
        a = -math.pi / 2 + side * 0.95
        ellipse(d, 16 + math.cos(a) * 5.5, 14 + math.sin(a) * 3.5, 6.0, 2.4, a, col)
    d.line([(s(9), s(28)), (s(23), s(28))], fill=GREEN + (170,), width=int(SS * 1.5))
    motif = motif.resize((T, T), Image.LANCZOS).resize((26, 26), Image.LANCZOS)
    button.alpha_composite(motif, (3, 3))
    return button


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "data", "gfx")
    os.makedirs(out, exist_ok=True)
    for f in range(8):
        overlay_frame(f).save(os.path.join(out, f"area-farm{f}.png"))
    zone_button().save(os.path.join(out, "gamegui58.png"))
    print("wrote", out)
