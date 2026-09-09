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


def prepare(out, render_root, samples, resolution_scale=1):
    if out.resolve() == (ROOT / 'datasrc/gfx/originals/units').resolve():
        raise ValueError('Prepared scenes must not overwrite the original Blender sources')
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, source, base, size, shadow in SETS:
        source_path = ROOT / 'datasrc/gfx/originals/units' / source
        data = bytearray(source_path.read_bytes())
        original = bytes(data)
        structures, blocks, render_offset = schema_and_scene(data)
        fields = structures['RenderData']

        def get(field, fmt):
            return struct.unpack_from('<' + fmt, data, render_offset + fields[field][0])[0]

        def put(field, fmt, value):
            struct.pack_into('<' + fmt, data, render_offset + fields[field][0], value)

        assert get('xsch', 'h') == size and get('ysch', 'h') == size
        put('xsch', 'h', size * resolution_scale)
        put('ysch', 'h', size * resolution_scale)
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
        for field in ['xsch', 'ysch', 'framapto', 'framelen', 'sfra', 'efra', 'pic']:
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
                             legacy_base=base, size=size, resolution_scale=resolution_scale, shadow=shadow,
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


def highres_layout():
    """Logical frame geometry stays native; texture dimensions are fourfold."""
    files, rows = {}, []
    for name, source, base, size, shadow in SETS:
        for index in range(256):
            frame = base * 4 + index
            regular = 'unit%d.png' % frame if shadow else '-'
            colored = 'unit%dr.png' % frame
            rows.append('unit%d %d %d 4 %s %s' % (frame, size, size, regular, colored))
            files[colored] = size * 4
            if shadow:
                files[regular] = size * 4
    return files, rows


def collect_highres(rendered, destination, reference):
    from PIL import Image
    import numpy as np
    if destination.resolve() == reference.resolve():
        raise ValueError('Collect HD frames separately from the native reference')
    destination.mkdir(parents=True, exist_ok=True)
    comparisons, metadata = [], []
    for name, source, base, size, shadow in SETS:
        source_path = ROOT / 'datasrc/gfx/originals/units' / source
        source_hash = hashlib.sha256(source_path.read_bytes()).hexdigest()
        for index in range(256):
            layers = []
            for suffix, offset in [('r', 0)] + ([('', 256)] if shadow else []):
                src = rendered / name / ('%04d.png' % (4 + index + offset))
                filename = 'unit%d%s.png' % (base * 4 + index, suffix)
                with Image.open(src) as image:
                    if image.mode != 'RGBA' or image.size != (size * 4, size * 4):
                        raise ValueError('Invalid HD render: ' + str(src))
                    # Compare at native size only for validation; install original PNG bytes.
                    reduced = np.asarray(image.resize((size, size), Image.Resampling.BOX)).astype(float)
                with Image.open(reference / filename) as image:
                    if image.mode != 'RGBA' or image.size != (size, size):
                        raise ValueError('Expected the native 32-pose reference: ' + filename)
                    old = np.asarray(image).astype(float)
                error = np.abs(reduced - old)
                comparisons.append(dict(animation=name, file=filename,
                    mean_error=float(error.mean()), alpha_mean_error=float(error[:, :, 3].mean()),
                    alpha_max_error=float(error[:, :, 3].max())))
                png = src.read_bytes()
                (destination / filename).write_bytes(png)
                layers.append(dict(file=filename, role='team' if suffix else 'base',
                    sha256=hashlib.sha256(png).hexdigest(), source_sha256=source_hash,
                    logical_width=size, logical_height=size,
                    original_sha256=hashlib.sha256((reference / filename).read_bytes()).hexdigest()))
            metadata.append(dict(id='unit%d' % (base * 4 + index), width=size, height=size, scale=4,
                recipe='recovered original: Blender 2.34 render; original rig and layers; 32 poses per direction',
                layers=layers, sources=[dict(path=str(source_path.relative_to(ROOT)), sha256=source_hash)],
                animation=name, direction=index // 32, pose=index % 32,
                render_settings=dict(samples_per_original_frame=4, resolution_scale=4,
                    team_frame=4 + index, shadow_frame=260 + index if shadow else None,
                    camera_pixel_offset=[-0.5, 0.5] if name == 'explorer' else [0, 0])))
    files, rows = highres_layout()
    (destination / 'frames.txt').write_text('GLOB2_HIGHRES 1\n' + '\n'.join(rows) + '\n')
    (destination / 'comparison-report.json').write_text(json.dumps(comparisons, indent=2) + '\n')
    (destination / 'manifest.json').write_text(json.dumps(dict(version=1, frames=metadata), indent=2) + '\n')
    return comparisons


