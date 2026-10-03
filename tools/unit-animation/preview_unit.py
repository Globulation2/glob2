#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Review images for a derived unit (requires Pillow and NumPy).

Reads WORK/render<size>/<set>/ from derive_unit.py and writes animated WebP
previews to --out, composited the way the game draws units: the shadow layer
first, then the team layer hue-shifted like Sprite::applyTeamHueShift, over
real terrain tiles.

    python3 tools/unit-animation/preview_unit.py --work /path/to/work --out artifacts/wizard

Outputs: <set>-hd.webp (8 directions, 128 px), compare.webp (beside the
shipped worker and warrior), teams.webp (six team hues) and native.webp (all
sets at the in-game 40 px, with the shipped warrior for reference).
"""
import argparse
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from derive_unit import load_unit  # noqa: E402

# Sprite::teamHueShiftDegrees measures team hues relative to Color(51, 255, 153).
BASE_HUE = 150.0
# Directions in display order: front first, then around.
DIRECTIONS = [5, 6, 7, 0, 1, 2, 3, 4]
POSES = 32
# Shipped legacy action bases (render.py SETS): poses live at base * 4 + direction * 32 + pose.
WORKER_WALK, WARRIOR_WALK, WARRIOR_SWIM, WARRIOR_FIGHT = 64, 256, 320, 384


def hue_shift(image, degrees):
    """Rotate hue, keeping saturation and value, as DrawableSurface::shiftHSV."""
    if degrees % 360 == 0:
        return image
    a = np.asarray(image).astype(np.float32) / 255.0
    rgb, alpha = a[..., :3], a[..., 3:]
    hi, lo = rgb.max(-1), rgb.min(-1)
    delta = hi - lo
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    hue = np.zeros_like(hi)
    some = delta > 1e-6
    red = some & (hi == r)
    green = some & (hi == g) & ~red
    blue = some & ~red & ~green
    hue[red] = ((g - b)[red] / delta[red]) % 6
    hue[green] = (b - r)[green] / delta[green] + 2
    hue[blue] = (r - g)[blue] / delta[blue] + 4
    hue = (hue * 60 + degrees) % 360
    value, chroma = hi, delta
    x = chroma * (1 - np.abs((hue / 60) % 2 - 1))
    m = value - chroma
    sector = (hue // 60).astype(int)
    zero = np.zeros_like(hue)
    out = np.zeros_like(rgb)
    for i, (p, q, t) in enumerate([(chroma, x, zero), (x, chroma, zero), (zero, chroma, x),
                                   (zero, x, chroma), (x, zero, chroma), (chroma, zero, x)]):
        sel = sector == i
        for channel, src in enumerate((p, q, t)):
            out[..., channel][sel] = (src + m)[sel]
    return Image.fromarray((np.concatenate([out, alpha], -1) * 255).round().astype(np.uint8), 'RGBA')


class Frames:
    """Composited frames of derived sets (from WORK) and shipped sets (data/)."""

    def __init__(self, work):
        self.work = Path(work)

    def derived(self, name, size, direction, pose, team_degrees=0):
        folder = self.work / ('render%d' % size) / name
        number = 4 + direction * POSES + pose          # team layer; shadow layer is +256
        team = hue_shift(Image.open(folder / ('%04d.png' % number)).convert('RGBA'), team_degrees)
        shadow = folder / ('%04d.png' % (number + 256))
        if shadow.exists():
            image = Image.open(shadow).convert('RGBA')
            image.alpha_composite(team)
            return image
        return team

    @staticmethod
    def shipped(base, size, direction, pose):
        folder = ROOT / ('data/highres/v1' if size == 128 else 'data/gfx')
        number = base * 4 + direction * POSES + pose
        image = None
        for suffix in ['', 'r']:                        # base (shadow) layer, then team layer
            path = folder / ('unit%d%s.png' % (number, suffix))
            if not path.exists():
                continue
            layer = Image.open(path).convert('RGBA')
            if image is None:
                image = layer
            else:
                image.alpha_composite(layer)
        return image


def grass(width, height, scale):
    tile = Image.open(ROOT / 'data/gfx/terrain3.png').convert('RGBA')
    tile = tile.resize((32 * scale, 32 * scale), Image.NEAREST if scale > 1 else Image.BICUBIC)
    canvas = Image.new('RGBA', (width, height))
    for y in range(0, height, tile.height):
        for x in range(0, width, tile.width):
            canvas.paste(tile, (x, y))
    return canvas


def animate(cell, cols, rows, size, path, labels=None, scale=1, terrain_scale=4):
    """Write an animated WebP: cell(row, col, pose) -> RGBA image or None."""
    width, height = cols * size * scale, rows * size * scale + (14 if labels else 0)
    background = grass(width, height, terrain_scale)
    frames = []
    for pose in range(POSES):
        image = background.copy()
        for r in range(rows):
            for c in range(cols):
                sprite = cell(r, c, pose)
                if sprite is None:
                    continue
                if scale != 1:
                    sprite = sprite.resize((sprite.width * scale, sprite.height * scale), Image.NEAREST)
                image.alpha_composite(sprite, (c * size * scale + (size * scale - sprite.width) // 2,
                                               r * size * scale + (size * scale - sprite.height) // 2))
        if labels:
            draw = ImageDraw.Draw(image)
            for c, text in enumerate(labels):
                draw.text((c * size * scale + 4, height - 13), text, fill=(255, 255, 230, 255))
        frames.append(image)
    lossless = size < 128                               # keep native pixel art exact
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=40, loop=0, format='WEBP',
                   lossless=lossless, quality=100 if lossless else 90, method=4)
    print('wrote', path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--unit', default='wizard')
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=ROOT / 'artifacts/unit-preview')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    unit, frames = load_unit(args.unit), Frames(args.work)
    names = [entry['name'] for entry in unit['sets']]
    walk, swim, cast = (next(n for n in names if n.endswith(k)) for k in ('walk', 'swim', 'cast'))

    for name in names:                                 # every set, 8 directions, HD
        animate(lambda r, c, p, n=name: frames.derived(n, 128, DIRECTIONS[r * 4 + c], p), 4, 2, 128,
                args.out / (name + '-hd.webp'))

    def compare(r, c, p):                              # front (dir 5) and side (dir 3) rows
        d = [5, 3][r]
        return [lambda: frames.shipped(WORKER_WALK, 128, d, p), lambda: frames.shipped(WARRIOR_WALK, 128, d, p),
                lambda: frames.derived(walk, 128, d, p), lambda: frames.shipped(WARRIOR_FIGHT, 128, d, p),
                lambda: frames.derived(cast, 128, d, p)][c]()
    animate(compare, 5, 2, 128, args.out / 'compare.webp',
            labels=['worker walk', 'warrior walk', args.unit + ' walk', 'warrior fight', args.unit + ' cast'])

    hues = [0, 60, 120, 180, 240, 300]
    animate(lambda r, c, p: frames.derived([cast, walk][r], 128, [5, 6][r], p, hues[c]), 6, 2, 128,
            args.out / 'teams.webp')

    def native(r, c, p):                               # rows walk/cast/swim; last column: warrior
        if c == 8:
            return frames.shipped([WARRIOR_WALK, WARRIOR_FIGHT, WARRIOR_SWIM][r], 40, 5, p)
        return frames.derived([walk, cast, swim][r], 40, DIRECTIONS[c], p)
    animate(native, 9, 3, 40, args.out / 'native.webp', terrain_scale=1)
    animate(native, 9, 3, 40, args.out / 'native-3x.webp', scale=3, terrain_scale=3)


if __name__ == '__main__':
    main()
