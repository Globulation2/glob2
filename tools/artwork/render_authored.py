#!/usr/bin/env python3
"""Render the hand-authored SVG area markers to runtime frames.

Each SVG under datasrc/gfx/authored/ holds one group per animation frame
("frame0", "frame1", ...) on a canvas four times the classic sprite size. This
writes, for every frame, the classic sprite (data/gfx/<name><n>.png at 1/4 size)
and the high-resolution frame (data/highres/v1/<name><n>.png at full size), both
rendered from the same vector source so they cannot drift apart. Registering the
high-resolution frames in the pack (frames.txt, manifest.json, production/) is
done by tools/artwork/package_runtime.py --capture-approved afterwards.

Requires librsvg and GdkPixbuf through PyGObject (gi.repository.Rsvg).

    python3 tools/artwork/render_authored.py [--check]

--check renders to memory and fails if any committed PNG differs.
"""
import argparse
import io
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
AUTHORED = ROOT / 'datasrc/gfx/authored'
# Animated markers: name -> number of frames. The classic sprite is the
# high-resolution canvas / SCALE.
MARKERS = {'area-farm': 8}
SCALE = 4
# Single classic-size images with no high-resolution layer: svg name -> data/gfx file.
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
            yield source, frame, canvas, ROOT / 'data/highres/v1' / f'{name}{frame}.png'
            yield source, frame, canvas // SCALE, ROOT / 'data/gfx' / f'{name}{frame}.png'
    for name, file in CLASSIC.items():
        source = (AUTHORED / f'{name}.svg').read_bytes()
        yield source, None, int(ElementTree.fromstring(source).get('width')), ROOT / 'data/gfx' / file


def check_marker_pairs():
    """The rules tools/artwork/validate_markers.py applies to recovered markers:
    a Lanczos downscale of each high-resolution frame matches its classic frame
    (mean premultiplied RGB error under 0.015) and keeps its alpha coverage
    (ratio within 0.97..1.03)."""
    from PIL import Image
    failures = []
    for name, frames in MARKERS.items():
        for frame in range(frames):
            hd = Image.open(ROOT / 'data/highres/v1' / f'{name}{frame}.png').convert('RGBA')
            classic = Image.open(ROOT / 'data/gfx' / f'{name}{frame}.png').convert('RGBA')
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


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--check', action='store_true',
                        help='fail if committed frames differ from the SVG or break the marker rules')
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
        failures = check_marker_pairs()
        if failures:
            print('Marker frames break the downscale/coverage rules:', *failures)
            return 1
        print('PASS:', count, 'authored files match their SVG sources and marker rules')
    else:
        print('Rendered', count, 'authored files')
    return 0


if __name__ == '__main__':
    sys.exit(main())
