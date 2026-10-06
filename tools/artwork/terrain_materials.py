#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild approved original-based HD grass, sand and connected terrain atlases.

Requires Pillow 12.2.0 and NumPy. --check verifies pixels and provenance without
writing. The shared packager assembles these finals and encodes runtime WebP.
"""
import argparse
import json
import math
from pathlib import Path
import random
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.artwork.package_runtime import PRODUCTION, require, sha, validate
from tools.artwork.validate_runtime import topology

SOURCES = ROOT / 'datasrc/gfx/derived/terrain-materials-v1'
OUTPUT = PRODUCTION / 'original-materials'
RECIPE = 'original-based grass and sand v1; retained rugged corner masks'
SCALE = 4
SIZE = 128
GRASS = range(16)
SAND = range(128, 144)
GRASS_SAND = range(16, 128)
SAND_WATER = range(144, 256)


def interpolate(get, x, y):
    """Bilinear sample; callers wrap coordinates to make the field periodic."""
    ix, iy = math.floor(x), math.floor(y)
    dx, dy = x - ix, y - iy
    return ((1 - dx) * (1 - dy) * get(ix, iy)
            + dx * (1 - dy) * get(ix + 1, iy)
            + (1 - dx) * dy * get(ix, iy + 1)
            + dx * dy * get(ix + 1, iy + 1))


def refine(original, settings):
    """Add connected and fine grain while keeping native colors as a soft target.

    Shared RGB noise preserves the palette. The block mean is pulled toward the
    native pixel, allowing neighboring pixels to blend rather than imposing an
    exact 4x4 sum. Rounding and channel clipping can exceed the floating-point
    drift allowance, particularly in sand's nearly zero blue channel.
    """
    w, h = original.size
    width, height = w * SCALE, h * SCALE
    rng = random.Random(settings['seed'])
    lattice_width, lattice_height = width // 2, height // 2
    lattice = [[rng.uniform(-1, 1) for _ in range(lattice_width)]
               for _ in range(lattice_height)]
    fine = [[rng.uniform(-1, 1) for _ in range(width)] for _ in range(height)]
    result = Image.new('RGB', (width, height))
    block_pixels = SCALE * SCALE
    for y in range(h):
        for x in range(w):
            target = original.getpixel((x, y))
            channels = [[], [], []]
            for sy in range(SCALE):
                for sx in range(SCALE):
                    px, py = x * SCALE + sx, y * SCALE + sy
                    # Two-HD-pixel grain crosses native block boundaries; the
                    # independent fine field adds crispness without leaf shapes.
                    connected = interpolate(
                        lambda i, j: lattice[j % lattice_height][i % lattice_width],
                        px / 2, py / 2)
                    detail = (settings['connected_grain'] * connected
                              + settings['fine_grain'] * fine[py][px])
                    for c in range(3):
                        smooth = interpolate(
                            lambda i, j: original.getpixel((i % w, j % h))[c],
                            (px + .5) / SCALE - .5, (py + .5) / SCALE - .5)
                        channels[c].append(target[c]
                                           + settings['smoothing'] * (smooth - target[c])
                                           + detail)
            for c in range(3):
                drift = sum(channels[c]) / block_pixels - target[c]
                retained = max(-settings['max_block_color_drift'], min(
                    settings['max_block_color_drift'], drift * (1 - settings['original_pull'])))
                channels[c] = [max(0, min(255, round(v + retained - drift)))
                               for v in channels[c]]
            for i in range(block_pixels):
                result.putpixel((x * SCALE + i % SCALE, y * SCALE + i // SCALE),
                                tuple(channels[c][i] for c in range(3)))
    return result


def rgba(path):
    with Image.open(path) as image:
        return np.asarray(image.convert('RGBA')).copy()


def source_record(path):
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path))


def load_sources():
    """Verify the selected references and retained masks before preparing outputs."""
    recipe = json.loads((SOURCES / 'recipe.json').read_text())
    require(recipe['version'] == 1, 'Unsupported terrain recipe')
    materials = {}
    for label, frame in (('grass', 0), ('sand', 128)):
        record = recipe['materials'][label]
        path, native_path = ROOT / record['path'], ROOT / f'data/gfx/terrain{frame}.png'
        require(record['frame'] == frame and sha(path) == record['sha256'],
                'Approved material hash/frame differs: ' + label)
        require(sha(native_path) == record['native_sha256'], 'Native reference differs: ' + label)
        with Image.open(path) as image:
            require(image.mode == 'RGB' and image.size == (SIZE, SIZE), 'Invalid material: ' + label)
            material = image.copy()
        with Image.open(native_path) as native:
            rebuilt = refine(native.convert('RGB'), recipe['settings'])
        require(rebuilt.tobytes() == material.tobytes(),
                'Recipe no longer reproduces the approved selection: ' + label)
        materials[label] = np.asarray(material.convert('RGBA'), dtype=np.float64)
    masks = {}
    for record in recipe['masks']:
        path = ROOT / record['path']
        require(record['frame'] not in masks and sha(path) == record['sha256'],
                'Duplicate or changed terrain mask: ' + str(path))
        family = 'grass-sand' if record['frame'] in GRASS_SAND else 'sand-water'
        require(record['family'] == family, 'Incorrect terrain mask family')
        with Image.open(path) as image:
            require(image.mode == 'L' and image.size == (SIZE, SIZE), 'Invalid terrain mask')
            masks[record['frame']] = np.asarray(image).copy()
    require(set(masks) == set(GRASS_SAND) | set(SAND_WATER), 'Incomplete transition masks')
    return recipe, materials, masks


def compatible_edges(tiles, corners, water):
    """Share edges by ordered corner pairs without changing tile interiors.

    H/S/E are the legacy grass/sand/water symbols. Transparent water's edges
    come from the unchanged water tile, including its hidden RGB. Every mip
    needs its own correction: filtering alone cannot preserve exact joins.
    """
    endpoints = {'E': water[0, 0]}
    for symbol, frames in (('H', GRASS), ('S', SAND)):
        pixels = [pixel for i in frames for pixel in
                  (tiles[i][0, 0], tiles[i][0, -1], tiles[i][-1, 0], tiles[i][-1, -1])]
        endpoints[symbol] = np.round(np.mean(pixels, axis=0)).astype(np.uint8)
    for horizontal in (True, False):
        groups = {}
        for i, tile in tiles.items():
            c = corners[i]
            sides = ([((c[0], c[2]), tile[:, 0]), ((c[1], c[3]), tile[:, -1])]
                     if horizontal else
                     [((c[0], c[1]), tile[0]), ((c[2], c[3]), tile[-1])])
            for key, edge in sides:
                groups.setdefault(key, []).append(edge.copy())
        canonical = {key: np.round(np.mean(edges, axis=0)).astype(np.uint8)
                     for key, edges in groups.items()}
        canonical['E', 'E'] = (water[:, 0] if horizontal else water[0]).copy()
        for key, edge in canonical.items():
            edge[0], edge[-1] = endpoints[key[0]], endpoints[key[1]]
        for i, tile in tiles.items():
            c = corners[i]
            if horizontal:
                tile[:, 0], tile[:, -1] = canonical[c[0], c[2]], canonical[c[1], c[3]]
            else:
                tile[0], tile[-1] = canonical[c[0], c[1]], canonical[c[2], c[3]]


def build_tiles(recipe, materials, masks):
    tiles = {}
    for i in list(GRASS) + list(SAND):
        with Image.open(ROOT / f'data/gfx/terrain{i}.png') as native:
            tiles[i] = np.asarray(refine(native.convert('RGB'), recipe['settings']).convert('RGBA')).copy()
    grass, sand = materials['grass'], materials['sand']
    for i in GRASS_SAND:
        weight = masks[i][:, :, None] / 255
        tiles[i] = np.round(sand + weight * (grass - sand)).astype(np.uint8)
    for i in SAND_WATER:
        tile = sand.astype(np.uint8).copy()
        # Shoreline geometry belongs to the retained mask, never to RGB noise.
        tile[:, :, 3] = masks[i]
        tiles[i] = tile
    return tiles


def build_atlases(tiles, corners, manifest):
    outputs = {}
    for level, record in enumerate(manifest['terrain_atlas']['levels']):
        size, border, slot = SIZE >> level, 64 >> level, 256 >> level
        resized = {i: np.asarray(Image.fromarray(tile).resize(
            (size, size), Image.Resampling.LANCZOS)).copy() for i, tile in tiles.items()}
        path = PRODUCTION / 'atlases' / record['file']
        atlas = rgba(path)
        for i in SAND_WATER:
            x, y = (i % 16) * slot + border, (i // 16) * slot + border
            # Preserve the approved alpha at each mip, not only at full size.
            resized[i][:, :, 3] = atlas[y:y + size, x:x + size, 3]
        x, y = (256 % 16) * slot + border, (256 // 16) * slot + border
        compatible_edges(resized, corners, atlas[y:y + size, x:x + size])
        for i, tile in resized.items():
            x, y = (i % 16) * slot, (i // 16) * slot
            atlas[y:y + slot, x:x + slot] = np.pad(
                tile, ((border, border), (border, border), (0, 0)), mode='edge')
        outputs[path] = Image.fromarray(atlas)
    return outputs


def prepare():
    recipe, materials, masks = load_sources()
    corners = {i: c for i, c in topology(ROOT).items() if i < 256}
    require(set(corners) == set(range(256)), 'Legacy terrain topology changed')
    tiles = build_tiles(recipe, materials, masks)
    compatible_edges(tiles, corners, rgba(PRODUCTION / 'ai-materials/terrain256.png'))
    manifest = json.loads((PRODUCTION / 'pack-metadata/manifest.json').read_text())
    outputs = {OUTPUT / f'terrain{i}.png': Image.fromarray(tile) for i, tile in tiles.items()}
    outputs.update(build_atlases(tiles, corners, manifest))
    common = [source_record(SOURCES / 'recipe.json'), source_record(Path(__file__).resolve())]
    for frame in manifest['frames']:
        if not frame['id'].startswith('terrain') or int(frame['id'][7:]) >= 256:
            continue
        i = int(frame['id'][7:])
        labels = ['grass'] if i in GRASS else ['sand'] if i in SAND or i in SAND_WATER else ['grass', 'sand']
        sources = [source_record(ROOT / recipe['materials'][label]['path']) for label in labels]
        native_frames = [i] if i in GRASS or i in SAND else [recipe['materials'][label]['frame'] for label in labels]
        sources += [source_record(ROOT / f'data/gfx/terrain{native}.png') for native in native_frames]
        if i in masks:
            family = 'grass-sand' if i in GRASS_SAND else 'sand-water'
            sources.append(source_record(SOURCES / f'{family}-masks/terrain{i}.png'))
        frame.update(recipe=RECIPE, sources=sources + common)
    return outputs, manifest


def write_or_check(outputs, check):
    for path, image in outputs.items():
        same = False
        if path.exists():
            with Image.open(path) as current:
                same = (current.mode == image.mode and current.size == image.size
                        and current.tobytes() == image.tobytes())
        if check:
            require(same, 'Stale terrain output: ' + str(path))
        elif not same:
            path.parent.mkdir(parents=True, exist_ok=True)
            image.save(path)


def update_metadata(manifest, check):
    for frame in manifest['frames']:
        if frame['recipe'] == RECIPE:
            for layer in frame['layers']:
                layer['sha256'] = layer['source_sha256'] = sha(OUTPUT / layer['file'])
    for level in manifest['terrain_atlas']['levels']:
        level['sha256'] = sha(PRODUCTION / 'atlases' / level['file'])
    manifest_path = PRODUCTION / 'pack-metadata/manifest.json'
    if check:
        require(json.loads(manifest_path.read_text()) == manifest, 'Stale terrain metadata')
    else:
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    index_path = PRODUCTION / 'package.json'
    index = json.loads(index_path.read_text())
    changed = {'manifest.json'} | {level['file'] for level in manifest['terrain_atlas']['levels']}
    changed.update(layer['file'] for frame in manifest['frames'] if frame['recipe'] == RECIPE
                   for layer in frame['layers'])
    for record in index['files']:
        name = record['runtime']
        if name.startswith('terrain') and name[7:-4].isdigit() and int(name[7:-4]) < 256:
            record['source'] = 'original-materials/' + name
        if name in changed:
            record['sha256'] = sha(PRODUCTION / record['source'])
    if check:
        require(json.loads(index_path.read_text()) == index, 'Stale terrain production index')
    else:
        index_path.write_text(json.dumps(index, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Validate without writing')
    args = parser.parse_args()
    outputs, manifest = prepare()
    write_or_check(outputs, args.check)
    update_metadata(manifest, args.check)
    if not args.check:
        # Promotion moves these frames out of the historical AI-material folder.
        # Delete only their named copies, after all outputs and metadata exist.
        for i in range(256):
            (PRODUCTION / f'ai-materials/terrain{i}.png').unlink(missing_ok=True)
    validate()
    print('PASS approved grass/sand recipes, 32 variants, 224 transitions and four atlas mip levels')


if __name__ == '__main__':
    main()
