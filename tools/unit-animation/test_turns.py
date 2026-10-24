# SPDX-License-Identifier: GPL-3.0-or-later
"""Stationary turn asset contracts; no Blender executable needed."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from PIL import Image
import turns


class TurnTest(unittest.TestCase):
    def test_all_originals_prepare_without_changes(self):
        originals = {source: hashlib.sha256((turns.ROOT / 'datasrc/gfx/originals/units' / source).read_bytes()).hexdigest()
                     for _, source, _, _, _ in turns.SETS}
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            records = turns.prepare_turns(work, '/work')
            self.assertEqual(len(records), 704)
            for record in records:
                data = (work / record['scene']).read_bytes()
                schema, blocks, _ = turns.schema_and_scene(data)
                # Preparing a second heading must be idempotent: no retained
                # cyclic offset and no accidental additive rotation per pass.
                self.assertEqual(turns.freeze_heading(bytearray(data), record['animation'], 0), data)
                # All body/material animation blocks stay byte-identical to
                # the calibrated source. Only one IPO/Bezier block may change.
                source = (work / record['mode'] / 'sources' / (record['animation'] + '.blend')).read_bytes()
                changed = [code for code, pos, size in blocks if data[pos:pos+size] != source[pos:pos+size]]
                self.assertTrue(all(code in (b'SC\0\0', b'DATA', b'CA\0\0') for code in changed), changed)
        for source, digest in originals.items():
            self.assertEqual(hashlib.sha256((turns.ROOT / 'datasrc/gfx/originals/units' / source).read_bytes()).hexdigest(), digest)

    def test_install_validates_complete_set_before_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory) / 'work'
            records = turns.prepare_turns(work, '/work')
            native, hd = Path(directory) / 'native', Path(directory) / 'hd'
            native.mkdir(); hd.mkdir()
            (native / 'unit0r.png').write_bytes(b'movement')
            (hd / 'manifest.json').write_text(json.dumps(dict(version=1, frames=[])))
            (hd / 'frames.txt').write_text('GLOB2_HIGHRES 1\n')
            for record in records[:-1]:
                output = work / record['output']
                output.parent.mkdir(parents=True, exist_ok=True)
                Image.new('RGBA', (record['pixelSize'], record['pixelSize'])).save(output)
            with self.assertRaises(FileNotFoundError):
                turns.install_turns(work, native, hd)
            self.assertEqual(list(native.iterdir()), [native / 'unit0r.png'])
            record = records[-1]
            Image.new('RGBA', (record['pixelSize'], record['pixelSize'])).save(work / record['output'])
            turns.install_turns(work, native, hd)
            self.assertEqual((native / 'unit0r.png').read_bytes(), b'movement')
            self.assertEqual(len(list(native.glob('*.png'))), 353)
            self.assertEqual(len(json.loads((hd / 'manifest.json').read_text())['frames']), 224)
            self.assertEqual(len((hd / 'frames.txt').read_text().splitlines()), 225)
            # Reinstallation updates instead of duplicating registered frames.
            turns.install_turns(work, native, hd)
            self.assertEqual(len(json.loads((hd / 'manifest.json').read_text())['frames']), 224)


if __name__ == '__main__':
    unittest.main()
