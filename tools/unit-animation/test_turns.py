# SPDX-License-Identifier: GPL-3.0-or-later
"""Turn layout, provenance, failure recovery and installation contracts.

Fixtures use real prepared Blender scenes and small synthetic PNGs. Rendering
itself is mocked so these failure cases do not require the legacy executable.
"""
from contextlib import contextmanager
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image
import turns


class TurnTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.root = Path(cls.directory.name)
        cls.work = cls.root / 'work'
        cls.records = turns.prepare_turns(cls.work, '/work')
        cls.inventory = cls.work / 'turns.json'
        cls.blender = cls.root / 'blender'
        cls.blender.write_bytes(b'fixture renderer')
        cls.renderer_hash = turns.digest(cls.blender.read_bytes())
        for record in cls.records:
            output = cls.work / record['output']
            output.parent.mkdir(parents=True, exist_ok=True)
            Image.new('RGBA', (record['pixelSize'], record['pixelSize']), (30, 80, 150, 255)).save(output)
            turns.write_json(output.with_suffix('.receipt.json'), dict(
                version=turns.RECEIPT_VERSION, source_sha256=record['source_sha256'],
                scene_sha256=record['scene_sha256'], renderer_sha256=cls.renderer_hash,
                output_sha256=turns.digest(output.read_bytes()),
            ))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    @contextmanager
    def changed_file(self, path, data=None):
        original = path.read_bytes() if path.exists() else None
        try:
            if data is not None:
                path.write_bytes(data)
            else:
                path.unlink(missing_ok=True)
            yield
        finally:
            if original is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(original)

    @contextmanager
    def changed_record(self, index, **values):
        records = json.loads(self.inventory.read_text())
        records[index].update(values)
        with self.changed_file(self.inventory, json.dumps(records).encode()):
            yield

    @contextmanager
    def destinations(self):
        with tempfile.TemporaryDirectory(dir=self.root) as directory:
            native, hd = Path(directory) / 'native', Path(directory) / 'hd'
            native.mkdir()
            hd.mkdir()
            (native / 'unit0r.png').write_bytes(b'movement')
            (hd / 'terrain0.png').write_bytes(b'unrelated art')
            (hd / 'manifest.json').write_text(json.dumps(dict(
                version=1, frames=[dict(id='terrain0')], terrain_atlas=dict(retain=True),
            )))
            (hd / 'frames.txt').write_text('GLOB2_HIGHRES 1\n\nterrain0 32 32 4 terrain0.png -\n\n')
            yield native, hd

    def assert_install_rejected(self, exception=ValueError):
        with self.destinations() as (native, hd):
            before = {path.name: path.read_bytes() for path in hd.iterdir()}
            with self.assertRaises(exception):
                turns.install_turns(self.work, native, hd)
            self.assertEqual(list(native.iterdir()), [native / 'unit0r.png'])
            self.assertEqual({path.name: path.read_bytes() for path in hd.iterdir()}, before)

    def test_canonical_coverage_and_source_preservation(self):
        self.assertEqual(len(self.records), 704)
        self.assertEqual(len({r['id'] for r in self.records}), 224)
        self.assertEqual(turns.load_records(self.work), self.records)
        seen = set()
        for record in self.records:
            source_path = turns.SOURCE_ROOT / record['source']
            self.assertEqual(turns.digest(source_path.read_bytes()), record['source_sha256'])
            if record['scene'] in seen:
                continue
            seen.add(record['scene'])
            data = (self.work / record['scene']).read_bytes()
            _, blocks, _ = turns.schema_and_scene(data)
            # Keeping the already fixed heading leaves every source byte intact.
            self.assertEqual(turns.freeze_heading(bytearray(data), record['animation'], 0), data)
            template = self.work / record['mode'] / 'sources' / (record['animation'] + '.blend')
            source = template.read_bytes()
            changed = [code for code, pos, size in blocks if data[pos:pos+size] != source[pos:pos+size]]
            allowed = (b'SC\0\0', b'DATA', b'CA\0\0') if record['animation'] == 'explorer' else (b'SC\0\0', b'DATA')
            self.assertTrue(all(code in allowed for code in changed), changed)

    def test_manifest_cannot_redefine_geometry_identity_or_paths(self):
        changes = dict(pixelSize=1, logicalSize=1, frame=65, pose=1,
                       animation='worker-walk', source='glob-worker-walk.blend',
                       output='../outside.png', scene='../outside.blend',
                       mode='other', id='unit9999', role='base', source_sha256='0' * 64)
        for field, value in changes.items():
            with self.subTest(field=field), self.changed_record(0, **{field: value}):
                self.assert_install_rejected()

    def test_incomplete_duplicate_and_malformed_inventories(self):
        for records in ([], self.records[:-1], self.records + [self.records[0]],
                        [self.records[0]] * len(self.records), {}, [None] * len(self.records)):
            with self.subTest(kind=type(records).__name__, count=len(records)):
                with self.changed_file(self.inventory, json.dumps(records).encode()):
                    self.assert_install_rejected()
        with self.changed_record(0, id=[]):
            self.assert_install_rejected()

    def test_changed_original_and_prepared_scene_are_rejected(self):
        copied = self.root / 'changed-sources'
        shutil.copytree(turns.SOURCE_ROOT, copied)
        source = copied / self.records[0]['source']
        source.write_bytes(source.read_bytes() + b'changed')
        with patch.object(turns, 'SOURCE_ROOT', copied):
            self.assert_install_rejected()
        scene = self.work / self.records[-1]['scene']
        with self.changed_file(scene, scene.read_bytes() + b'changed'):
            self.assert_install_rejected()
        # A shadow record cannot claim a different hash for its team's scene.
        shadow = next(i for i, r in enumerate(self.records) if r['role'] == 'base')
        with self.changed_record(shadow, scene_sha256='0' * 64):
            self.assert_install_rejected()

    def test_missing_image_receipt_and_valid_rgba_corruption_reject_before_writes(self):
        output = self.work / self.records[-1]['output']
        with self.changed_file(output):
            self.assert_install_rejected(FileNotFoundError)
        with self.changed_file(output.with_suffix('.receipt.json')):
            self.assert_install_rejected(FileNotFoundError)
        with self.changed_file(output, output.read_bytes()):
            Image.new('RGBA', (128, 128), (255, 0, 0, 255)).save(output)
            self.assert_install_rejected()

    def test_forged_receipts_cannot_override_image_mode_or_size(self):
        record = self.records[-1]
        output = self.work / record['output']
        receipt = output.with_suffix('.receipt.json')
        for mode, size in [('RGB', 128), ('RGBA', 1)]:
            with self.subTest(mode=mode, size=size):
                with self.changed_file(output, output.read_bytes()), self.changed_file(receipt, receipt.read_bytes()):
                    Image.new(mode, (size, size)).save(output)
                    marker = json.loads(receipt.read_text())
                    marker['output_sha256'] = turns.digest(output.read_bytes())
                    turns.write_json(receipt, marker)
                    self.assert_install_rejected()

    def test_stale_and_mixed_renderer_receipts_are_rejected(self):
        output = self.work / self.records[-1]['output']
        path = output.with_suffix('.receipt.json')
        for field in ('source_sha256', 'scene_sha256', 'renderer_sha256', 'output_sha256'):
            with self.subTest(field=field):
                marker = json.loads(path.read_text())
                marker[field] = '0' * 64
                with self.changed_file(path, json.dumps(marker).encode()):
                    self.assert_install_rejected()
        with self.changed_file(path, b'not JSON'):
            self.assert_install_rejected()

    def test_destination_versions_rows_and_identity_mismatch_reject_before_writes(self):
        with self.destinations() as (native, hd):
            for filename, payload in [('manifest.json', b'{"version":2,"frames":[]}'),
                                      ('frames.txt', b'GLOB2_HIGHRES 2\n'),
                                      ('frames.txt', b'GLOB2_HIGHRES 1\ninvalid\n'),
                                      ('frames.txt', b'GLOB2_HIGHRES 1\n')]:
                with self.subTest(payload=payload), self.changed_file(hd / filename, payload):
                    before = {p.name: p.read_bytes() for p in hd.iterdir()}
                    with self.assertRaises(ValueError):
                        turns.install_turns(self.work, native, hd)
                    self.assertEqual({p.name: p.read_bytes() for p in hd.iterdir()}, before)
                    self.assertEqual(list(native.iterdir()), [native / 'unit0r.png'])

    def test_install_preserves_unrelated_assets_and_is_repeatable(self):
        with self.destinations() as (native, hd):
            turns.install_turns(self.work, native, hd)
            self.assertEqual((native / 'unit0r.png').read_bytes(), b'movement')
            self.assertEqual((hd / 'terrain0.png').read_bytes(), b'unrelated art')
            self.assertEqual(len(list(native.glob('*.png'))), 353)
            metadata = json.loads((hd / 'manifest.json').read_text())
            self.assertEqual(len(metadata['frames']), 225)
            self.assertEqual(metadata['terrain_atlas'], dict(retain=True))
            self.assertEqual(metadata['frames'][1]['width'], 40)  # Explorer gutter is canonical.
            self.assertEqual(metadata['frames'][1]['render_settings']['renderer_sha256'], self.renderer_hash)
            before = (hd / 'manifest.json').read_bytes(), (hd / 'frames.txt').read_bytes()
            turns.install_turns(self.work, native, hd)
            self.assertEqual(((hd / 'manifest.json').read_bytes(), (hd / 'frames.txt').read_bytes()), before)

    def fake_render(self, command, **unused):
        frame = int(command[command.index('-s') + 1])
        scene = Path(command[command.index('-b') + 1]).read_bytes()
        schema, _, offset = turns.schema_and_scene(scene)
        pos, length = schema['RenderData']['pic']
        directory = Path(scene[offset+pos:offset+pos+length].split(b'\0')[0].decode())
        record = self.records[0]
        image = Image.new('RGBA', (record['pixelSize'], record['pixelSize']), (100, 20, 50, 255))
        image.save(directory / f'{frame:04d}.png')

    def test_valid_resume_skips_renderer_but_corrupt_pixels_force_a_fresh_render(self):
        record = self.records[0]
        output = self.work / record['output']
        receipt = output.with_suffix('.receipt.json')
        with patch.object(turns, 'load_records', return_value=[record]), patch.object(turns.subprocess, 'run') as run:
            turns.render_turns(self.work, self.blender, 1)
            run.assert_not_called()
        with self.changed_file(output, b'corrupt'), self.changed_file(receipt, receipt.read_bytes()):
            with patch.object(turns, 'load_records', return_value=[record]):
                with patch.object(turns.subprocess, 'run', side_effect=self.fake_render) as run:
                    turns.render_turns(self.work, self.blender, 1)
                    run.assert_called_once()
            data, marker = turns.verified_output(self.work, record, self.renderer_hash)
            self.assertEqual(turns.digest(data), marker['output_sha256'])

    def test_success_without_output_cannot_bless_an_existing_file(self):
        record = self.records[0]
        output = self.work / record['output']
        receipt = output.with_suffix('.receipt.json')
        original = output.read_bytes()
        with self.changed_file(receipt):
            with patch.object(turns, 'load_records', return_value=[record]), patch.object(turns.subprocess, 'run'):
                with self.assertRaises(FileNotFoundError):
                    turns.render_turns(self.work, self.blender, 1)
            self.assertEqual(output.read_bytes(), original)
            self.assertFalse(receipt.exists())
            self.assertFalse(list(self.work.glob('.render-*')))

    def test_failed_renderer_keeps_previous_output_without_new_receipt(self):
        record = self.records[0]
        output = self.work / record['output']
        receipt = output.with_suffix('.receipt.json')
        original = output.read_bytes()
        failure = subprocess.CalledProcessError(1, ['fixture renderer'])
        with self.changed_file(receipt):
            with patch.object(turns, 'load_records', return_value=[record]):
                with patch.object(turns.subprocess, 'run', side_effect=failure):
                    with self.assertRaises(subprocess.CalledProcessError):
                        turns.render_turns(self.work, self.blender, 1)
            self.assertEqual(output.read_bytes(), original)
            self.assertFalse(receipt.exists())
            self.assertFalse(list(self.work.glob('.render-*')))

    def test_scene_changed_between_preflight_and_worker_is_rejected(self):
        record = self.records[0]
        output = self.work / record['output']
        receipt = output.with_suffix('.receipt.json')
        scene = self.work / record['scene']
        original_pixels = output.read_bytes()
        # Preflight succeeded against the old scene, but another prepare/edit
        # replaces it before this worker opens it. Never attribute new pixels
        # to the old scene identity or invoke Blender on those bytes.
        with self.changed_file(receipt), self.changed_file(scene, scene.read_bytes() + b'changed'):
            with patch.object(turns, 'load_records', return_value=[record]):
                with patch.object(turns.subprocess, 'run') as run:
                    with self.assertRaisesRegex(ValueError, 'changed before rendering'):
                        turns.render_turns(self.work, self.blender, 1)
                    run.assert_not_called()
            self.assertEqual(output.read_bytes(), original_pixels)
            self.assertFalse(receipt.exists())

    def test_prepare_rejects_source_and_asset_overlap(self):
        for path in (turns.ROOT, turns.SOURCE_ROOT, turns.SOURCE_ROOT / 'nested',
                     turns.ROOT / 'data', turns.ROOT / 'data/gfx',
                     turns.ROOT / 'data/highres/v1', turns.ROOT / 'datasrc/gfx/production'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                turns.prepare_turns(path, '/work')

    def test_nonpositive_worker_count_rejected(self):
        for workers in (0, -1):
            with self.subTest(workers=workers), self.assertRaises(ValueError):
                turns.render_turns(self.work, self.blender, workers)


if __name__ == '__main__':
    unittest.main()
