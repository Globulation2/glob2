#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render stationary turns from frozen original poses using Blender 2.34.

Run prepare, render (with the legacy renderer's libraries on LD_LIBRARY_PATH),
then install. Outputs append to the stock atlas; movement art is never replaced.
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from render import (
    ROOT, SETS, TURN_BASE, TURN_FRAMES, UNIT_HD_PIXEL_SIZE, prepare, schema_and_scene,
)

SOURCE_ROOT = ROOT / 'content-src/gfx/originals/units'
RECEIPT_VERSION = 1


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    """Publish metadata atomically; an interrupted render remains resumable."""
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        try:
            output.write((json.dumps(value, indent=2) + '\n').encode())
            output.close()
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)


def turn_layout():
    """Canonical identities and geometry; work manifests cannot redefine them."""
    records = []
    source_hashes = {source: digest((SOURCE_ROOT / source).read_bytes())
                     for _, source, _, _, _ in SETS}
    for mode in ('native', 'hd'):
        for clip, (name, source, _, native_size, shadow) in enumerate(SETS):
            size = 40 if name == 'explorer' else native_size
            for pose in range(TURN_FRAMES):
                tag = f'{name}-{pose:02d}'
                for role, frame in [('team', 1)] + ([('base', 65)] if shadow else []):
                    records.append(dict(
                        mode=mode, scene=f'{mode}/scenes/{tag}.blend',
                        output=f'{mode}/rendered/{tag}/{frame:04d}.png', frame=frame,
                        id=f'unit{TURN_BASE + clip * TURN_FRAMES + pose}', role=role,
                        logicalSize=size, pixelSize=UNIT_HD_PIXEL_SIZE if mode == 'hd' else size,
                        animation=name, pose=pose, source=source,
                        source_sha256=source_hashes[source],
                    ))
    return records


def load_records(work):
    """Reject incomplete, edited, moved or stale preparation before any writes."""
    validate_work(work)
    records = json.loads((work / 'turns.json').read_text())
    expected = turn_layout()
    if not isinstance(records, list) or len(records) != len(expected):
        raise ValueError('Incomplete turn render inventory')
    by_identity = {}
    for record in records:
        if not isinstance(record, dict):
            raise ValueError('Invalid turn render record')
        key = (record.get('mode'), record.get('id'), record.get('role'))
        if any(not isinstance(value, str) for value in key):
            raise ValueError('Invalid turn render identity')
        if key in by_identity:
            raise ValueError('Duplicate turn render record')
        by_identity[key] = record
    scene_hashes = {}
    for canonical in expected:
        key = (canonical['mode'], canonical['id'], canonical['role'])
        record = by_identity.get(key)
        if record is None or set(record) != set(canonical) | {'scene_sha256'}:
            raise ValueError('Invalid turn render inventory')
        if any(type(record[field]) is not type(value) or record[field] != value
               for field, value in canonical.items()):
            raise ValueError('Turn layout or source differs: ' + canonical['output'])
        scene = work / canonical['scene']
        if scene not in scene_hashes:
            scene_hashes[scene] = digest(scene.read_bytes())
        # Team and shadow records refer to the same immutable prepared scene.
        if scene_hashes[scene] != record['scene_sha256']:
            raise ValueError('Prepared scene hash differs: ' + canonical['scene'])
    return [by_identity[(r['mode'], r['id'], r['role'])] for r in expected]


def validate_work(work):
    """Temporary authoring output must not overlap retained sources or assets."""
    work = work.resolve()
    protected = [SOURCE_ROOT, ROOT / 'content/data/gfx', ROOT / 'content/data/highres/v1',
                 ROOT / 'content-src/gfx/production']
    for path in protected:
        if work.is_relative_to(path.resolve()) or path.resolve().is_relative_to(work):
            raise ValueError('Turn work directory overlaps retained artwork: ' + str(path))


