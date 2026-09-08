#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare Blender 2.34 scene copies, then install and validate 32-pose sprites.

No Blender Python API is required: only RenderData timing and output fields are
patched, using the source file's embedded DNA schema. The explorer camera is
aligned to its shipped sprites; animation curves remain byte-identical.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
SETS = [
    ('explorer', 'explorer.blend', 0, 32, False),
    ('worker-walk', 'glob-worker-walk.blend', 64, 38, True),
    ('worker-swim', 'glob-worker-swim.blend', 128, 38, False),
    ('worker-harvest', 'glob-worker-harvest.blend', 192, 38, True),
    ('warrior-walk', 'glob-warrior-walk.blend', 256, 40, True),
    ('warrior-swim', 'glob-warrior-swim.blend', 320, 40, False),
    ('warrior-fight', 'glob-warrior-fight.blend', 384, 40, True),
]


def schema_and_scene(data):
    if data[:12] != b'BLENDER_v234':
        raise ValueError('Expected an uncompressed, little-endian, 32-bit Blender 2.34 source')
    pos, blocks = 12, []
    while pos + 20 <= len(data):
        code, size, address, schema, count = struct.unpack_from('<4sIIII', data, pos)
        blocks.append((code, pos + 20, size))
        pos += 20 + size
        if code == b'ENDB':
            break
    dna = next(data[p:p+n] for c, p, n in blocks if c == b'DNA1')
    pos = 8

    def integer():
        nonlocal pos
        value, = struct.unpack_from('<I', dna, pos)
        pos += 4
        return value

    def strings(count):
        nonlocal pos
        result = []
        for _ in range(count):
            end = dna.index(0, pos)
            result.append(dna[pos:end].decode('ascii'))
            pos = end + 1
        pos = (pos + 3) & ~3
        return result

    names = strings(integer())
    assert dna[pos:pos+4] == b'TYPE'
    pos += 4
    types = strings(integer())
    assert dna[pos:pos+4] == b'TLEN'
    pos += 4
    lengths = struct.unpack_from('<' + 'H' * len(types), dna, pos)
    pos = (pos + len(types) * 2 + 3) & ~3
    assert dna[pos:pos+4] == b'STRC'
    pos += 4
    structures = {}
    for _ in range(integer()):
        type_index, count = struct.unpack_from('<HH', dna, pos)
        pos += 4
        fields, offset = {}, 0
        for _ in range(count):
            field_type, field_name = struct.unpack_from('<HH', dna, pos)
            pos += 4
            name = names[field_name]
            size = 4 if '*' in name else lengths[field_type]
            for dimension in re.findall(r'\[(\d+)\]', name):
                size *= int(dimension)
            fields[name.split('[')[0].lstrip('*')] = (offset, size)
            offset += size
        assert offset == lengths[type_index], types[type_index]
        structures[types[type_index]] = fields
    scenes = [p for c, p, n in blocks if c == b'SC\0\0']
    if len(scenes) != 1:
        raise ValueError('Expected exactly one scene')
    return structures, blocks, scenes[0] + structures['Scene']['r'][0]


