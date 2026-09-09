# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('unit_render', Path(__file__).with_name('render.py'))
render = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render)


class PipelineTest(unittest.TestCase):
    def test_all_sources_prepare(self):
        with tempfile.TemporaryDirectory() as root:
            for samples in (1, 4):
                output = Path(root) / str(samples)
                render.prepare(output, '/work/render', samples)
                self.assertEqual(len(list(output.glob('*.blend'))), 7)
                for scene in output.glob('*.blend'):
                    _, _, offset = render.schema_and_scene(scene.read_bytes())
                    self.assertGreater(offset, 0)

    def test_rejects_original_source_as_output(self):
        with self.assertRaises(ValueError):
            render.prepare(render.ROOT / 'datasrc/gfx/globules', '/work/render', 4)

    def test_rejects_wrong_file_version(self):
        with self.assertRaises(ValueError):
            render.schema_and_scene(b'BLENDER-v300')

    def test_pack_round_trips_every_frame(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            staged, destination = root / 'staged', root / 'destination'
            staged.mkdir()
            destination.mkdir()
            frames = {}
            for name, source, base, size, shadow in render.SETS:
                for index in range(64):
                    for suffix in ['r'] + ([''] if shadow else []):
                        pixels = bytes((base + index + suffix.count('r') + position) % 256
                                       for position in range(size * size * 4))
                        image = Image.frombytes('RGBA', (size, size), pixels)
                        filename = 'unit%d%s.png' % (base + index, suffix)
                        image.save(staged / filename)
                        frames[filename] = image.tobytes()
            render.pack(staged, destination, 1)
            unpacked = {}
            for line in (destination / 'unit.sheet').read_text().splitlines():
                if line.startswith('#'):
                    continue
                filename, layer, first, count, width, height = line.split()
                first, count, width, height = int(first), int(count), int(width), int(height)
                with Image.open(destination / filename) as sheet:
                    self.assertEqual(sheet.width % width, 0)
                    columns = sheet.width // width
                    self.assertGreaterEqual(sheet.height, -(-count // columns) * height)
                    for index in range(count):
                        left, top = (index % columns) * width, (index // columns) * height
                        tile = sheet.crop((left, top, left + width, top + height))
                        name = 'unit%d%s.png' % (first + index, 'r' if layer == 'rotated' else '')
                        unpacked[name] = tile.tobytes()
            self.assertEqual(unpacked, frames)

    def test_rejects_incomplete_install_without_writes(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            staged, destination = root / 'staged', root / 'destination'
            staged.mkdir()
            destination.mkdir()
            icon = destination / 'unitmini0.png'
            icon.write_bytes(b'unchanged')
            with self.assertRaises(ValueError):
                render.install(staged, destination)
            self.assertEqual(icon.read_bytes(), b'unchanged')
            self.assertEqual(len(list(destination.iterdir())), 1)


if __name__ == '__main__':
    unittest.main()