def image_bytes(path, record):
    from PIL import Image
    data = path.read_bytes()
    with Image.open(io.BytesIO(data)) as image:
        size = record['pixelSize']
        if image.format != 'PNG' or image.mode != 'RGBA' or image.size != (size, size):
            raise ValueError('Invalid turn image: ' + str(path))
        image.load()  # Detect truncated files before publication, not just header errors.
    return data


def verified_output(work, record, renderer_hash=None):
    """Return pixels only when their receipt binds the source, scene and output."""
    output = work / record['output']
    receipt = json.loads(output.with_suffix('.receipt.json').read_text())
    expected = dict(version=RECEIPT_VERSION, source_sha256=record['source_sha256'],
                    scene_sha256=record['scene_sha256'])
    if (not isinstance(receipt, dict)
            or set(receipt) != set(expected) | {'renderer_sha256', 'output_sha256'}
            or any(receipt[key] != value for key, value in expected.items())):
        raise ValueError('Render receipt differs: ' + str(output))
    for key in ('renderer_sha256', 'output_sha256'):
        value = receipt[key]
        if not isinstance(value, str) or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('Invalid render receipt digest: ' + str(output))
    if renderer_hash is not None and receipt['renderer_sha256'] != renderer_hash:
        raise ValueError('Renderer hash differs: ' + str(output))
    data = image_bytes(output, record)
    if digest(data) != receipt['output_sha256']:
        raise ValueError('Rendered image hash differs: ' + str(output))
    return data, receipt


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
    validate_work(work)
    records = turn_layout()
    scene_hashes = {}
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
                scene_hashes[str(scene.relative_to(work))] = digest(data)
    for record in records:
        record['scene_sha256'] = scene_hashes[record['scene']]
    write_json(work / 'turns.json', records)
    return records


def render_turns(work, blender, workers):
    if workers < 1:
        raise ValueError('Render workers must be positive')
    records = load_records(work)
    blender_hash = digest(blender.read_bytes())

    def run(record):
        output = work / record['output']
        try:
            verified_output(work, record, blender_hash)
            return
        except (OSError, ValueError):
            pass  # A missing, stale or corrupt result must be rendered again.
        output.parent.mkdir(parents=True, exist_ok=True)
        frame = str(record['frame'])
        # A successful process must produce a fresh image. Never let an old
        # output satisfy a failed/no-output rerender, even when it exits zero.
        with tempfile.TemporaryDirectory(dir=work, prefix='.render-') as directory:
            # Blender 2.34 has no output-path CLI option. Patch only RenderData.pic
            # in a disposable copy; retain the prepared scene's hash as identity.
            scene = bytearray((work / record['scene']).read_bytes())
            if digest(scene) != record['scene_sha256']:
                raise ValueError('Prepared scene changed before rendering: ' + record['scene'])
            schema, _, render_offset = schema_and_scene(scene)
            pos, length = schema['RenderData']['pic']
            render_path = (directory + '/').encode() + b'\0'
            if len(render_path) > length:
                raise ValueError('Temporary render path too long')
            scene[render_offset+pos:render_offset+pos+length] = render_path.ljust(length, b'\0')
            temporary_scene = Path(directory) / 'render.blend'
            temporary_scene.write_bytes(scene)
            with output.with_suffix('.log').open('w') as stream:
                subprocess.run([
                    str(blender), '-b', str(temporary_scene), '-s', frame, '-e', frame, '-a',
                ], stdout=stream, stderr=subprocess.STDOUT, check=True)
            fresh = Path(directory) / f"{record['frame']:04d}.png"
            data = image_bytes(fresh, record)
            fresh.replace(output)
        receipt = dict(version=RECEIPT_VERSION, source_sha256=record['source_sha256'],
                       scene_sha256=record['scene_sha256'], renderer_sha256=blender_hash,
                       output_sha256=digest(data))
        write_json(output.with_suffix('.receipt.json'), receipt)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        for count, _ in enumerate(executor.map(run, records), 1):
            if count % 32 == 0:
                print(f'Rendered {count}/{len(records)} turn layers', flush=True)


