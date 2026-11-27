"""Independent runtime checks reject bad WebP pixels, indices and encoding policy."""

import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from tools.artwork import validate_runtime as validator
from tools.package_assets import image_recipe


class ExportValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.pack = self.root / 'source'
        self.pack.mkdir()
        self.export = self.root / 'runtime'
        self.hd = self.export / 'content/data/highres/v1'
        self.hd.mkdir(parents=True)
        image = Image.new('RGBA', (4, 4), (10, 50, 120, 127))
        image.save(self.pack / 'inn0b0.png')
        image.save(self.hd / 'inn0b0.webp', lossless=True, exact=True)
        index = 'GLOB2_HIGHRES 1\ninn0b0 1 1 4 inn0b0.png -\n'
        (self.pack / 'frames.txt').write_text(index)
        (self.hd / 'frames.txt').write_text(index.replace('.png', '.webp'))
        self.layer = dict(file='inn0b0.png', sha256=validator.digest(self.pack / 'inn0b0.png'))
        self.manifest = dict(frames=[dict(layers=[self.layer])], terrain_atlas=dict(levels=[]),
                             resource_atlas=dict(levels=[]))
        self.record = dict(source='content/data/highres/v1/inn0b0.png', output='content/data/highres/v1/inn0b0.webp',
                           source_sha256=self.layer['sha256'], output_sha256=validator.digest(self.hd / 'inn0b0.webp'),
                           recipe=image_recipe(lossy=False), lossy=False)
        self.audit = dict(optimized=True, lossy_images=False, files=[self.record])
        self.write_audit()

    def write_audit(self):
        self.export.with_suffix('.json').write_text(json.dumps(self.audit))

    def check(self):
        validator.validate_export(self.root, self.pack, self.manifest, self.export)

    def test_exact_webp_and_rewritten_index_pass(self):
        self.check()

    def test_generated_terrain_can_have_multiple_outputs_per_catalog_source(self):
        self.audit['files'] += [dict(source='content/data/terrain/tileset.json',
                                     output=f'content/data/terrain/compiled/texture{i}.webp') for i in range(2)]
        self.write_audit()
        self.check()
        self.audit['files'].append(dict(self.record))
        self.write_audit()
        with self.assertRaisesRegex(ValueError, 'Duplicate exported HD'):
            self.check()

    def test_source_png_runtime_name_and_stale_index_fail(self):
        self.record['output'] = 'content/data/highres/v1/inn0b0.png'
        self.write_audit()
        with self.assertRaisesRegex(ValueError, 'Runtime filename'):
            self.check()
        self.record['output'] = 'content/data/highres/v1/inn0b0.webp'
        self.write_audit()
        (self.hd / 'frames.txt').write_text((self.pack / 'frames.txt').read_text())
        with self.assertRaisesRegex(ValueError, 'index differs'):
            self.check()

    def test_wrong_alpha_fails_even_with_valid_output_hash(self):
        Image.new('RGBA', (4, 4), (10, 50, 120, 255)).save(self.hd / 'inn0b0.webp', lossless=True)
        self.record['output_sha256'] = validator.digest(self.hd / 'inn0b0.webp')
        self.write_audit()
        with self.assertRaisesRegex(ValueError, 'Runtime alpha'):
            self.check()

    def test_changed_rgb_fails_for_lossless_but_is_allowed_by_lossy_policy(self):
        Image.new('RGBA', (4, 4), (20, 50, 120, 127)).save(self.hd / 'inn0b0.webp', lossless=True)
        self.record['output_sha256'] = validator.digest(self.hd / 'inn0b0.webp')
        self.write_audit()
        with self.assertRaisesRegex(ValueError, 'Lossless runtime pixels'):
            self.check()
        self.audit['lossy_images'] = True
        self.record['recipe'] = image_recipe(lossy=True)
        self.record['lossy'] = True
        self.write_audit()
        self.check()

    def test_unstandardized_recipe_and_leaked_png_fail(self):
        self.record['recipe']['lossy_quality'] = 80
        self.write_audit()
        with self.assertRaisesRegex(ValueError, 'Encoding policy'):
            self.check()
        self.record['recipe'] = image_recipe(lossy=False)
        self.write_audit()
        (self.hd / 'leftover.png').write_bytes(b'PNG source')
        with self.assertRaisesRegex(ValueError, 'PNGs leaked'):
            self.check()


if __name__ == '__main__':
    unittest.main()


class ShadowLayerValidationTests(unittest.TestCase):
    """A frame's shadow layer is its own `<frame>s` frame: black, at least the frame's size."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.gfx = self.root / 'content/data/gfx'
        self.pack = self.root / 'content/data/highres/v1'
        self.gfx.mkdir(parents=True)
        self.pack.mkdir(parents=True)
        Image.new('RGBA', (2, 2), (200, 10, 10, 255)).save(self.gfx / 'inn0b0.png')
        self.shadow((3, 3), (0, 0, 0, 90))
        self.frames = {'inn0b0': dict(width=2, height=2, layers=[dict(role='base', file='inn0b0.png')]),
                       'inn0b0s': dict(width=3, height=3, recipe='cast ground shadow v1: block',
                                       layers=[dict(role='base', file='inn0b0s.png')])}

    def shadow(self, size, colour):
        Image.new('RGBA', size, colour).save(self.gfx / 'inn0b0s.png')
        Image.new('RGBA', (size[0] * 4, size[1] * 4), colour).save(self.pack / 'inn0b0s.png')

    def test_black_shadow_reaching_past_its_frame_passes(self):
        self.assertEqual(validator.validate_shadows(self.root, self.pack, self.frames), 1)

    def test_unregistered_coloured_or_small_shadows_fail(self):
        with self.assertRaisesRegex(ValueError, 'without HD frames'):
            validator.validate_shadows(self.root, self.pack, {'inn0b0': self.frames['inn0b0']})
        self.shadow((3, 3), (5, 0, 0, 90))
        with self.assertRaisesRegex(ValueError, 'not black'):
            validator.validate_shadows(self.root, self.pack, self.frames)
        self.frames['inn0b0s'].update(width=1)
        with self.assertRaisesRegex(ValueError, 'smaller'):
            validator.validate_shadows(self.root, self.pack, self.frames)
