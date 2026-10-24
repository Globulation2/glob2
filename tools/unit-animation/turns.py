#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render stationary turns from frozen original poses using Blender 2.34.

Run prepare, render (with the legacy renderer's libraries on LD_LIBRARY_PATH),
then install. Outputs append to the stock atlas; movement art is never replaced.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from render import ROOT, SETS, UNIT_HD_PIXEL_SIZE, prepare, schema_and_scene

TURN_BASE = 1792
TURN_FRAMES = 32


def freeze_heading(data, name, angle):
    """Set only the heading IPO to a constant; keep gait and pass IPOs intact.

    Legacy rotation IPO ordinates are in units of ten degrees, not radians.
    Freeze the gait by rendering frame 1 (frame 65 for its shadow), while the
    heading's cyclic offset must be disabled for both passes to face identically.
    """
    original = bytes(data)
    schema, blocks, _ = schema_and_scene(data)
    pointers = {struct.unpack_from('<I', data, pos - 12)[0]: pos for _, pos, _ in blocks}
    def field(pos, kind, key, fmt):
        return struct.unpack_from('<' + fmt, data, pos + schema[kind][key][0])[0]
    offset = schema['Object']['id'][0] + schema['ID']['name'][0]
    target = b'OBEmpty' if name == 'explorer' else b'OBRotEmpty'
    objects = [pos for code, pos, _ in blocks if code == b'OB\0\0'
               and data[pos+offset:pos+offset+24].split(b'\0')[0] == target]
    if len(objects) != 1:
        raise ValueError('Expected one heading object: ' + name)
    ipo = pointers[field(objects[0], 'Object', 'ipo', 'I')]
    curve = pointers[field(ipo, 'Ipo', 'curve', 'I')]
    while field(curve, 'IpoCurve', 'adrcode', 'h') != 9:
        curve = pointers[field(curve, 'IpoCurve', 'next', 'I')]
    vertices = field(curve, 'IpoCurve', 'totvert', 'h')
    bezier = pointers[field(curve, 'IpoCurve', 'bezt', 'I')]
    stride = max(pos + size for pos, size in schema['BezTriple'].values())
    origin = struct.unpack_from('<f', data, bezier + 16)[0]
    permitted = set()
    # Hold all key handles at the same heading. This also avoids interpolating
    # through the legacy scene's alternating gait halves between directions.
    for key in range(vertices):
        for handle in range(3):
            pos = bezier + key * stride + handle * 12 + 4
            struct.pack_into('<f', data, pos, origin - angle / 10)
            permitted.update(range(pos, pos + 4))
    for key, value in [('ipo', 0), ('extrap', 0)]:
        pos = curve + schema['IpoCurve'][key][0]
        struct.pack_into('<h', data, pos, value)
        permitted.update(range(pos, pos + 2))
    if any(a != b and i not in permitted for i, (a, b) in enumerate(zip(original, data))):
        raise ValueError('Unexpected change outside heading curve')
    return data


def prepare_turns(work, render_root):
    records = []
    for hd in (False, True):
        mode = 'hd' if hd else 'native'
        folder = work / mode
        prepare(folder / 'sources', render_root + '/' + mode + '/unused', 1,
                fixed_size=UNIT_HD_PIXEL_SIZE if hd else None)
        scenes = folder / 'scenes'
        scenes.mkdir(parents=True, exist_ok=True)
        for clip, (name, source, _, size, shadow) in enumerate(SETS):
            template = (folder / 'sources' / (name + '.blend')).read_bytes()
            schema, _, render_offset = schema_and_scene(template)
            # The explorer's spread wings exceed its old canvas between the
            # eight authored headings. Add a four-pixel gutter on each side,
            # widening the camera by the same factor to preserve world size.
            if name == 'explorer':
                original_size, size = size, 40
                template = bytearray(template)
                _, blocks, _ = schema_and_scene(template)
                camera = next(pos for code, pos, _ in blocks if code == b'CA\0\0')
                pos = camera + schema['Camera']['lens'][0]
                lens = struct.unpack_from('<f', template, pos)[0]
                struct.pack_into('<f', template, pos, lens * original_size / size)
                if not hd:
                    for key in ('xsch', 'ysch'):
                        struct.pack_into('<h', template, render_offset + schema['RenderData'][key][0], size)
            for pose in range(TURN_FRAMES):
                tag = f'{name}-{pose:02d}'
                data = freeze_heading(bytearray(template), name, pose * 360 / TURN_FRAMES)
                path = (render_root.rstrip('/') + '/' + mode + '/rendered/' + tag + '/').encode() + b'\0'
                pos, length = schema['RenderData']['pic']
                if len(path) > length:
                    raise ValueError('Render path too long')
                data[render_offset+pos:render_offset+pos+length] = path.ljust(length, b'\0')
                scene = scenes / (tag + '.blend')
                scene.write_bytes(data)
                for role, frame in [('team', 1)] + ([('base', 65)] if shadow else []):
                    records.append(dict(mode=mode, scene=str(scene.relative_to(work)),
                        output=f'{mode}/rendered/{tag}/{frame:04d}.png', frame=frame,
                        id=f'unit{TURN_BASE + clip * TURN_FRAMES + pose}', role=role,
                        logicalSize=size, pixelSize=UNIT_HD_PIXEL_SIZE if hd else size,
                        animation=name, pose=pose, source=source,
                        scene_sha256=hashlib.sha256(data).hexdigest()))
    (work / 'turns.json').write_text(json.dumps(records, indent=2) + '\n')
    return records


def render_turns(work, blender, workers):
    records = json.loads((work / 'turns.json').read_text())
    blender_hash = hashlib.sha256(blender.read_bytes()).hexdigest()
    def run(record):
        output = work / record['output']
        marker = output.with_suffix('.sha256')
        if hashlib.sha256((work / record['scene']).read_bytes()).hexdigest() != record['scene_sha256']:
            raise ValueError('Prepared scene hash differs: ' + record['scene'])
        signature = record['scene_sha256'] + ':' + blender_hash
        if output.exists() and marker.exists() and marker.read_text() == signature:
            return
        output.parent.mkdir(parents=True, exist_ok=True)
        log = output.with_suffix('.log')
        frame = str(record['frame'])
        with log.open('w') as stream:
            subprocess.run([str(blender), '-b', str(work / record['scene']), '-s', frame,
                            '-e', frame, '-a'], stdout=stream, stderr=subprocess.STDOUT, check=True)
        if not output.is_file():
            raise ValueError('Renderer did not write: ' + str(output))
        marker.write_text(signature)
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        for count, _ in enumerate(executor.map(run, records), 1):
            if count % 32 == 0:
                print(f'Rendered {count}/{len(records)} turn layers', flush=True)


def install_turns(work, native, highres):
    from PIL import Image
    records = json.loads((work / 'turns.json').read_text())
    # Require complete coverage, and validate everything before any install write.
    expected = {(mode, f'unit{TURN_BASE + clip * TURN_FRAMES + pose}', role)
                for mode in ('native', 'hd') for clip, (_, _, _, _, shadow) in enumerate(SETS)
                for pose in range(TURN_FRAMES) for role in (['team', 'base'] if shadow else ['team'])}
    if len(records) != len(expected) or {(r['mode'], r['id'], r['role']) for r in records} != expected:
        raise ValueError('Incomplete turn render inventory')
    metadata = {}
    for record in records:
        path = work / record['output']
        with Image.open(path) as image:
            if image.mode != 'RGBA' or image.size != (record['pixelSize'], record['pixelSize']):
                raise ValueError('Invalid turn image: ' + str(path))
        if record['mode'] != 'hd':
            continue
        source = ROOT / 'datasrc/gfx/originals/units' / record['source']
        entry = metadata.setdefault(record['id'], dict(id=record['id'], width=record['logicalSize'],
            height=record['logicalSize'], scale=0,
            recipe='recovered original: Blender 2.34 frozen-pose turn; 32 headings',
            layers=[], sources=[dict(path=str(source.relative_to(ROOT)), sha256=hashlib.sha256(source.read_bytes()).hexdigest())],
            animation=record['animation'], turn_pose=record['pose'],
            render_settings=dict(hd_pixel_size=128, heading_degrees=-record['pose'] * 360 / TURN_FRAMES,
                                 gait_frame=1, shadow_frame=65 if any(name == record['animation'] and shadow for name, _, _, _, shadow in SETS) else None)))
        entry['layers'].append(dict(file=record['id'] + ('r' if record['role'] == 'team' else '') + '.png',
                                   role=record['role'], sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    manifest = json.loads((highres / 'manifest.json').read_text())
    ids = set(metadata)
    manifest['frames'] = [frame for frame in manifest['frames'] if frame['id'] not in ids] + list(metadata.values())
    lines = [line for line in (highres / 'frames.txt').read_text().splitlines() if line.split()[0] not in ids]
    for entry in metadata.values():
        layers = {layer['role']: layer['file'] for layer in entry['layers']}
        lines.append(f"{entry['id']} {entry['width']} {entry['height']} 0 {layers.get('base', '-')} {layers['team']}")
    for record in records:
        destination = highres if record['mode'] == 'hd' else native
        filename = record['id'] + ('r' if record['role'] == 'team' else '') + '.png'
        (destination / filename).write_bytes((work / record['output']).read_bytes())
    (highres / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (highres / 'frames.txt').write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'render', 'install'])
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--blender', type=Path)
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    work = args.work.resolve()
    if args.command == 'prepare':
        prepare_turns(work, str(work))
    elif args.command == 'render':
        if not args.blender or args.workers < 1:
            parser.error('render needs --blender and a positive --workers')
        render_turns(work, args.blender.resolve(), args.workers)
    else:
        install_turns(work, ROOT / 'data/gfx', ROOT / 'data/highres/v1')


if __name__ == '__main__':
    main()
