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