def destination_metadata(highres):
    """Check the existing pack before writing either native or HD assets."""
    manifest = json.loads((highres / 'manifest.json').read_text())
    if not isinstance(manifest, dict) or manifest.get('version') != 1 or not isinstance(manifest.get('frames'), list):
        raise ValueError('Unsupported destination provenance')
    ids = [frame.get('id') for frame in manifest['frames'] if isinstance(frame, dict)]
    if (len(ids) != len(manifest['frames'])
            or any(not isinstance(ident, str) for ident in ids)
            or len(set(ids)) != len(ids)):
        raise ValueError('Invalid destination frame identities')
    lines = (highres / 'frames.txt').read_text().splitlines()
    if not lines or lines[0] != 'GLOB2_HIGHRES 1':
        raise ValueError('Unsupported destination frame index')
    rows = [line.split() for line in lines[1:] if line.strip()]
    if any(len(row) != 6 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise ValueError('Invalid destination frame index rows')
    if {row[0] for row in rows} != set(ids):
        raise ValueError('Destination index and provenance differ')
    return manifest, rows


def install_turns(work, native, highres):
    records = load_records(work)
    manifest, rows = destination_metadata(highres)
    work_path = work.resolve()
    destinations = (native.resolve(), highres.resolve())
    if (destinations[0].is_relative_to(destinations[1])
            or destinations[1].is_relative_to(destinations[0])
            or any(work_path.is_relative_to(path) or path.is_relative_to(work_path) for path in destinations)):
        raise ValueError('Turn work and destination directories must be separate')
    metadata, validated = {}, []
    renderer_hash = None
    # Hold verified PNG bytes until all inputs pass, avoiding a second read of
    # mutable staging files after the validation barrier.
    for record in records:
        data, receipt = verified_output(work, record, renderer_hash)
        renderer_hash = receipt['renderer_sha256']
        filename = record['id'] + ('r' if record['role'] == 'team' else '') + '.png'
        validated.append((record['mode'], filename, data))
        if record['mode'] != 'hd':
            continue
        source = SOURCE_ROOT / record['source']
        shadow = next(shadow for name, _, _, _, shadow in SETS if name == record['animation'])
        entry = metadata.setdefault(record['id'], dict(
            id=record['id'], width=record['logicalSize'], height=record['logicalSize'], scale=0,
            recipe='recovered original: Blender 2.34 frozen-pose turn; 32 headings', layers=[],
            sources=[dict(path=str(source.relative_to(ROOT)), sha256=record['source_sha256'])],
            animation=record['animation'], turn_pose=record['pose'],
            render_settings=dict(
                hd_pixel_size=UNIT_HD_PIXEL_SIZE, heading_degrees=-record['pose'] * 360 / TURN_FRAMES,
                gait_frame=1, shadow_frame=65 if shadow else None,
                renderer_sha256=renderer_hash,
                camera_lens_scale=0.8 if record['animation'] == 'explorer' else 1,
            ),
        ))
        entry['layers'].append(dict(file=filename, role=record['role'], sha256=digest(data),
                                    scene_sha256=record['scene_sha256']))
    ids = set(metadata)
    manifest['frames'] = [frame for frame in manifest['frames'] if frame['id'] not in ids] + list(metadata.values())
    lines = ['GLOB2_HIGHRES 1'] + [' '.join(row) for row in rows if row[0] not in ids]
    for entry in metadata.values():
        layers = {layer['role']: layer['file'] for layer in entry['layers']}
        lines.append(f"{entry['id']} {entry['width']} {entry['height']} 0 {layers.get('base', '-')} {layers['team']}")
    for mode, filename, data in validated:
        destination = highres if mode == 'hd' else native
        (destination / filename).write_bytes(data)
    write_json(highres / 'manifest.json', manifest)
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
        install_turns(work, ROOT / 'content/data/gfx', ROOT / 'content/data/highres/v1')


if __name__ == '__main__':
    main()
