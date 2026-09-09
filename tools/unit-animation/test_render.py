# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
from pathlib import Path
import tempfile
import unittest
import struct
import hashlib
import json

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

    def test_fourfold_resolution_keeps_timing(self):
        with tempfile.TemporaryDirectory() as root:
            root=Path(root)
            render.prepare(root/'small','/work/render',4)
            render.prepare(root/'large','/work/render',4,4)
            for name,_,_,size,_ in render.SETS:
                data=(root/'large'/(name+'.blend')).read_bytes()
                schema,_,offset=render.schema_and_scene(data)
                fields=schema['RenderData']
                for field in ('xsch','ysch'):
                    self.assertEqual(struct.unpack_from('<h',data,offset+fields[field][0])[0],size*4)
                small=(root/'small'/(name+'.blend')).read_bytes()
                for field in ('framapto','framelen','sfra','efra'):
                    pos,length=fields[field]
                    self.assertEqual(data[offset+pos:offset+pos+length],small[offset+pos:offset+pos+length])

    def test_highres_layout_and_safe_install(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            staged, destination = root / 'staged', root / 'pack'
            staged.mkdir(); destination.mkdir()
            marker = destination / 'unitmini0.png'
            marker.write_bytes(b'icon')
            (destination / 'frames.txt').write_text('GLOB2_HIGHRES 1\nterrain0 32 32 4 terrain0.png -\n')
            with self.assertRaises(ValueError):
                render.install_highres(staged, destination)
            self.assertEqual(marker.read_bytes(), b'icon')
            files, rows = render.highres_layout()
            self.assertEqual(len(files), 2816)
            self.assertEqual(len(rows), 1792)
            for name, size in files.items():
                Image.new('RGBA', (size, size)).save(staged / name)
            (staged / 'frames.txt').write_text('GLOB2_HIGHRES 1\n' + '\n'.join(rows) + '\n')
            metadata = []
            for row in rows:
                ident, width, height, scale, base, team = row.split()
                metadata.append(dict(id=ident, width=int(width), height=int(height), scale=int(scale),
                    layers=[dict(file=name, role=role, sha256=hashlib.sha256((staged / name).read_bytes()).hexdigest())
                    for role, name in [('base', base), ('team', team)] if name != '-']))
            (staged / 'manifest.json').write_text(json.dumps(dict(version=1, frames=metadata)))
            render.install_highres(staged, destination)
            self.assertEqual(marker.read_bytes(), b'icon')
            installed = (destination / 'frames.txt').read_text().splitlines()
            self.assertEqual(installed[1], 'terrain0 32 32 4 terrain0.png -')
            self.assertEqual(installed[2:], rows)
            self.assertEqual((destination / 'unit256r.png').read_bytes(), (staged / 'unit256r.png').read_bytes())

    def test_rejects_original_source_as_output(self):
        with self.assertRaises(ValueError):
            render.prepare(render.ROOT / 'datasrc/gfx/originals/units', '/work/render', 4)

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