def install_highres(staged, destination):
    from PIL import Image
    import shutil
    files, rows = highres_layout()
    supplied = {p.name for p in staged.glob('unit*.png')}
    if supplied != set(files):
        raise ValueError('HD staging must contain all 2816 unit image layers')
    if (staged / 'frames.txt').read_text() != 'GLOB2_HIGHRES 1\n' + '\n'.join(rows) + '\n':
        raise ValueError('HD staging manifest does not match the 1792-frame layout')
    staged_metadata = json.loads((staged / 'manifest.json').read_text())
    if staged_metadata['version'] != 1 or len(staged_metadata['frames']) != 1792 or {f['id'] for f in staged_metadata['frames']} != {'unit%d' % i for i in range(1792)}:
        raise ValueError('Invalid HD provenance frame coverage')
    expected_frames = {row.split()[0]: row.split()[1:] for row in rows}
    for frame in staged_metadata['frames']:
        width, height, scale, regular, colored = expected_frames[frame['id']]
        expected_layers = {'team': colored}
        if regular != '-':
            expected_layers['base'] = regular
        if ((frame['width'], frame['height'], frame['scale']) != (int(width), int(height), int(scale))
                or len(frame['layers']) != len(expected_layers)
                or {layer['role']: layer['file'] for layer in frame['layers']} != expected_layers):
            raise ValueError('HD provenance layout mismatch: ' + frame['id'])
    hashes = {layer['file']: layer['sha256'] for frame in staged_metadata['frames'] for layer in frame['layers']}
    if set(hashes) != set(files):
        raise ValueError('Invalid HD provenance layer coverage')
    for name, size in files.items():
        if hashlib.sha256((staged / name).read_bytes()).hexdigest() != hashes[name]:
            raise ValueError('HD provenance hash mismatch: ' + name)
        with Image.open(staged / name) as image:
            if image.mode != 'RGBA' or image.size != (size, size):
                raise ValueError('Invalid HD texture: ' + name)
    # Preserve any building, terrain, resource or icon entries in an existing pack.
    manifest = destination / 'frames.txt'
    other_rows = []
    if manifest.exists():
        lines = manifest.read_text().splitlines()
        if not lines or lines[0] != 'GLOB2_HIGHRES 1':
            raise ValueError('Unsupported destination pack')
        other_rows = [line for line in lines[1:]
                      if line.strip() and not re.fullmatch(r'unit\d+', line.split()[0])]
    metadata_path = destination / 'manifest.json'
    metadata = json.loads(metadata_path.read_text()) if metadata_path.exists() else dict(version=1, frames=[])
    if metadata['version'] != 1:
        raise ValueError('Unsupported destination provenance')
    metadata['frames'] = [f for f in metadata['frames'] if not re.fullmatch(r'unit\d+', f['id'])] + staged_metadata['frames']
    destination.mkdir(parents=True, exist_ok=True)
    for name in sorted(files):
        temp = destination / (name + '.tmp')
        shutil.copyfile(staged / name, temp)
        temp.replace(destination / name)
    metadata_path.write_text(json.dumps(metadata, indent=2) + '\n')
    temporary = destination / 'frames.txt.tmp'
    temporary.write_text('GLOB2_HIGHRES 1\n' + '\n'.join(other_rows + rows) + '\n')
    temporary.replace(manifest)
    for path in destination.glob('unit*.png'):
        if re.fullmatch(r'unit\d+r?\.png', path.name) and path.name not in files:
            path.unlink()
    if destination.resolve() == (ROOT / 'data/highres/v1').resolve():
        (destination / 'README.md').write_text(
            '# High-resolution runtime pack\n\n'
            + str(len(metadata['frames'])) + ' registered frames, including 1,792 unit poses across seven animation sets.\n'
            'Unit textures are rendered at 4× width and height from the preserved Blender originals, without AI.\n'
            'Native-resolution sprites remain in `data/gfx`; logical geometry, team colors and animation timing are preserved.\n\n'
            'Approved inputs live in `datasrc/gfx/production`; package them with `tools/artwork/package_runtime.py`.\n'
            'See `manifest.json` for all frame/layer/source hashes, `tools/unit-animation/README.md` for unit render recipes,\n'
            'and `datasrc/gfx/RECOVERED-RUNTIME.md` for world-art exports.\n')
        # The established packaging pipeline owns production categories and hashes.
        import importlib.util
        specification = importlib.util.spec_from_file_location('package_runtime', ROOT / 'tools/artwork/package_runtime.py')
        package = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(package)
        package.capture()



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    prep = sub.add_parser('prepare')
    prep.add_argument('--output', type=Path, required=True)
    prep.add_argument('--render-root', required=True, help='Output path as seen by Blender')
    prep.add_argument('--resolution-scale', type=int, choices=[1, 4], default=1)
    prep.add_argument('--samples', type=int, choices=[1, 4], default=4)
    coll = sub.add_parser('collect')
    coll.add_argument('--rendered', type=Path, required=True)
    coll.add_argument('--output', type=Path, required=True)
    coll.add_argument('--reference', type=Path, default=ROOT / 'data/gfx')
    coll.add_argument('--samples', type=int, choices=[1, 4], default=4)
    inst = sub.add_parser('install')
    inst.add_argument('--staged', type=Path, required=True)
    inst.add_argument('--destination', type=Path, default=ROOT / 'data/gfx')
    hd = sub.add_parser('collect-highres')
    hd.add_argument('--rendered', type=Path, required=True)
    hd.add_argument('--output', type=Path, required=True)
    hd.add_argument('--reference', type=Path, default=ROOT / 'data/gfx')
    hi = sub.add_parser('install-highres')
    hi.add_argument('--staged', type=Path, required=True)
    hi.add_argument('--destination', type=Path, default=ROOT / 'data/highres/v1')
    args = parser.parse_args()
    if args.command == 'prepare':
        prepare(args.output, args.render_root, args.samples, args.resolution_scale)
    elif args.command == 'collect-highres':
        collect_highres(args.rendered, args.output, args.reference)
    elif args.command == 'install-highres':
        install_highres(args.staged, args.destination)
    elif args.command == 'collect':
        collect(args.rendered, args.output, args.samples, args.reference)
    else:
        install(args.staged, args.destination)


if __name__ == '__main__':
    main()
