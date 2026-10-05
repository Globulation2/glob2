#!/usr/bin/env python3
"""Validate approved source artwork and optional WebP runtime exports."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
WORLD_FRAME = re.compile(r'(terrain|ressource|water|cloud|black|shade|area-clearing|area-forbidden|area-guard|bullet|explosion|magiceffect|particle)\d+r?\.png')
RECOVERED_RESOURCES = set(range(14)) | set(range(15, 19)) | set(range(20, 25))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgba(path):
    with Image.open(path) as image:
        return image.convert('RGBA')


def validate_frames(root, pack, manifest):
    require(manifest['version'] == 1 and manifest['frames'], 'Invalid or empty artwork manifest')
    frames = {frame['id']: frame for frame in manifest['frames']}
    require(len(frames) == len(manifest['frames']), 'Duplicate frame IDs')
    for frame in frames.values():
        frame_id = frame['id']
        native = root / 'data/gfx' / (frame_id + '.png')
        if not native.exists():
            native = root / 'data/gfx' / (frame_id + 'r.png')
        require(rgba(native).size == (frame['width'], frame['height']), f'Logical dimensions: {frame_id}')
        layers = {layer['role']: layer for layer in frame['layers']}
        require(len(layers) == len(frame['layers']) and layers and layers.keys() <= {'base', 'team'},
                f'Invalid/duplicate layers: {frame_id}')
        for role, layer in layers.items():
            suffix = 'r.png' if role == 'team' else '.png'
            require(layer['file'] == frame_id + suffix, f'Invalid layer filename: {frame_id}')
            path = pack / layer['file']
            require(digest(path) == layer['sha256'], f'Layer hash: {path}')
            with Image.open(path) as image:
                require(image.mode == 'RGBA', f'Layer mode: {path}')
                # Blender unit renders use a fixed 128px canvas; world art is 4x.
                expected = (128, 128) if re.fullmatch(r'unit\d+', frame_id) else (
                    layer['logical_width'] * 4, layer['logical_height'] * 4)
                require(image.size == expected, f'Layer dimensions: {path}')
        for role, suffix in (('base', '.png'), ('team', 'r.png')):
            if (root / 'data/gfx' / (frame_id + suffix)).exists():
                require(role in layers, f'Missing {role} layer: {frame_id}')
    expected = {p.stem.removesuffix('r') for p in (root / 'data/gfx').glob('*.png')
                if WORLD_FRAME.fullmatch(p.name)}
    # New experimental terrain is compiled by the shared tileset pipeline and
    # uses native fallback. This HD pack covers the 272 legacy connected tiles.
    connected = topology(root)
    expected = {name for name in expected if not name.startswith('terrain') or
                int(name.removeprefix('terrain')) in connected}
    require(expected <= frames.keys(), f'Missing world frames: {sorted(expected - frames.keys())}')
    rows = {}
    lines = (pack / 'frames.txt').read_text().splitlines()
    require(lines and lines[0] == 'GLOB2_HIGHRES 1', 'Invalid source frame index header')
    for line in lines[1:]:
        row = line.split()
        require(len(row) == 6 and row[0] not in rows, f'Invalid/duplicate source index row: {line}')
        rows[row[0]] = row
    require(rows.keys() == frames.keys(), 'Source index and manifest frame IDs differ')
    for frame_id, frame in frames.items():
        layers = {layer['role']: layer['file'] for layer in frame['layers']}
        row = rows[frame_id]
        scale = '0' if re.fullmatch(r'unit\d+', frame_id) else '4'
        require(row[1:] == [str(frame['width']), str(frame['height']), scale,
                            layers.get('base', '-'), layers.get('team', '-')],
                f'Source index geometry/layers differ: {frame_id}')
    print(f'PASS: {len(frames)} frames; world coverage, paired layers, logical dimensions, RGBA and hashes')
    return frames


def atlas_image(pack, record, expected):
    path = pack / record['file']
    require(digest(path) == record['sha256'], f'Atlas hash: {path}')
    image = rgba(path)
    require(image.size == expected, f'Atlas dimensions: {path}')
    return np.asarray(image)


def validate_resources(root, pack, manifest, frames):
    levels = manifest['resource_atlas']['levels']
    require(len(levels) == 4, 'Expected four resource atlas levels')
    tiles = []
    for i in range(65):
        name = f'ressource{i}'
        tile = rgba(pack / (name + '.png'))
        if i in RECOVERED_RESOURCES:
            frame = frames[name]
            require(frame['recipe'].startswith(('recovered original tree:', 'recovered original wheat:',
                                               'recovered original papyrus:')), f'Resource recipe: {name}')
            native = rgba(root / frame['sources'][1]['path'])
            require(tile.tobytes() == native.resize(tile.size, Image.Resampling.LANCZOS).tobytes(),
                    f'Recovered resource pixels: {name}')
        else:
            original = rgba(root / 'data/gfx' / (name + '.png'))
            alpha = original.getchannel('A').resize(tile.size, Image.Resampling.BILINEAR)
            require(tile.getchannel('A').tobytes() == alpha.tobytes(), f'Constrained resource alpha: {name}')
        tiles.append(tile)
    for level, record in enumerate(levels):
        atlas = atlas_image(pack, record, (2048 >> level, 2304 >> level))
        for i, source in enumerate(tiles):
            tile = np.asarray(source.resize((source.width >> level, source.height >> level), Image.Resampling.LANCZOS))
            x = (i % 8) * (256 >> level) + (32 >> level)
            y = (i // 8) * (256 >> level) + (32 >> level)
            height, width = tile.shape[:2]
            require(np.array_equal(atlas[y:y + height, x:x + width], tile), f'Resource placement: {level}/{i}')
            require(np.array_equal(atlas[y:y + height, x - 1], tile[:, 0]) and
                    np.array_equal(atlas[y:y + height, x + width], tile[:, -1]),
                    f'Resource border: {level}/{i}')
    print('PASS: 65 resource frames, recovered pixels/constrained alpha and four padded atlas levels')


def topology(root):
    source = (root / 'src/map/MapTerrain.cpp').read_text()
    entries = re.findall(r'\{\s*(\d+),\s*(\d+)\s*\},?\s*// ([HSE]), ([HSE]), ([HSE]), ([HSE])', source)
    result = {}
    for start, count, *corners in entries:
        if set(corners) in ({'H', 'S'}, {'S', 'E'}) or len(set(corners)) == 1:
            for i in range(int(start), int(start) + int(count)):
                result[i] = corners
    require(len(result) == 272, 'Expected 272 connected legacy terrain frames')
    return result


def validate_terrain(root, pack, manifest):
    topo = topology(root)
    levels = manifest['terrain_atlas']['levels']
    require(len(levels) == 4, 'Expected four terrain atlas levels')
    joins = 0
    for level, record in enumerate(levels):
        atlas = atlas_image(pack, record, (4096 >> level, 4352 >> level))
        slot, border, size = 256 >> level, 64 >> level, 128 >> level
        tiles, corners = {}, {}
        for i, symbols in topo.items():
            x, y = (i % 16) * slot, (i // 16) * slot
            tile = atlas[y + border:y + border + size, x + border:x + border + size]
            require(np.all(atlas[y + border:y + border + size, x:x + border] == tile[:, :1]) and
                    np.all(atlas[y + border:y + border + size, x + border + size:x + slot] == tile[:, -1:]),
                    f'Terrain extrusion: {level}/{i}')
            tiles[i] = tile
            for symbol, pixel in zip(symbols, (tile[0, 0], tile[0, -1], tile[-1, 0], tile[-1, -1])):
                if symbol in corners:
                    require(np.array_equal(corners[symbol], pixel), f'Terrain corner: {level}/{i}/{symbol}')
                else:
                    corners[symbol] = pixel
        for i, a in tiles.items():
            c = topo[i]
            for j, b in tiles.items():
                other = topo[j]
                if (c[1], c[3]) == (other[0], other[2]):
                    require(np.array_equal(a[:, -1], b[:, 0]), f'Horizontal terrain join: {level}/{i}/{j}')
                    joins += 1
                if (c[2], c[3]) == (other[0], other[1]):
                    require(np.array_equal(a[-1], b[0]), f'Vertical terrain join: {level}/{i}/{j}')
                    joins += 1
    print(f'PASS: {joins} terrain joins, all corner junctions and padded borders through four mip levels')


def validate_water(pack):
    water = rgba(pack / 'water0.png')
    require(water.getchannel('A').getextrema() == (255, 255), 'Water must be opaque')
    for level in range(4):
        pixels = np.asarray(water.resize((water.width >> level, water.height >> level), Image.Resampling.BOX))
        require(np.array_equal(pixels[0], pixels[-1]) and np.array_equal(pixels[:, 0], pixels[:, -1]),
                f'Periodic water edges: {level}')
    print('PASS: opaque periodic water through four mip levels')


def validate_export(root, pack, manifest, exported):
    """Check shipped naming, decoded alpha/geometry and audited encoding identity."""
    sys.path.insert(0, str(ROOT))
    from tools.package_assets import image_recipe

    audit = json.loads(exported.with_suffix('.json').read_text())
    require(audit['optimized'], 'Original-byte measurement exports are not runtime artwork')
    records = {record['source']: record for record in audit['files']}
    require(len(records) == len(audit['files']), 'Duplicate exported source records')
    images = [layer for frame in manifest['frames'] for layer in frame['layers']]
    images += [level for key in ('terrain_atlas', 'resource_atlas') for level in manifest[key]['levels']]
    for image in images:
        name = image['file']
        relative = 'data/highres/v1/' + name
        record = records[relative]
        expected = str(Path(relative).with_suffix('.webp'))
        require(record['output'] == expected, f'Runtime filename: {relative}')
        path = exported / expected
        require(digest(pack / name) == record['source_sha256'] == image['sha256'], f'Export source hash: {name}')
        require(digest(path) == record['output_sha256'], f'Export output hash: {name}')
        recipe = image_recipe(lossy=audit['lossy_images'])
        require(all(record['recipe'].get(key) == value for key, value in recipe.items()),
                f'Encoding policy: {name}')
        with Image.open(path) as decoded:
            require(decoded.format == 'WEBP', f'Runtime format: {name}')
            source = rgba(pack / name)
            decoded = decoded.convert('RGBA')
            require(decoded.size == source.size, f'Runtime geometry: {name}')
            require(decoded.getchannel('A').tobytes() == source.getchannel('A').tobytes(), f'Runtime alpha: {name}')
            if not record['lossy']:
                require(decoded.tobytes() == source.tobytes(), f'Lossless runtime pixels: {name}')
    expected_index = re.sub(r'\.png(?=\s|$)', '.webp', (pack / 'frames.txt').read_text())
    require((exported / 'data/highres/v1/frames.txt').read_text() == expected_index, 'Runtime frame index differs')
    require(not list((exported / 'data/highres/v1').glob('*.png')), 'Source PNGs leaked into the runtime pack')
    for name in ('manifest.json', 'README.md'):
        require(not (exported / 'data/highres/v1' / name).exists(), f'Source metadata shipped: {name}')
    print(f'PASS: {len(images)} WebP layers/atlases; shared encoding, hashes, alpha, geometry and runtime index')


def validate(root=ROOT, exported=None):
    pack = root / 'data/highres/v1'
    manifest = json.loads((pack / 'manifest.json').read_text())
    frames = validate_frames(root, pack, manifest)
    validate_resources(root, pack, manifest, frames)
    validate_terrain(root, pack, manifest)
    validate_water(pack)
    if exported is not None:
        validate_export(root, pack, manifest, Path(exported).resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--export', type=Path, help='Also verify a tools/package_assets.py WebP runtime tree and audit')
    args = parser.parse_args()
    try:
        validate(exported=args.export)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Artwork validation failed: {error}\n')


if __name__ == '__main__':
    main()
