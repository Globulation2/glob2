"""The shipped colony paint layout stays attached across all source animations."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / 'data/skins/colony-v1'
DESIGNER = ROOT / 'platform/apps/web/public/skins/models'
# Which fitted format animates each unit clip by default, and its header size.
UNIT_FORMATS = {
    'worker-walk': ('shapes', 'GSB1', 32), 'worker-swim': ('shapes', 'GSB1', 32),
    'worker-harvest': ('shapes', 'GSB1', 32), 'warrior-walk': ('shapes', 'GSB1', 32),
    'warrior-swim': ('shapes', 'GSB1', 32), 'warrior-fight': ('shapes', 'GSB1', 32),
    'explorer-fly': ('rigs', 'GSR1', 28),
}


class ColonySkinAssetsTest(unittest.TestCase):
    def test_camera_metadata_is_bound_and_native_constants_match(self):
        header = (ROOT / 'src/online/SkinViewTransforms.h').read_text()
        for path in FOLDER.glob('*.gsk'):
            sidecar = path.with_suffix('.view.json')
            view = json.loads(sidecar.read_text())
            self.assertEqual(view['version'], 1)
            self.assertEqual(view['meshSha256'], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(sidecar.read_bytes(), (DESIGNER / sidecar.name).read_bytes())
            if path.stem.startswith('swarm'):
                shape = path.stem.removeprefix('swarm-') if path.stem != 'swarm' else 'classic'
                line = next(line for line in header.splitlines() if line.endswith('// ' + shape))
                actual = [float(value) for value in re.findall(r'([-+\d.]+e[-+]\d+)f', line)]
                expected = view['clipToModel'] + view['modelToClip'] + view['normalToModel']
                self.assertEqual(len(actual), len(expected))
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b, places=8)
                self.assertEqual(view['pivot'], [0, 0, 0])

    def test_unit_rigs_are_accepted_hash_bound_and_preserve_paint_topology(self):
        manifest = json.loads((FOLDER / 'manifest.json').read_text())
        self.assertEqual(set(manifest['rigs']), {'explorer-fly.gsr'})
        self.assertEqual(set(manifest['shapes']), {name + '.gsb' for name, (t, _, _) in UNIT_FORMATS.items() if t == 'shapes'})
        for name, (table, magic, header) in UNIT_FORMATS.items():
            with self.subTest(name):
                record = manifest[table][name + ('.gsb' if table == 'shapes' else '.gsr')]
                self.assertEqual(record['format'], magic)
                self.assertTrue(record['accepted'], 'rigs are the default; accept every installed one')
                data = (FOLDER / record['file']).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
                self.assertEqual(data, (DESIGNER / record['file']).read_bytes())
                for source, digest in record['sources'].items():
                    self.assertEqual(hashlib.sha256((ROOT / source).read_bytes()).hexdigest(), digest, source)
                self.assertEqual(data[:4], magic.encode())
                baked = (FOLDER / (name + '.gsk')).read_bytes()
                vertices, indices, _, size = struct.unpack_from('<4I', baked, 4)
                if magic == 'GSR1':
                    self.assertEqual(struct.unpack_from('<6I', data, 4), (vertices, indices, 4, 1, size, len(data) - header))
                    uv_at, uv_stride = header + 24, 64
                else:
                    shapes, normal_shapes = record['clips'][0]['shapes'], record['clips'][0]['normalShapes']
                    self.assertEqual(struct.unpack_from('<7I', data, 4),
                                     (vertices, indices, shapes, normal_shapes, 1, size, len(data) - header))
                    uv_at, uv_stride = header, 8
                # The same paint coordinates and triangles as the baked clip, so
                # every paint applies to both.
                for vertex in range(vertices):
                    at, baked_at = uv_at + vertex * uv_stride, 20 + vertex * 8
                    self.assertEqual(data[at:at + 8], baked[baked_at:baked_at + 8])
                index_at = header + vertices * uv_stride
                self.assertEqual(data[index_at:index_at + indices * 4], baked[20 + vertices * 8:20 + vertices * 8 + indices * 4])

    def test_material_unwraps_are_shared_by_runtime_and_studio(self):
        manifest = json.loads((FOLDER / 'manifest.json').read_text())
        for model in ('worker', 'warrior'):
            reference = None
            contract = json.loads((FOLDER / (model + '-surface.json')).read_text())
            for name in UNIT_FORMATS:
                if not name.startswith(model + '-'):
                    continue
                record = manifest['meshes'][name + '.gsk']['detailUV']
                data = (FOLDER / record['file']).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
                self.assertEqual(data, (DESIGNER / record['file']).read_bytes())
                magic, count = struct.unpack_from('<4sI', data)
                self.assertEqual(magic, b'GUV1')
                self.assertEqual(count, len(contract['welds']))
                if reference is None:
                    reference = data
                self.assertEqual(data, reference, 'material chart changed between actions')
                uv = [data[8+i*8:16+i*8] for i in range(count)]
                for fold in contract['reflections'].values():
                    for a, b in enumerate(fold):
                        self.assertEqual(uv[a], uv[b], 'material reflection mismatch')

    def test_installed_meshes_match_their_sources_and_paint_contract(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / 'tools/skins/test_export.py'), str(FOLDER)],
            # Every posed triangle now receives deformation checks; allow the
            # complete asset set to finish on slower or concurrently loaded hosts.
            cwd=ROOT, text=True, capture_output=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