def prepare(out, render_root, samples):
    if out.resolve() == (ROOT / 'datasrc/gfx/globules').resolve():
        raise ValueError('Prepared scenes must not overwrite the original Blender sources')
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, source, base, size, shadow in SETS:
        source_path = ROOT / 'datasrc/gfx/globules' / source
        data = bytearray(source_path.read_bytes())
        original = bytes(data)
        structures, blocks, render_offset = schema_and_scene(data)
        fields = structures['RenderData']

        def get(field, fmt):
            return struct.unpack_from('<' + fmt, data, render_offset + fields[field][0])[0]

        def put(field, fmt, value):
            struct.pack_into('<' + fmt, data, render_offset + fields[field][0], value)

        assert get('xsch', 'h') == size and get('ysch', 'h') == size
        assert get('framapto', 'h') % samples == 0
        put('framapto', 'h', get('framapto', 'h') // samples)
        put('framelen', 'f', get('framelen', 'f') / samples)
        # Frame samples=the original frame 1; retain all original direction and
        # pass switches on the exact same timeline, including fractional poses.
        first = samples
        last = (128 if shadow else 64) * samples + samples - 1
        put('sfra', 'h', first)
        put('efra', 'h', last)
        output = (render_root.rstrip('/') + '/' + name + '/').encode('utf-8') + b'\0'
        offset, length = fields['pic']
        if len(output) > length:
            raise ValueError('Render output path exceeds the Blender field length')
        data[render_offset+offset:render_offset+offset+length] = output.ljust(length, b'\0')
        permitted = set()
        for field in ['framapto', 'framelen', 'sfra', 'efra', 'pic']:
            offset, length = fields[field]
            permitted.update(range(render_offset+offset, render_offset+offset+length))
        # The explorer's shipped sprite is half a pixel left/down from the
        # source camera projection. Restore that alignment in the scene copy.
        if name == 'explorer':
            name_offset = structures['Object']['id'][0] + structures['ID']['name'][0]
            objects = [pos for code, pos, length in blocks if code == b'OB\0\0'
                       and data[pos+name_offset:pos+name_offset+24].split(b'\0')[0] == b'OBCamera']
            cameras = [pos for code, pos, length in blocks if code == b'CA\0\0']
            assert len(objects) == len(cameras) == 1
            loc_offset = objects[0] + structures['Object']['loc'][0]
            rotation_offset = objects[0] + structures['Object']['rot'][0]
            loc = struct.unpack_from('<3f', data, loc_offset)
            x, y, z = struct.unpack_from('<3f', data, rotation_offset)
            sx, sy, sz, cx, cy, cz = math.sin(x), math.sin(y), math.sin(z), math.cos(x), math.cos(y), math.cos(z)
            right = (cz*cy, sz*cy, -sy)
            up = (cz*sy*sx-sz*cx, sz*sy*sx+cz*cx, cy*sx)
            backward = (cz*sy*cx+sz*sx, sz*sy*cx-cz*sx, cy*cx)
            lens, = struct.unpack_from('<f', data, cameras[0] + structures['Camera']['lens'][0])
            depth = sum(a*b for a, b in zip(loc, backward))
            half_pixel = 0.5 * 32.0 * depth / (size * lens)
            calibrated = [loc[i] + half_pixel * (right[i] + up[i]) for i in range(3)]
            struct.pack_into('<3f', data, loc_offset, *calibrated)
            permitted.update(range(loc_offset, loc_offset + 12))
        assert all(a == b or i in permitted for i, (a, b) in enumerate(zip(original, data)))
        (out / (name + '.blend')).write_bytes(data)
        manifest.append(dict(name=name, source=source, source_sha256=hashlib.sha256(original).hexdigest(),
                             legacy_base=base, size=size, shadow=shadow,
                             first=first, last=last, samples_per_original_frame=samples,
                             camera_pixel_offset=[-0.5, 0.5] if name == 'explorer' else [0, 0]))
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def collect(rendered, destination, samples, reference):
    from PIL import Image
    import numpy as np
    if destination.resolve() == reference.resolve():
        raise ValueError('Collect into a separate staging directory, not the reference assets')
    destination.mkdir(parents=True, exist_ok=True)
    report = []
    for name, source, base, size, shadow in SETS:
        differences = []
        for index in range(64 * samples):
            for suffix, pass_offset in [('r', 0)] + ([('', 64 * samples)] if shadow else []):
                src = rendered / name / ('%04d.png' % (samples + index + pass_offset))
                with Image.open(src) as image:
                    assert image.mode == 'RGBA' and image.size == (size, size), src
                    new = np.asarray(image).copy()
                dest = destination / ('unit%d%s.png' % (base * samples + index, suffix))
                # Retain original encoded pixels. No image processing or changes
                # to the old renderer's alpha, team colors, or shadow coverage.
                dest.write_bytes(src.read_bytes())
                if index % samples == 0:
                    old_file = reference / ('unit%d%s.png' % (base + index // samples, suffix))
                    with Image.open(old_file) as image:
                        old = np.asarray(image.convert('RGBA'))
                    error = np.abs(new.astype(float) - old.astype(float))
                    differences.append(dict(frame=index // samples, layer=suffix or 'shadow',
                                            mean_error=float(error.mean()), max_error=int(error.max()),
                                            alpha_mean_error=float(error[:, :, 3].mean()),
                                            alpha_exact=bool(np.array_equal(new[:, :, 3], old[:, :, 3])),
                                            exact=bool(np.array_equal(new, old))))
        report.append(dict(name=name, comparisons=differences))
    (destination / 'comparison-report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def install(staged, destination):
    from PIL import Image
    expected = {}
    for name, source, base, size, shadow in SETS:
        for index in range(256):
            for suffix in ['r'] + ([''] if shadow else []):
                expected['unit%d%s.png' % (base * 4 + index, suffix)] = size
    supplied = {p.name for p in staged.glob('unit*.png')}
    if supplied != set(expected):
        raise ValueError('Staged filenames do not match the complete 2816-file layout')
    for filename, size in expected.items():
        with Image.open(staged / filename) as image:
            if image.mode != 'RGBA' or image.size != (size, size):
                raise ValueError('Invalid staged image: ' + filename)
    for filename in expected:
        (destination / filename).write_bytes((staged / filename).read_bytes())
    # Remove old optional layers whose indices now belong to other animations.
    for path in destination.glob('unit*.png'):
        if re.fullmatch(r'unit\d+r?\.png', path.name) and path.name not in expected:
            path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare')
    prep.add_argument('--output', type=Path, required=True)
    prep.add_argument('--render-root', required=True, help='Output path as seen by Blender')
    prep.add_argument('--samples', type=int, choices=[1, 4], default=4)
    coll = sub.add_parser('collect')
    coll.add_argument('--rendered', type=Path, required=True)
    coll.add_argument('--output', type=Path, required=True)
    coll.add_argument('--reference', type=Path, default=ROOT / 'data/gfx')
    coll.add_argument('--samples', type=int, choices=[1, 4], default=4)
    inst = sub.add_parser('install')
    inst.add_argument('--staged', type=Path, required=True)
    inst.add_argument('--destination', type=Path, default=ROOT / 'data/gfx')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.output, args.render_root, args.samples)
    elif args.command == 'collect':
        collect(args.rendered, args.output, args.samples, args.reference)
    else:
        install(args.staged, args.destination)


if __name__ == '__main__':
    main()
