#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared tile helpers for terrain material production.

Both production methods, procedural synthesis (`terrain_synth.py`) and the
image-generator export (`export_material.py`), cut a 128x128 sheet into sixteen
32x32 variants, equalise their perimeters so any two variants can meet, hash the
decoded pixels for provenance and measure the same style statistics. Keeping
that code here makes the two methods byte-identical where they overlap.

Pure Python 3 plus Pillow; no numpy.
"""
import hashlib
import math
import statistics
from pathlib import Path

from PIL import Image

TILE = 32
SHEET_TILES = 4
SHEET = TILE * SHEET_TILES
ROOT = Path(__file__).resolve().parents[2]

# Style references: the native tiles the new materials must sit beside. They are
# only ever measured (luma mean/std, neighbour grain, saturation); no pixel of
# them is read into any output.
STYLE_REFERENCES = {
    "grass": ("data/gfx/terrain", 0),
    "sand": ("data/gfx/terrain", 128),
    "water": ("data/gfx/terrain", 256),
    "ice": ("data/gfx/terrain", 272),
    "trail": ("data/gfx/terrain", 288),
}


def fnv1a32(text):
    """Stable 32-bit seed for a material/variant/phase label."""
    value = 0x811C9DC5
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return value


def luma(r, g, b):
    return 0.299 * r + 0.587 * g + 0.114 * b


def box_down(image, size=SHEET):
    """Area-average an RGBA image to `size` x `size` on premultiplied colour."""
    if image.mode != "RGBA":
        image = image.convert("RGBA")
    if image.size == (size, size):
        return image.copy()
    return image.convert("RGBa").resize((size, size), Image.Resampling.BOX).convert("RGBA")


def sheet_to_tiles(sheet):
    """Cut a 128x128 sheet into sixteen 32x32 tiles, row-major."""
    if sheet.size != (SHEET, SHEET):
        raise ValueError(f"Sheet must be {SHEET}x{SHEET}, got {sheet.size}")
    sheet = sheet.convert("RGBA")
    return [
        sheet.crop((x * TILE, y * TILE, (x + 1) * TILE, (y + 1) * TILE))
        for y in range(SHEET_TILES)
        for x in range(SHEET_TILES)
    ]


def tiles_to_sheet(tiles, columns=SHEET_TILES, scale=1):
    """Lay tiles out in a grid (nearest-neighbour `scale`), for previews only."""
    size = tiles[0].width * scale
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * size, rows * size), (0, 0, 0, 0))
    for i, tile in enumerate(tiles):
        if scale != 1:
            tile = tile.resize((size, size), Image.Resampling.NEAREST)
        sheet.paste(tile, ((i % columns) * size, (i // columns) * size))
    return sheet


def _premultiply(pixel):
    r, g, b, a = pixel
    return (r * a, g * a, b * a, a * 255)


def _unpremultiply(p):
    r, g, b, a = p
    alpha = a / 255
    if alpha <= 0:
        return (0, 0, 0, 0)
    return tuple(min(255, max(0, round(c / alpha))) for c in (r, g, b)) + (min(255, max(0, round(alpha))),)


def share_perimeter(tiles, width=2):
    """Give every tile the perimeter of tile 0 so any two variants join.

    Ring 0 is copied from tile 0; opposite edges read the same coordinates
    (x = 31 reads x = 0, y = 31 reads y = 0), so a tile also joins itself across
    the torus seam. Ring 1 (and further rings when `width` > 2) is blended
    toward tile 0 in premultiplied RGBA, with the blend weight falling off
    linearly, so translucent materials keep both colour and opacity continuous
    without dark fringes. The runtime adds its own four-pixel premultiplied
    blend toward variant 0 (see `seamless_sources` in tools/terrain_tileset.py).
    """
    if not tiles:
        return []
    size = tiles[0].width
    last = size - 1
    shared = tiles[0].convert("RGBA").copy()
    shared_pixels = shared.load()
    result = []
    for tile in tiles:
        out = tile.convert("RGBA").copy()
        pixels = out.load()
        for y in range(size):
            for x in range(size):
                distance = min(x, y, last - x, last - y)
                if distance >= width:
                    continue
                sx = 0 if x == last else x
                sy = 0 if y == last else y
                boundary = shared_pixels[sx, sy]
                if distance == 0:
                    pixels[x, y] = boundary
                    continue
                weight = (width - distance) / width
                own = _premultiply(pixels[x, y])
                target = _premultiply(boundary)
                pixels[x, y] = _unpremultiply(
                    tuple(o * (1 - weight) + t * weight for o, t in zip(own, target))
                )
        result.append(out)
    return result


def runtime_blend(tiles, width=4):
    """The renderer's own border preparation, for previews and tests.

    Mirrors `seamless_sources` in tools/terrain_tileset.py: four native pixels
    blended toward variant 0 (mirrored coordinates) on premultiplied RGBA.
    """
    if not tiles:
        return []
    size = tiles[0].width
    last = size - 1
    master = tiles[0].convert("RGBA").load()
    result = []
    for tile in tiles:
        out = tile.convert("RGBA").copy()
        pixels = out.load()
        for y in range(size):
            for x in range(size):
                distance = min(x, y, last - x, last - y)
                if distance >= width:
                    continue
                source = master[min(x, last - x), min(y, last - y)]
                pixel = pixels[x, y]
                alpha = source[3] * (width - distance) + pixel[3] * distance
                pixels[x, y] = tuple(
                    (source[k] * source[3] * (width - distance) + pixel[k] * pixel[3] * distance) // alpha
                    if alpha else 0
                    for k in range(3)
                ) + (alpha // width,)
        result.append(out)
    return result


def pixel_sha256(image):
    """sha256 of decoded RGBA bytes: independent of PNG encoder settings."""
    return hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()


def tile_hashes(tiles):
    return [pixel_sha256(tile) for tile in tiles]


def write_frames(tiles, prefix, first=0, root=ROOT):
    """Write tiles as `<prefix><first + i>.png`; return {relative path: pixel sha}."""
    hashes = {}
    for i, tile in enumerate(tiles):
        relative = f"{prefix}{first + i}.png"
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        tile.save(path, optimize=False)
        hashes[relative] = pixel_sha256(tile)
    return hashes


def load_frames(prefix, first, count, root=ROOT):
    tiles = []
    for i in range(count):
        with Image.open(root / f"{prefix}{first + i}.png") as image:
            tiles.append(image.convert("RGBA"))
    return tiles


def _luma_rows(tile):
    pixels = tile.convert("RGBA").load()
    size = tile.width
    return [[luma(*pixels[x, y][:3]) for x in range(size)] for y in range(size)]


def style_stats(tiles):
    """Luma mean/std, wrapped neighbour grain, HSV saturation and alpha mean.

    Matches the measurements quoted for the native tiles in the plan and the
    terrain-materials guide: grass 79/5.0/3.0/0.73, sand 160/27/28/0.74,
    trail 103/10/8/0.37, ice 215/6/4/0.17, water 95/8/5.5/0.92.
    """
    lum, grain, sat, alpha = [], [], [], []
    for tile in tiles:
        tile = tile.convert("RGBA")
        size = tile.width
        pixels = tile.load()
        rows = _luma_rows(tile)
        for y in range(size):
            for x in range(size):
                value = rows[y][x]
                lum.append(value)
                grain.append(abs(value - rows[y][(x + 1) % size]))
                grain.append(abs(value - rows[(y + 1) % size][x]))
                r, g, b, a = pixels[x, y]
                high = max(r, g, b)
                sat.append((high - min(r, g, b)) / high if high else 0.0)
                alpha.append(a)
    return {
        "luma": round(statistics.fmean(lum), 1),
        "std": round(statistics.pstdev(lum), 1),
        "grain": round(statistics.fmean(grain), 1),
        "sat": round(statistics.fmean(sat), 2),
        "alpha": round(statistics.fmean(alpha), 1),
    }


def mean_color(tiles):
    """Mean RGB over the opaque-weighted pixels of all tiles."""
    total = [0.0, 0.0, 0.0]
    weight = 0.0
    for tile in tiles:
        pixels = tile.convert("RGBA").load()
        for y in range(tile.height):
            for x in range(tile.width):
                r, g, b, a = pixels[x, y]
                total[0] += r * a
                total[1] += g * a
                total[2] += b * a
                weight += a
    if weight <= 0:
        return (0, 0, 0)
    return tuple(int(round(c / weight)) for c in total)


def interior_grain(tile):
    """Mean absolute luma step between interior neighbours (no wrap)."""
    rows = _luma_rows(tile)
    size = len(rows)
    steps = []
    for y in range(size):
        for x in range(size):
            if x + 1 < size:
                steps.append(abs(rows[y][x] - rows[y][x + 1]))
            if y + 1 < size:
                steps.append(abs(rows[y][x] - rows[y + 1][x]))
    return statistics.fmean(steps)


def wrap_seam_error(tile):
    """Mean absolute luma step across the torus seam (last column/row to first)."""
    rows = _luma_rows(tile)
    size = len(rows)
    steps = [abs(rows[y][size - 1] - rows[y][0]) for y in range(size)]
    steps += [abs(rows[size - 1][x] - rows[0][x]) for x in range(size)]
    return statistics.fmean(steps)


def join_error(left, right):
    """Mean absolute RGBA step where `left`'s last column meets `right`'s first."""
    lp, rp = left.convert("RGBA").load(), right.convert("RGBA").load()
    size = left.height
    return statistics.fmean(
        abs(a - b) for y in range(size) for a, b in zip(lp[size - 1, y], rp[0, y])
    )


def reference_tiles(name, root=ROOT):
    prefix, first = STYLE_REFERENCES[name]
    return load_frames(prefix, first, 16, root)


def reference_stats(root=ROOT):
    """Statistics of the native tiles, the only thing read from them."""
    return {name: style_stats(reference_tiles(name, root)) for name in STYLE_REFERENCES}


def color_distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
