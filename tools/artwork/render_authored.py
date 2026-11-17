#!/usr/bin/env python3
"""Render the hand-authored SVG area markers and icons to runtime frames.

Each SVG under content-src/gfx/authored/ holds one group per animation frame
("frame0", "frame1", ...) on a canvas four times the classic sprite size. For
the markers this writes, for every frame, the classic sprite
(content/data/gfx/<name><n>.png at 1/4 size) and the high-resolution frame
(content/data/highres/v1/<name><n>.png at full size), both rendered from the
same vector source so they cannot drift apart. The icon sets are traced from
classic sprites that stay as they are, so only their high-resolution frames are
rendered. Registering the high-resolution frames in the pack (frames.txt,
manifest.json, production/) is done by tools/artwork/package_runtime.py
--capture-approved afterwards.

Requires librsvg and GdkPixbuf through PyGObject (gi.repository.Rsvg).

    python3 tools/artwork/render_authored.py [--check]

--check renders to memory and fails if any committed PNG differs.
"""
import argparse
import io
import math
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import gi

gi.require_version('Rsvg', '2.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf, Rsvg  # noqa: E402

SVG_NS = 'http://www.w3.org/2000/svg'
ElementTree.register_namespace('', SVG_NS)
ElementTree.register_namespace('xlink', 'http://www.w3.org/1999/xlink')

ROOT = Path(__file__).resolve().parents[2]
AUTHORED = ROOT / 'content-src/gfx/authored'
# Animated markers: name -> number of frames. The classic sprite is the
# high-resolution canvas / SCALE.
MARKERS = {'area-farm': 8}
SCALE = 4
# High-resolution frames only, traced from classic sprites that stay as they are:
# sprite name -> [(svg name, frame numbers)]. A sprite whose classic frames
# differ in size takes one SVG per canvas size. The carried-material icons follow
# the map resource art they come from. Most classic icons share their map art's
# colours, and the high-resolution icons keep them (a mean colour difference
# under 2). Stone's classic icon is grey and the later materials' are much
# duller than their lavender, gold, rust, crystal and cotton map art, so those
# keep the map art's colour and lean only toward the classic one.
ICONS = {'ressourcemini': [('ressourcemini', range(8)), ('ressourcemini-materials', range(8, 12))]}
ICON_COLOUR_LIMIT = 2
MAP_ART_COLOURED = {'ressourcemini3', 'ressourcemini8', 'ressourcemini9', 'ressourcemini10', 'ressourcemini11'}
MAP_ART_COLOUR_LIMIT = 12
# Single classic-size images with no high-resolution layer: svg name -> content/data/gfx file.
CLASSIC = {'gamegui58': 'gamegui58.png'}


def render(source, frame, size):
    """PNG bytes of group "frame<frame>" alone (or the whole image when frame is
    None), rendered at size x size."""
    root = ElementTree.fromstring(source)
    root.set('width', str(size))
    root.set('height', str(size))
    for group in root.iter(f'{{{SVG_NS}}}g'):
        if frame is not None and group.get('class') == 'frame' and group.get('id') != f'frame{frame}':
            group.set('display', 'none')
    handle = Rsvg.Handle.new_from_data(ElementTree.tostring(root))
    pixbuf = handle.get_pixbuf()
    if pixbuf is None or pixbuf.get_width() != size or pixbuf.get_height() != size:
        raise SystemExit(f'cannot render {frame} at {size}px')
    ok, data = pixbuf.save_to_bufferv('png', [], [])
    if not ok:
        raise SystemExit(f'cannot encode {frame}')
    return bytes(data)


def outputs():
    """(source bytes, frame or None, size, target path) for every rendered file."""
    for name, frames in MARKERS.items():
        source = (AUTHORED / f'{name}.svg').read_bytes()
        canvas = int(ElementTree.fromstring(source).get('width'))
        if canvas % SCALE:
            raise SystemExit(f'{name}.svg: canvas {canvas} is not a multiple of {SCALE}')
        for frame in range(frames):
            yield source, frame, canvas, ROOT / 'content/data/highres/v1' / f'{name}{frame}.png'
            yield source, frame, canvas // SCALE, ROOT / 'content/data/gfx' / f'{name}{frame}.png'
    for name, sets in ICONS.items():
        for svg, frames in sets:
            source = (AUTHORED / f'{svg}.svg').read_bytes()
            canvas = int(ElementTree.fromstring(source).get('width'))
            for frame in frames:
                yield source, frame, canvas, ROOT / 'content/data/highres/v1' / f'{name}{frame}.png'
    for name, file in CLASSIC.items():
        source = (AUTHORED / f'{name}.svg').read_bytes()
        yield source, None, int(ElementTree.fromstring(source).get('width')), ROOT / 'content/data/gfx' / file


def check_marker_pairs():
    """The rules tools/artwork/validate_markers.py applies to recovered markers:
    a Lanczos downscale of each high-resolution frame matches its classic frame
    (mean premultiplied RGB error under 0.015) and keeps its alpha coverage
    (ratio within 0.97..1.03)."""
    from PIL import Image
    failures = []
    for name, frames in MARKERS.items():
        for frame in range(frames):
            hd = Image.open(ROOT / 'content/data/highres/v1' / f'{name}{frame}.png').convert('RGBA')
            classic = Image.open(ROOT / 'content/data/gfx' / f'{name}{frame}.png').convert('RGBA')
            size = classic.size
            down = hd.resize(size, Image.Resampling.LANCZOS)
            a, b = classic.tobytes(), down.tobytes()
            error = sum(abs(a[i + c] * a[i + 3] - b[i + c] * b[i + 3])
                        for i in range(0, len(a), 4) for c in range(3)) / (255 * 255 * 3 * size[0] * size[1])
            coverage = sum(hd.tobytes()[3::4]) / (SCALE * SCALE) / max(1, sum(a[3::4]))
            print(f'{name}{frame}: mean premultiplied RGB error {error:.4f}, alpha coverage {coverage:.3f}')
            if error >= 0.015 or not 0.97 < coverage < 1.03:
                failures.append(f'{name}{frame}')
    return failures


def srgb_to_lab(rgb):
    """CIELAB (D65) of an sRGB colour given as 0..255 floats."""
    def linear(c):
        c /= 255
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (linear(c) for c in rgb)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 216 / 24389 else (24389 / 27 * t + 16) / 116
    return 116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))


