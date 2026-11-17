#!/usr/bin/env python3
"""Explain how two renderer captures differ in one region.

renderer_parity.py says how much differs; this says what kind of difference
it is, for a region such as the HUD panel's text or the far-zoom map:

    python3 tools/rendering/parity_diagnose.py \\
        artifacts/renderer-parity/<label>/colony-1x/portable-sdl.png \\
        artifacts/renderer-parity/<label>/colony-1x/sdl-gpu.png --region panel

A contact sheet's crops give regions as x0,y0,x1,y1 (summary.json lists each
scene's crop positions); pass --scale 2 for a -hidpi scene's captures.

It reports, over the pixels more than 8 levels apart:
- position: how many still differ after the best whole-pixel shift (up to
  --max-shift) and the best half-pixel shift. A difference that vanishes when
  shifted is a placement or rounding difference.
- edges: the share of differing pixels on edges (strong local gradient) as
  opposed to flat areas. Filtering differences live on edges; a wrong colour,
  blend or missing layer also shows on flat areas.
- sign: the mean signed difference (second minus first) of luminance on
  differing edge pixels, against what the first image's edge contrast predicts.
  Premultiplication or blending differences darken (or brighten) fringes
  consistently; filtering differences average out.
- blur: whether the second image is softer or sharper than the first (the
  ratio of their gradient energy over the region). Linear against nearest
  sampling, or a different mip level, shows here.
"""
import argparse
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError as error:  # pragma: no cover - environment guidance
    raise SystemExit(f'{error}; install the development requirements: uv pip install -r requirements-dev.txt')

PANEL_WIDTH = 160  # GameGUI::RIGHT_MENU_WIDTH
TOP_BAR = 16


def region_box(name, width, height, scale=1):
    panel, top = PANEL_WIDTH * scale, TOP_BAR * scale
    if name == 'all':
        return 0, 0, width, height
    if name == 'panel':
        return width - panel, 0, width, height
    if name == 'topbar':
        return 0, 0, width - panel, top
    if name == 'map':
        return 0, top, width - panel, height
    x0, y0, x1, y1 = (int(v) for v in name.split(','))
    return x0, y0, x1, y1


def luminance(image):
    return image[..., 0] * 0.299 + image[..., 1] * 0.587 + image[..., 2] * 0.114


def gradient(lum):
    gy, gx = np.gradient(lum)
    return np.hypot(gx, gy)


def shifted_difference(a, b, dx, dy):
    """Per-pixel largest channel difference with b moved by (dx, dy), on the overlap."""
    h, w = a.shape[:2]
    ys, ye = max(0, dy), min(h, h + dy)
    xs, xe = max(0, dx), min(w, w + dx)
    return np.abs(a[ys:ye, xs:xe] - b[ys - dy:ye - dy, xs - dx:xe - dx]).max(axis=2)


def half_pixel(b, axis):
    """b resampled half a pixel along an axis (the mean of neighbours)."""
    return (b + np.roll(b, 1, axis=axis)) / 2


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('first', help='Reference capture (PNG or PPM)')
    parser.add_argument('second', help='Capture to explain')
    parser.add_argument('--region', default='all', help='all, panel, topbar, map, or x0,y0,x1,y1')
    parser.add_argument('--threshold', type=int, default=8, help='Levels that count as different (default 8)')
    parser.add_argument('--max-shift', type=int, default=2)
    parser.add_argument('--scale', type=int, default=1,
                        help='Display scale of the captures (2 for renderer_parity.py\'s -hidpi scenes)')
    args = parser.parse_args()
    first = np.asarray(Image.open(args.first).convert('RGB')).astype(np.float64)
    second = np.asarray(Image.open(args.second).convert('RGB')).astype(np.float64)
    if first.shape != second.shape:
        raise SystemExit(f'sizes differ: {first.shape[1]}x{first.shape[0]} and {second.shape[1]}x{second.shape[0]}')
    x0, y0, x1, y1 = region_box(args.region, first.shape[1], first.shape[0], args.scale)
    a, b = first[y0:y1, x0:x1], second[y0:y1, x0:x1]
    delta = np.abs(a - b).max(axis=2)
    differing = delta > args.threshold
    count = int(differing.sum())
    print(f'region {args.region} ({x0},{y0})-({x1},{y1}): {count} pixels over {args.threshold} levels '
          f'({100.0 * count / differing.size:.2f}%), max {delta.max():.0f}, mean {delta.mean():.2f}')
    if not count:
        return 0
    # Position: the best whole-pixel and half-pixel shifts.
    best = min(((dx, dy) for dx in range(-args.max_shift, args.max_shift + 1)
                for dy in range(-args.max_shift, args.max_shift + 1)),
               key=lambda s: (shifted_difference(a, b, *s) > args.threshold).mean())
    after_shift = (shifted_difference(a, b, *best) > args.threshold).mean() * 100
    halves = {'x': half_pixel(b, 1), 'y': half_pixel(b, 0), 'xy': half_pixel(half_pixel(b, 1), 0)}
    half_name, half = min(halves.items(), key=lambda item: (np.abs(a - item[1]).max(axis=2) > args.threshold).mean())
    after_half = (np.abs(a - half).max(axis=2) > args.threshold).mean() * 100
    print(f'position: best whole shift {best} leaves {after_shift:.2f}%; '
          f'best half-pixel shift ({half_name}) leaves {after_half:.2f}%')
    # Edges against flat areas.
    la, lb = luminance(a), luminance(b)
    edge = gradient(la) > 24
    on_edges = (differing & edge).sum() / count * 100
    print(f'edges: {on_edges:.1f}% of differing pixels are on edges ({edge.mean() * 100:.1f}% of the region is edge)')
    # Sign on differing edge pixels.
    mask = differing & edge
    if mask.any():
        signed = (lb - la)[mask]
        print(f'sign: on differing edge pixels the second is {signed.mean():+.1f} levels '
              f'(median {np.median(signed):+.1f}; {100.0 * (signed < 0).mean():.0f}% darker)')
    flat = differing & ~edge
    if flat.any():
        signed = (lb - la)[flat]
        print(f'flat areas: {flat.sum()} differing pixels, second {signed.mean():+.1f} levels on average')
    # Softness.
    energy_a, energy_b = (gradient(la) ** 2).sum(), (gradient(lb) ** 2).sum()
    if energy_a > 0:
        ratio = energy_b / energy_a
        verdict = 'softer' if ratio < 0.9 else 'sharper' if ratio > 1.1 else 'about as sharp'
        print(f'blur: the second has {ratio:.2f}x the first\'s gradient energy ({verdict})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
