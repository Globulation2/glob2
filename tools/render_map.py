#!/usr/bin/env python3
"""Render generated maps to PNG, alone or as a comparison sheet.

Every map comes from build/src/MapGeneratorStudy's dump (build it with
`scons release=1 map-generator-study`), so what is drawn is exactly what the
generator made. No third-party modules: the PNG is written with zlib.

One map:
  tools/render_map.py one coral --seed 7 --size 256 --out artifacts/coral.png
  tools/render_map.py one maze --set cell-shape=1 --set teams=6 --overlay sites

A sheet: every generator given, beside each other, at every size given, with
an HTML page captioning each image (the rule for a new generator is to compare
it with its nearest neighbours at 128, 256 and 512 before showing it):
  tools/render_map.py sheet coral spider-web --sizes 128 256 512 --out artifacts/sheet

Generators are named by their stable string id or numeric legacy id
(`MapGeneratorStudy --catalog` lists both). Overlays tint the map by a measure
the study writes beside the dump: growth (crop growth chance), sites (where a
4x4 building fits), chop (cost from the nearest colony, clearing crops) and
owner (which colony that nearest colony is).
"""
import argparse
import html
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STUDY = ROOT / 'build' / 'src' / 'MapGeneratorStudy'

# The dump's classes: grass, sand, water, mixed shore, wheat, wood, stone, building, fruit, algae.
PALETTE = {
    0: (86, 160, 64), 1: (222, 204, 140), 2: (40, 92, 168), 3: (196, 178, 118),
    4: (236, 200, 60), 5: (24, 96, 36), 6: (150, 150, 150), 7: (220, 40, 40),
    8: (210, 60, 170), 9: (60, 190, 170),
}
OWNER_COLOURS = [
    (230, 25, 75), (60, 180, 75), (255, 225, 25), (0, 130, 200), (245, 130, 48),
    (145, 30, 180), (70, 240, 240), (240, 50, 230), (210, 245, 60), (250, 190, 212),
    (0, 128, 128), (220, 190, 255),
]


