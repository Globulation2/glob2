"""Approved artwork is validated completely before assembly or capture writes."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.artwork import package_runtime


class ArtworkPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.production = self.root / 'production'
        self.output = self.root / 'source-export'
        self.payload = b'approved image bytes'
        self.hash = hashlib.sha256(self.payload).hexdigest()
        self.manifest = dict(version=1, frames=[dict(id='inn0b0', recipe='current', layers=[
            dict(role='base', file='inn0b0.png', sha256=self.hash)])])
        self.write_pack()

    def write_pack(self):
        files = {'ai-upscaled/inn0b0.png': self.payload,
                 'pack-metadata/manifest.json': json.dumps(self.manifest).encode(),
                 'pack-metadata/frames.txt': b'GLOB2_HIGHRES 1\ninn0b0 3 2 4 inn0b0.png -\n',
                 'pack-metadata/README.md': b'Approved source pack'}
        self.records = []
        for relative, data in files.items():
            path = self.production / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.records.append(dict(runtime=path.name, source=relative,
                                     sha256=hashlib.sha256(data).hexdigest()))
        self.write_index()

    def write_index(self):
        (self.production / 'package.json').write_text(json.dumps(dict(version=1, files=self.records)))

    def test_assembly_preserves_approved_source_bytes(self):
        package_runtime.assemble(self.production, self.output)
        self.assertEqual((self.output / 'inn0b0.png').read_bytes(), self.payload)
        self.assertEqual(len(list(self.output.iterdir())), 4)
        before = {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()}
        package_runtime.assemble(self.production, self.output)
        self.assertEqual(before, {p.name: p.stat().st_mtime_ns for p in self.output.iterdir()})

    def test_bad_hash_fails_before_any_output_write(self):
        (self.production / 'ai-upscaled/inn0b0.png').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'hash differs'):
            package_runtime.assemble(self.production, self.output)
        self.assertFalse(self.output.exists())

    def test_index_rejects_duplicate_paths_traversal_and_wrong_categories(self):
        original = list(self.records)
        cases = [original + [original[0]],
                 [dict(original[0], source='../outside')] + original[1:],
                 [dict(original[0], runtime='..')] + original[1:]]
        for records in cases:
            self.records = records
            self.write_index()
            with self.assertRaises(ValueError):
                package_runtime.validate(self.production)
        self.records = original
        self.write_index()
        self.manifest['frames'][0]['recipe'] = 'soft mask resampling'
        self.write_pack()
        with self.assertRaisesRegex(ValueError, 'category differs'):
            package_runtime.validate(self.production)

    def test_unindexed_files_and_overlapping_output_are_rejected(self):
        extra = self.production / 'unused.png'
        extra.write_bytes(b'obsolete')
        with self.assertRaisesRegex(ValueError, 'Unindexed'):
            package_runtime.assemble(self.production, self.output)
        self.assertFalse(self.output.exists())
        extra.unlink()
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            package_runtime.assemble(self.production, self.production / 'export')

    def test_capture_validates_all_source_hashes_before_production_changes(self):
        package_runtime.assemble(self.production, self.output)
        before = {str(p.relative_to(self.production)): p.read_bytes()
                  for p in self.production.rglob('*') if p.is_file()}
        (self.output / 'inn0b0.png').write_bytes(b'wrong selection')
        with self.assertRaisesRegex(ValueError, 'Source hash differs'):
            package_runtime.capture(self.production, self.output)
        after = {str(p.relative_to(self.production)): p.read_bytes()
                 for p in self.production.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_assembly_rejects_stale_or_symlinked_source_exports(self):
        package_runtime.assemble(self.production, self.output)
        stale = self.output / 'old-frame.png'
        stale.write_bytes(b'obsolete')
        with self.assertRaisesRegex(ValueError, 'Unindexed source export'):
            package_runtime.assemble(self.production, self.output)
        stale.unlink()
        target = self.output / 'inn0b0.png'
        target.unlink()
        target.symlink_to(self.production / 'ai-upscaled/inn0b0.png')
        with self.assertRaisesRegex(ValueError, 'symlinked artwork'):
            package_runtime.assemble(self.production, self.output)

    def test_inventory_rejects_duplicate_or_misnamed_layers_and_unknown_recipes(self):
        layer = self.manifest['frames'][0]['layers'][0]
        self.manifest['frames'][0]['layers'].append(dict(layer))
        with self.assertRaisesRegex(ValueError, 'duplicate layer'):
            package_runtime.inventory(self.manifest)
        self.manifest['frames'][0]['layers'] = [dict(layer, file='other.png')]
        with self.assertRaisesRegex(ValueError, 'filename'):
            package_runtime.inventory(self.manifest)
        self.manifest['frames'][0]['layers'] = [layer]
        self.manifest['frames'][0]['recipe'] = 'unknown'
        with self.assertRaisesRegex(ValueError, 'Unclassified'):
            package_runtime.inventory(self.manifest)


if __name__ == '__main__':
    unittest.main()