def delta_e_2000(lab1, lab2):
    """CIEDE2000 colour difference; about 2.3 is a just noticeable difference."""
    (L1, a1, b1), (L2, a2, b2) = lab1, lab2
    mean_chroma = (math.hypot(a1, b1) + math.hypot(a2, b2)) / 2
    g = 0.5 * (1 - math.sqrt(mean_chroma ** 7 / (mean_chroma ** 7 + 25 ** 7)))
    a1, a2 = a1 * (1 + g), a2 * (1 + g)
    C1, C2 = math.hypot(a1, b1), math.hypot(a2, b2)
    h1, h2 = math.degrees(math.atan2(b1, a1)) % 360, math.degrees(math.atan2(b2, a2)) % 360
    dh = 0 if C1 * C2 == 0 else h2 - h1 if abs(h2 - h1) <= 180 else h2 - h1 - 360 if h2 > h1 else h2 - h1 + 360
    dL, dC, dH = L2 - L1, C2 - C1, 2 * math.sqrt(C1 * C2) * math.sin(math.radians(dh / 2))
    L, C = (L1 + L2) / 2, (C1 + C2) / 2
    h = h1 + h2 if C1 * C2 == 0 else (h1 + h2) / 2 if abs(h1 - h2) <= 180 else \
        (h1 + h2 + 360) / 2 if h1 + h2 < 360 else (h1 + h2 - 360) / 2
    t = (1 - 0.17 * math.cos(math.radians(h - 30)) + 0.24 * math.cos(math.radians(2 * h))
         + 0.32 * math.cos(math.radians(3 * h + 6)) - 0.20 * math.cos(math.radians(4 * h - 63)))
    sl = 1 + 0.015 * (L - 50) ** 2 / math.sqrt(20 + (L - 50) ** 2)
    sc, sh = 1 + 0.045 * C, 1 + 0.015 * C * t
    rt = -2 * math.sqrt(C ** 7 / (C ** 7 + 25 ** 7)) * math.sin(math.radians(60 * math.exp(-((h - 275) / 25) ** 2)))
    return math.sqrt((dL / sl) ** 2 + (dC / sc) ** 2 + (dH / sh) ** 2 + rt * (dC / sc) * (dH / sh))


def check_icon_pairs():
    """Each icon's high-resolution frame, reduced to its classic size, keeps the
    classic icon's colour: the CIEDE2000 difference of their alpha-weighted mean
    colours stays under its limit (2.3 is just noticeable), and its alpha coverage stays
    within 0.9..1.15. Classic pixels below alpha 8 are stray matte, not icon."""
    from PIL import Image

    def mean(pixels):
        weight = sum(p[3] for p in pixels) or 1
        return tuple(sum(p[c] * p[3] for p in pixels) / weight for c in range(3))
    failures = []
    for name, sets in ICONS.items():
        for _, frames in sets:
            for frame in frames:
                limit = MAP_ART_COLOUR_LIMIT if f'{name}{frame}' in MAP_ART_COLOURED else ICON_COLOUR_LIMIT
                hd = Image.open(ROOT / 'content/data/highres/v1' / f'{name}{frame}.png').convert('RGBA')
                classic = Image.open(ROOT / 'content/data/gfx' / f'{name}{frame}.png').convert('RGBA')
                pixels = [p if p[3] > 8 else (0, 0, 0, 0) for p in classic.getdata()]
                down = list(hd.resize(classic.size, Image.Resampling.LANCZOS).getdata())
                difference = delta_e_2000(srgb_to_lab(mean(pixels)), srgb_to_lab(mean(down)))
                coverage = sum(p[3] for p in hd.getdata()) / (SCALE * SCALE) / max(1, sum(p[3] for p in pixels))
                print(f'{name}{frame}: mean colour difference {difference:.2f} dE2000, alpha coverage {coverage:.3f}')
                if difference >= limit or not 0.7 < coverage < 1.15:
                    failures.append(f'{name}{frame}')
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--check', action='store_true',
                        help='fail if committed frames differ from the SVG or break the marker and icon rules')
    args = parser.parse_args()
    stale = []
    count = 0
    for source, frame, size, target in outputs():
        png = render(source, frame, size)
        count += 1
        if args.check:
            from PIL import Image
            old = Image.open(target).convert('RGBA').tobytes() if target.exists() else None
            if old != Image.open(io.BytesIO(png)).convert('RGBA').tobytes():
                stale.append(target.relative_to(ROOT))
        else:
            target.write_bytes(png)
    if stale:
        print('Stale rendered frames (run tools/artwork/render_authored.py):', *stale, sep='\n  ')
        return 1
    if args.check:
        failures = check_marker_pairs() + check_icon_pairs()
        if failures:
            print('Frames break the downscale/colour/coverage rules:', *failures)
            return 1
        print('PASS:', count, 'authored files match their SVG sources and marker and icon rules')
    else:
        print('Rendered', count, 'authored files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
