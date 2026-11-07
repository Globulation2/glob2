"""Contracts for the development disk tooling (tools/dev_disk.py, dev_store.require_free_space)."""
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'scons'))
import dev_disk  # noqa: E402
import dev_store  # noqa: E402
import check_artifacts  # noqa: E402


def row(**overrides):
    base = {'merged_into_master': False, 'pr': None, 'dirty': False, 'locked': False}
    base.update(overrides)
    return base


class DevDiskTest(unittest.TestCase):
    def test_finished_requires_merge_or_closed_pr_and_nothing_to_lose(self):
        self.assertTrue(dev_disk.finished(row(merged_into_master=True)))
        self.assertTrue(dev_disk.finished(row(pr={'state': 'MERGED'})))  # squash merge
        self.assertTrue(dev_disk.finished(row(pr={'state': 'CLOSED'})))
        self.assertFalse(dev_disk.finished(row(pr={'state': 'OPEN'})))
        self.assertFalse(dev_disk.finished(row(merged_into_master=True, dirty=True)))
        self.assertFalse(dev_disk.finished(row(merged_into_master=True, locked=True)))

    def test_build_gc_keeps_recent_and_locked_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            for name, age in (('old', 30), ('recent', 1)):
                build = checkout / 'build/linux/client' / name
                build.mkdir(parents=True)
                (build / 'identity.json').write_text(json.dumps({'mode': name}))
                (build / 'big.o').write_bytes(b'x' * 4096)
                stamp = build / '.last-use'
                stamp.touch()
                os.utime(stamp, (time.time() - age * 86400,) * 2)
            rows = dev_disk.builds([checkout])
            self.assertEqual(len(rows), 2)
            removed = dev_disk.gc_builds(rows, 14, apply=True)
            self.assertEqual([Path(r['path']).name for r in removed], ['old'])
            self.assertFalse((checkout / 'build/linux/client/old').exists())
            self.assertTrue((checkout / 'build/linux/client/recent').exists())

    def test_free_space_guard(self):
        with patch.dict(os.environ, {'GLOB2_MIN_FREE_GIB': str(10**9)}):
            with self.assertRaises(SystemExit):
                dev_store.require_free_space(ROOT / 'build' / 'not-yet-created')
        with patch.dict(os.environ, {'GLOB2_MIN_FREE_GIB': '0'}):
            dev_store.require_free_space(ROOT)

    def test_toolchains_no_checkout_selects_are_stale(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            checkout = base / 'checkout'
            for name, text in (('platforms/web/toolchain.json', 'web-v2'), ('platforms/mobile/toolchain.json', 'm'),
                               ('platforms/mobile/android-tools.json', 'a'), ('platforms/mobile/emulator.json', 'e')):
                (checkout / name).parent.mkdir(parents=True, exist_ok=True)
                (checkout / name).write_text(text)
            home = base / 'home'
            current = dev_store.key(checkout, ['platforms/web/toolchain.json'])
            (home / 'toolchains/emscripten' / current).mkdir(parents=True)
            (home / 'toolchains/emscripten/Linux-x86_64-old').mkdir(parents=True)
            used = home / 'toolchains/artifacts/used'
            unused = home / 'toolchains/artifacts/unused'
            used.mkdir(parents=True)
            unused.mkdir(parents=True)
            (home / 'toolchains/emscripten' / current / 'link').symlink_to(used)
            stale = {path.name for path in dev_disk.stale_toolchains([checkout], home)}
            self.assertEqual(stale, {'Linux-x86_64-old', 'unused'})

    def test_artifact_scan(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'shot.ppm').write_bytes(b'P6')
            (root / 'run.log').write_bytes(b'x' * (check_artifacts.LOG_LIMIT + 1))
            (root / 'small.log').write_bytes(b'x')
            (root / 'copied/build').mkdir(parents=True)
            (root / 'copied/build/identity.json').write_text('{}')
            found = sorted((kind, path.name) for kind, path in check_artifacts.scan(root))
            self.assertEqual(found, [('build-tree', 'build'), ('large-log', 'run.log'), ('raw-image', 'shot.ppm')])
            check_artifacts.fix('large-log', root / 'run.log')
            self.assertTrue((root / 'run.log.gz').exists())


if __name__ == '__main__':
    unittest.main()