def write_png(path, width, height, pixels):
    """pixels: a bytes-like of width * height * 3 RGB values."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * width * 3:(y + 1) * width * 3]

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(png)


def read_grid(path):
    lines = Path(path).read_text().split('\n')
    width, height = map(int, lines[0].split())
    grid = [list(map(int, lines[1 + y].split())) for y in range(height)]
    return width, height, grid


def catalog():
    out = subprocess.run([str(STUDY), '--catalog'], capture_output=True, text=True, check=True, cwd=ROOT)
    return json.loads(out.stdout)


def resolve(name, entries):
    for entry in entries:
        if str(entry['method']) == str(name) or entry['id'] == name:
            return entry
    sys.exit(f'unknown generator {name!r}; MapGeneratorStudy --catalog lists them')


def exponent(size):
    e = int(size).bit_length() - 1
    if 1 << e != int(size):
        sys.exit(f'map sizes are powers of two, not {size}')
    return e


def generate(entry, seed, width, height, settings, overlay, workdir):
    dump = Path(workdir) / f"{entry['id']}-{width}x{height}-{seed}.dump"
    profile = Path(workdir) / 'profile'
    profile.mkdir(exist_ok=True)
    command = [str(STUDY), str(entry['method']), str(seed), str(profile), 'quality',
               f'width={exponent(width)}', f'height={exponent(height)}', f'dump={dump}']
    command += [f'{k}={v}' for k, v in settings]
    if overlay:
        command.append(f'overlay={overlay}')
    result = subprocess.run(command, capture_output=True, text=True, cwd=ROOT)
    quality = next((l for l in result.stdout.splitlines() if l.startswith('QUALITY')), '')
    if result.returncode != 0 or not dump.exists():
        return None, (result.stderr.strip().splitlines() or ['failed'])[-1], quality
    return dump, '', quality


def render(dump, overlay, scale):
    width, height, grid = read_grid(dump)
    tint = None
    if overlay:
        _, _, tint = read_grid(str(dump) + '.overlay')
        peak = max(max(row) for row in tint) or 1
    pixels = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            r, g, b = PALETTE.get(grid[y][x], (0, 0, 0))
            if tint is not None:
                v = tint[y][x]
                if overlay == 'owner':
                    if v >= 0:
                        orr, og, ob = OWNER_COLOURS[v % len(OWNER_COLOURS)]
                        r, g, b = (r + orr) // 2, (g + og) // 2, (b + ob) // 2
                elif overlay == 'sites':
                    if v:
                        r, g, b = (r + 255) // 2, (g + 255) // 2, b // 2
                else:
                    share = 0 if v < 0 else v / peak
                    if overlay == 'chop':
                        share = 1 - share if v >= 0 else 0
                    r, g, b = int(r * (1 - share) + 255 * share), int(g * (1 - share)), int(b * (1 - share))
            row += bytes((r, g, b)) * scale
        pixels += row * scale
    return width * scale, height * scale, pixels


def settings_of(pairs):
    out = []
    for pair in pairs or []:
        key, _, value = pair.partition('=')
        if not value:
            sys.exit(f'--set takes key=value, not {pair!r}')
        out.append((key, value))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='mode', required=True)
    for mode in ('one', 'sheet'):
        p = sub.add_parser(mode)
        p.add_argument('generators', nargs='+' if mode == 'sheet' else 1)
        p.add_argument('--seed', type=int, default=1)
        p.add_argument('--set', action='append', metavar='KEY=VALUE',
                       help='a control by id, or teams=/workers=; repeatable')
        p.add_argument('--overlay', choices=['growth', 'sites', 'chop', 'owner'])
        p.add_argument('--scale', type=int, default=0, help='pixels per tile (default: fit ~512 px)')
        if mode == 'one':
            p.add_argument('--size', type=int, default=256)
            p.add_argument('--height', type=int)
            p.add_argument('--out', required=True)
        else:
            p.add_argument('--sizes', type=int, nargs='+', default=[128, 256, 512])
            p.add_argument('--out', required=True, help='a directory: PNGs and index.html')
    args = parser.parse_args()
    if not STUDY.exists():
        sys.exit(f'{STUDY} is missing; run: scons release=1 map-generator-study')
    entries = catalog()
    settings = settings_of(args.set)
    with tempfile.TemporaryDirectory() as workdir:
        if args.mode == 'one':
            entry = resolve(args.generators[0], entries)
            height = args.height or args.size
            dump, error, quality = generate(entry, args.seed, args.size, height, settings, args.overlay, workdir)
            if not dump:
                sys.exit(f"{entry['id']}: {error}")
            scale = args.scale or max(1, 512 // max(args.size, height))
            w, h, pixels = render(dump, args.overlay, scale)
            write_png(args.out, w, h, pixels)
            print(f'{args.out} {quality}')
            return
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        cells = []
        for name in args.generators:
            entry = resolve(name, entries)
            row = []
            for size in args.sizes:
                dump, error, quality = generate(entry, args.seed, size, size, settings, args.overlay, workdir)
                image = f"{entry['id']}-{size}.png"
                if dump:
                    scale = args.scale or max(1, 512 // size)
                    w, h, pixels = render(dump, args.overlay, scale)
                    write_png(out / image, w, h, pixels)
                    row.append((size, image, quality))
                else:
                    row.append((size, None, error))
                print(entry['id'], size, 'ok' if dump else error)
            cells.append((entry, row))
        page = ['<!doctype html><meta charset="utf-8"><title>Map sheet</title>',
                '<style>body{font:13px system-ui;background:#222;color:#ddd;margin:16px}'
                'table{border-collapse:collapse}td{padding:6px;vertical-align:top}'
                'img{image-rendering:pixelated;width:320px;height:320px;display:block}'
                '.q{font-size:11px;color:#aaa;max-width:320px;word-break:break-all}</style>',
                f'<p>seed {args.seed}; settings {html.escape(str(settings))}; overlay {args.overlay}</p><table>',
                '<tr><th></th>' + ''.join(f'<th>{s}</th>' for s in args.sizes) + '</tr>']
        for entry, row in cells:
            page.append(f"<tr><th>{html.escape(entry['id'])}</th>")
            for size, image, note in row:
                body = f'<img src="{image}">' if image else '<div>failed</div>'
                page.append(f'<td>{body}<div class="q">{html.escape(note)}</div></td>')
            page.append('</tr>')
        page.append('</table>')
        (out / 'index.html').write_text('\n'.join(page))
        print(out / 'index.html')


if __name__ == '__main__':
    main()
