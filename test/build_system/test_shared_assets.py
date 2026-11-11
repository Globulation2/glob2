"""Contracts for sharing runtime-asset exports between checkouts (buildsys/shared_assets.py)."""
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
import dev_store  # noqa: E402
import shared_assets  # noqa: E402


def checkout(path):
    path.mkdir()
    subprocess.run(['git', 'init', '-q', str(path)], check=True)
    (path / 'a.txt').write_text('alpha')
    (path / 'b.txt').write_text('beta')
    subprocess.run(['git', '-C', str(path), 'add', '.'], check=True)
    return path


class SharedAssetsTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.base = Path(self.directory.name)
        self.environment = patch.dict(os.environ, {'GLOB2_DEV_HOME': str(self.base / 'store'), 'GLOB2_DEV_MODE': 'shared'})
        self.environment.start()

    def tearDown(self):
        dev_store.finish()
        self.environment.stop()
        self.directory.cleanup()

    def test_key_follows_content_and_options_not_location(self):
        first, second = checkout(self.base / 'one'), checkout(self.base / 'two')
        key = lambda root, extra=('x',): shared_assets.content_key(root, [root / 'a.txt', root / 'b.txt'], list(extra))
        self.assertEqual(key(first), key(second))
        self.assertNotEqual(key(first), key(first, ('y',)))
        (second / 'b.txt').write_text('changed')  # modified tracked file
        self.assertNotEqual(key(first), key(second))
        (second / 'b.txt').write_text('beta')
        (second / 'c.txt').write_text('untracked')
        self.assertNotEqual(key(second), shared_assets.content_key(second, [second / 'a.txt', second / 'c.txt'], ['x']))

    def test_second_checkout_clones_the_published_export(self):
        root = checkout(self.base / 'one')
        calls = []

        def produce(output):
            calls.append(output)
            (output / 'data').mkdir(parents=True)
            (output / 'data/file.webp').write_bytes(b'encoded')
            output.with_suffix('.json').write_text(json.dumps({'policy': 'runtime-assets-v1', 'files': []}))

        first = self.base / 'build1/runtime-assets'
        self.assertFalse(shared_assets.export(root, first, 'k1', produce))
        second = self.base / 'build2/runtime-assets'
        self.assertTrue(shared_assets.export(root, second, 'k1', produce))
        self.assertEqual(len(calls), 1)
        self.assertEqual((second / 'data/file.webp').read_bytes(), b'encoded')
        self.assertEqual(second.with_suffix('.json').read_text(), first.with_suffix('.json').read_text())
        (second / 'data/file.webp').write_bytes(b'edited in one build')
        third = self.base / 'build3/runtime-assets'
        shared_assets.export(root, third, 'k1', produce)
        self.assertEqual((third / 'data/file.webp').read_bytes(), b'encoded')  # the store was not modified


if __name__ == '__main__':
    unittest.main()
