"""Browser relay origins remain explicit during a hostname transition."""
import json
import os
from pathlib import Path
import subprocess
import shutil
import unittest

ROOT = Path(__file__).resolve().parents[2]


def compose_available():
    if not shutil.which('docker'):
        return False
    try:
        return subprocess.run(['docker', 'compose', 'version'],
                              capture_output=True, timeout=5).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


@unittest.skipUnless(compose_available(), 'Docker Compose is not installed')
class RelayOriginTests(unittest.TestCase):
    def resolved(self, **settings):
        env = {key: value for key, value in os.environ.items()
               if key not in ('GLOB2_PUBLIC_ORIGIN', 'GLOB2_RELAY_ALLOWED_ORIGINS')}
        env.update(POSTGRES_PASSWORD='fixture', GLOB2_ENV_FILE=os.devnull, **settings)
        result = subprocess.run(
            ['docker', 'compose', '--env-file', os.devnull, '-f',
             str(ROOT / 'deploy/compose.yaml'), 'config', '--format', 'json'],
            env=env, check=True, capture_output=True, text=True)
        return json.loads(result.stdout)['services']['relay']['environment']['GLOB2_RELAY_ALLOWED_ORIGINS']

    def test_defaults_follow_the_canonical_origin(self):
        self.assertEqual(self.resolved(), 'https://localhost:8443')
        self.assertEqual(self.resolved(GLOB2_PUBLIC_ORIGIN='https://app.glob2.online'),
                         'https://app.glob2.online')
        self.assertEqual(self.resolved(GLOB2_PUBLIC_ORIGIN='https://app.glob2.online',
                                      GLOB2_RELAY_ALLOWED_ORIGINS=''),
                         'https://app.glob2.online')

    def test_migration_can_allow_both_origins(self):
        origins = 'https://app.glob2.online,https://app.glob2online.com'
        self.assertEqual(self.resolved(GLOB2_PUBLIC_ORIGIN='https://app.glob2.online',
                                      GLOB2_RELAY_ALLOWED_ORIGINS=origins), origins)


if __name__ == '__main__':
    unittest.main()
