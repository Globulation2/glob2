"""Check the resolved deployment storage contract, including optional workers."""
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BlobMountTests(unittest.TestCase):
    def test_every_blob_writer_uses_its_writable_shared_volume(self):
        result = subprocess.run(
            ['docker', 'compose', '--env-file', os.devnull, '-f',
             str(ROOT / 'deploy/compose.yaml'), '--profile', '*', 'config', '--format', 'json'],
            env={**os.environ, 'POSTGRES_PASSWORD': 'fixture', 'GLOB2_ENV_FILE': os.devnull},
            check=True, capture_output=True, text=True)
        services = json.loads(result.stdout)['services']
        writers = set()
        for name, service in services.items():
            mounts = [m for m in service.get('volumes', []) if m.get('source') == 'blobs']
            if not mounts:
                continue
            writers.add(name)
            with self.subTest(service=name):
                self.assertEqual(len(mounts), 1)
                self.assertFalse(mounts[0].get('read_only', False))
                self.assertEqual(service['environment'].get('BLOB_DIR'), mounts[0]['target'])
        self.assertTrue({'platform-api', 'ai-terrain-worker', 'ai-building-worker',
                         'ai-map-worker', 'ai-music-worker', 'skin-render-worker'} <= writers)


if __name__ == '__main__':
    unittest.main()
