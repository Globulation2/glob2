"""The shipped colony paint layout stays attached across all source animations."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ColonySkinAssetsTest(unittest.TestCase):
    def test_camera_metadata_is_bound_and_native_constants_match(self):
        folder = ROOT / 'data/skins/colony-v1'
        header = (ROOT / 'src/online/SkinViewTransforms.h').read_text()
        for path in folder.glob('*.gsk'):
            sidecar = path.with_suffix('.view.json')
            view = json.loads(sidecar.read_text())
            self.assertEqual(view['version'], 1)
            self.assertEqual(view['meshSha256'], hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertEqual(sidecar.read_bytes(), (ROOT / 'platform/apps/web/public/skins/models' / sidecar.name).read_bytes())
            if path.stem.startswith('swarm'):
                shape = path.stem.removeprefix('swarm-') if path.stem != 'swarm' else 'classic'
                line = next(line for line in header.splitlines() if line.endswith('// ' + shape))
                actual = [float(value) for value in re.findall(r'([-+\d.]+e[-+]\d+)f', line)]
                expected = view['clipToModel'] + view['modelToClip'] + view['normalToModel']
                self.assertEqual(len(actual), len(expected))
                for a, b in zip(actual, expected):
                    self.assertAlmostEqual(a, b, places=8)
                self.assertEqual(view['pivot'], [0, 0, 0])

    def test_experimental_rigs_are_hash_bound_and_preserve_paint_topology(self):
        import struct
        folder = ROOT / 'data/skins/colony-v1'
        manifest = json.loads((folder / 'manifest.json').read_text())
        for name, expected_bones, expected_size in [('worker-walk', 13, 38), ('warrior-walk', 9, 40),
                                                    ('warrior-swim', 9, 40), ('warrior-fight', 9, 40),
                                                    ('explorer-fly', 4, 32)]:
            record = manifest['rigs'][name + '.gsr']
            self.assertEqual(record['format'], 'GSR1')
            self.assertFalse(record['accepted'])
            data = (folder / record['file']).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
            self.assertEqual(data, (ROOT / 'platform/apps/web/public/skins/models' / record['file']).read_bytes())
            for source, digest in record['sources'].items():
                self.assertEqual(hashlib.sha256((ROOT / source).read_bytes()).hexdigest(), digest, source)
            magic, vertices, indices, bones, clips, canvas, length = struct.unpack_from('<4s6I', data)
            self.assertEqual((magic, bones, clips, canvas, length), (b'GSR1', expected_bones, 1, expected_size, len(data)-28))
            baked = (folder / (name + '.gsk')).read_bytes()
            self.assertEqual((vertices, indices), struct.unpack_from('<2I', baked, 4))
            for vertex in range(vertices):
                self.assertEqual(data[28+vertex*64+24:28+vertex*64+32], baked[20+vertex*8:20+vertex*8+8])
            self.assertEqual(data[28+vertices*64:28+vertices*64+indices*4], baked[20+vertices*8:20+vertices*8+indices*4])

    def test_installed_meshes_match_their_sources_and_paint_contract(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / 'tools/skins/test_export.py'),
             str(ROOT / 'data/skins/colony-v1')],
            # Every posed triangle now receives deformation checks; allow the
            # complete asset set to finish on slower or concurrently loaded hosts.
            cwd=ROOT, text=True, capture_output=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
