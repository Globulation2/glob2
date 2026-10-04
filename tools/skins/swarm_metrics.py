#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure how well a static GSK1 swarm mesh takes paint, as the game shows it.

Needs NumPy (Blender's bundled Python has it):
  python3 tools/skins/swarm_metrics.py MESH.gsk [...] [--size 128] [--preview DIR]

Rasterizes each mesh with a depth buffer from the game camera at --size
pixels (the swarm sprite's on-screen size at normal zoom), so only surface the
player can see and paint counts:
  texelsPerPixel  - paint texels per screen pixel over the visible surface
                    (median, p10, p90, and the share below 1, which looks blurry)
  visibleSeamPx   - length in screen pixels of UV seams on the visible surface
  islands         - UV islands, and how many of them are visible
  textureUse      - share of the 256x256 paint texture that is seen on screen
"""
import argparse
import json
from pathlib import Path
import struct
import zlib

import numpy as np

SUPERSAMPLE = 4
TEXTURE = 256


def load(path):
    data = Path(path).read_bytes()
    magic, vertices, indices, frames, size = struct.unpack_from('<4sIIII', data)
    if magic != b'GSK1' or frames != 1:
        raise ValueError(f'{path}: expected a single-pose GSK1 mesh')
    offset = 20
    uv = np.frombuffer(data, '<f4', vertices * 2, offset).reshape(-1, 2)
    offset += uv.nbytes
    index = np.frombuffer(data, '<u4', indices, offset).reshape(-1, 3)
    offset += index.nbytes
    pose = np.frombuffer(data, '<f4', vertices * 6, offset).reshape(-1, 6)
    return uv, index, pose[:, :3], pose[:, 3:]


def rasterize(screen, depth, triangles, size):
    """Triangle id per pixel (-1 for none); nearer is smaller depth."""
    ids = np.full((size, size), -1, np.int32)
    zbuf = np.full((size, size), np.inf)
    for t, (a, b, c) in enumerate(triangles):
        p = screen[[a, b, c]]
        area = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (p[1, 1] - p[0, 1])
        if abs(area) < 1e-12:
            continue
        x0, y0 = np.floor(p.min(axis=0)).astype(int).clip(0, size - 1)
        x1, y1 = np.ceil(p.max(axis=0)).astype(int).clip(0, size - 1)
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((p[1, 0] - xs) * (p[2, 1] - ys) - (p[2, 0] - xs) * (p[1, 1] - ys)) / area
        w1 = ((p[2, 0] - xs) * (p[0, 1] - ys) - (p[0, 0] - xs) * (p[2, 1] - ys)) / area
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * depth[a] + w1 * depth[b] + w2 * depth[c]
        region = (slice(y0, y1 + 1), slice(x0, x1 + 1))
        nearer = inside & (z < zbuf[region])
        zbuf[region][nearer] = z[nearer]
        ids[region][nearer] = t
    return ids


def components(count, edges):
    parent = np.arange(count)

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a, b in edges:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
    return np.array([find(i) for i in range(count)])


def measure(path, size, preview=None, paint=None):
    uv, triangles, position, normal = load(path)
    hi = size * SUPERSAMPLE
    screen = np.empty((len(position), 2))
    screen[:, 0] = (position[:, 0] + 1) / 2 * hi
    screen[:, 1] = (1 - position[:, 1]) / 2 * hi
    ids = rasterize(screen, position[:, 2], triangles, hi)
    seen = ids >= 0
    visible_px = np.bincount(ids[seen], minlength=len(triangles)) / SUPERSAMPLE ** 2

    def area(p):
        return np.abs(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0])) / 2
    screen_area = area(screen[triangles] / SUPERSAMPLE)
    uv_area = area(uv[triangles] * TEXTURE)
    density = np.sqrt(uv_area / np.maximum(screen_area, 1e-9))
    weights = visible_px[visible_px > 0]
    values = density[visible_px > 0]
    order = np.argsort(values)
    cumulative = np.cumsum(weights[order]) / weights.sum()

    def quantile(q):
        return float(values[order][np.searchsorted(cumulative, q)])

    # Seams: welded positions whose adjacent triangles disagree on UVs.
    _, welded = np.unique(np.round(position, 6), axis=0, return_inverse=True)
    welded = welded.ravel()
    edge_uvs = {}
    for t, tri in enumerate(triangles):
        for i in range(3):
            a, b = tri[i], tri[(i + 1) % 3]
            key = tuple(sorted((welded[a], welded[b])))
            ends = sorted(((welded[a], tuple(uv[a])), (welded[b], tuple(uv[b]))))
            edge_uvs.setdefault(key, []).append((t, tuple(ends)))
    seam_px = 0.0
    seam_mask = np.zeros_like(seen)
    for (a, b), entries in edge_uvs.items():
        if len({ends for _, ends in entries}) < 2:
            continue
        owners = {t for t, _ in entries}
        pa = screen[np.where(welded == a)[0][0]]
        pb = screen[np.where(welded == b)[0][0]]
        steps = max(2, int(np.linalg.norm(pb - pa)))
        for s in np.linspace(0, 1, steps):
            x, y = (pa + (pb - pa) * s).astype(int).clip(0, hi - 1)
            # A seam point is visible when a neighbouring pixel shows one of its triangles.
            window = ids[max(0, y - 1):y + 2, max(0, x - 1):x + 2]
            if any(int(t) in owners for t in np.unique(window)):
                seam_px += np.linalg.norm(pb - pa) / (steps - 1) / SUPERSAMPLE
                seam_mask[y, x] = True

    edges = triangles[:, [0, 1, 1, 2, 2, 0]].reshape(-1, 2)
    island = components(len(uv), edges)
    visible_islands = len(np.unique(island[triangles[visible_px > 0, 0]]))
    use = float((weights * values ** 2).sum() / TEXTURE ** 2)
    result = {'mesh': str(path), 'triangles': len(triangles), 'vertices': len(uv),
              'silhouettePx': float(seen.sum() / SUPERSAMPLE ** 2),
              'texelsPerPixel': {'median': quantile(0.5), 'p10': quantile(0.1), 'p90': quantile(0.9),
                                 'below1': float(weights[values < 1].sum() / weights.sum())},
              'visibleSeamPx': round(seam_px, 1), 'islands': int(len(np.unique(island))),
              'visibleIslands': visible_islands, 'textureUse': round(use, 3)}
    if preview:
        write_preview(preview, ids, uv, triangles, position, normal, screen, paint, seam_mask)
    return result


def write_preview(path, ids, uv, triangles, position, normal, screen, paint, seam_mask):
    """Software stand-in for the game shader: paint times simple lighting."""
    hi = ids.shape[0]
    image = np.zeros((hi, hi, 4))
    ys, xs = np.nonzero(ids >= 0)
    tri = triangles[ids[ys, xs]]
    p = screen[tri]
    q = np.stack([xs + 0.5, ys + 0.5], axis=1)
    d = (p[:, 1, 0] - p[:, 0, 0]) * (p[:, 2, 1] - p[:, 0, 1]) - (p[:, 2, 0] - p[:, 0, 0]) * (p[:, 1, 1] - p[:, 0, 1])
    w1 = ((q[:, 0] - p[:, 0, 0]) * (p[:, 2, 1] - p[:, 0, 1]) - (p[:, 2, 0] - p[:, 0, 0]) * (q[:, 1] - p[:, 0, 1])) / d
    w2 = ((p[:, 1, 0] - p[:, 0, 0]) * (q[:, 1] - p[:, 0, 1]) - (q[:, 0] - p[:, 0, 0]) * (p[:, 1, 1] - p[:, 0, 1])) / d
    w = np.stack([1 - w1 - w2, w1, w2], axis=1)[:, :, None]
    n = (normal[tri] * w).sum(axis=1)
    n /= np.linalg.norm(n, axis=1, keepdims=True)
    light = 0.45 + 0.55 * np.clip(n @ (np.array([-0.4, 0.7, 1.0]) / np.linalg.norm([-0.4, 0.7, 1.0])), 0, 1)
    if paint is None:
        colour = np.full((len(xs), 3), 0.55)
    else:
        t = ((uv[tri] * w).sum(axis=1) * TEXTURE).astype(int).clip(0, TEXTURE - 1)
        colour = paint[t[:, 1], t[:, 0]]
    image[ys, xs, :3] = colour * light[:, None]
    image[ys, xs, 3] = 1
    if seam_mask is not None:
        image[seam_mask] = (1, 0.1, 0.6, 1)
    write_png(path, image)


def write_png(path, image):
    data = (image.clip(0, 1) * 255).astype(np.uint8)
    rows = b''.join(b'\0' + row.tobytes() for row in data)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', data.shape[1], data.shape[0], 8, 6, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def read_png(path):
    """Minimal reader for 8-bit RGB/RGBA, non-interlaced PNGs (make_paint.py output)."""
    data = Path(path).read_bytes()
    width, height, depth, kind = struct.unpack('>IIBB', data[16:26])
    channels = {2: 3, 6: 4}[kind]
    if depth != 8:
        raise ValueError('expected 8-bit PNG')
    offset, body = 8, b''
    while offset < len(data):
        length, = struct.unpack('>I', data[offset:offset + 4])
        if data[offset + 4:offset + 8] == b'IDAT':
            body += data[offset + 8:offset + 8 + length]
        offset += 12 + length
    raw = np.frombuffer(zlib.decompress(body), np.uint8).reshape(height, 1 + width * channels)
    stride, out = width * channels, np.zeros((height, width * channels), np.int32)
    for y in range(height):
        f, line = raw[y, 0], raw[y, 1:].astype(np.int32)
        prior = out[y - 1] if y else np.zeros(stride, np.int32)
        row = np.zeros(stride, np.int32)
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up, corner = prior[x], (prior[x - channels] if x >= channels else 0)
            if f == 0: pred = 0
            elif f == 1: pred = left
            elif f == 2: pred = up
            elif f == 3: pred = (left + up) // 2
            else:
                pa, pb, pc = abs(up - corner), abs(left - corner), abs(left + up - 2 * corner)
                pred = left if pa <= pb and pa <= pc else up if pb <= pc else corner
            row[x] = (line[x] + pred) & 255
        out[y] = row
    return out.reshape(height, width, channels)[:, :, :3] / 255.0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('meshes', nargs='+', type=Path)
    parser.add_argument('--size', type=int, default=128)
    parser.add_argument('--preview', type=Path, help='write a shaded preview per mesh into this directory')
    parser.add_argument('--paint', type=Path, help='256px paint PNG for previews')
    args = parser.parse_args()
    paint = read_png(args.paint) if args.paint else None
    for mesh in args.meshes:
        preview = None
        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            preview = args.preview / ('-'.join(mesh.parts[-3:-1]) + '.png')
        print(json.dumps(measure(mesh, args.size, preview, paint)), flush=True)
