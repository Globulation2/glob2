"""deploy/configure-signin.py: adds the Google provider to instance.yaml without
disturbing the rest of the file, and puts the secret (from stdin) in the env file.

    python3 -m unittest test/deployment/test_configure_signin.py -v

Needs PyYAML (as the script does); skipped without it.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

try:
    import yaml
except ImportError:  # pragma: no cover
    yaml = None

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'deploy/configure-signin.py'
CLIENT = '123456789012-abcdefghijklmnop0123456789abcdef.apps.googleusercontent.com'

INSTANCE = '''# app.glob2online.com staging instance.
name: Globulation 2 (staging)

guests:
  enabled: true

auth:
  # No OIDC providers yet.
  #  - id: google
  #    clientSecretEnv: GOOGLE_CLIENT_SECRET
  providers: []
  local:
    enabled: true
    allowRegistration: true

# Universal links for invite URLs.
appLinks:
  ios:
    appIds: [CL2MNNYQX3.org.globulation2.glob2]
'''


@unittest.skipIf(yaml is None, 'PyYAML is not installed')
class ConfigureSigninTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='signin-'))
        self.addCleanup(shutil.rmtree, self.dir)
        self.instance = self.dir / 'instance.yaml'
        self.instance.write_text(INSTANCE)
        self.env_file = self.dir / 'staging.env'
        self.env_file.write_text(f'POSTGRES_PASSWORD=pw\nGLOB2_INSTANCE_CONFIG={self.instance}\n')
        self.env_file.chmod(0o600)

    def run_script(self, *args: str, secret: str = '') -> subprocess.CompletedProcess:
        return subprocess.run([sys.executable, str(SCRIPT), str(self.env_file), 'google', *args],
                              input=secret, capture_output=True, text=True)

    def providers(self):
        return yaml.safe_load(self.instance.read_text())['auth']['providers']

    def test_adds_google_and_keeps_the_rest(self):
        result = self.run_script('--client-id', CLIENT, secret='GOCSPX-s3cret\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('GOCSPX', result.stdout + result.stderr)
        self.assertEqual(self.providers(), [{
            'id': 'google', 'kind': 'oidc', 'preset': 'google', 'displayName': 'Google',
            'clientId': CLIENT, 'clientSecretEnv': 'GOOGLE_CLIENT_SECRET'}])
        text = self.instance.read_text()
        for kept in ('# No OIDC providers yet.', '# Universal links', 'allowRegistration: true',
                     'appIds: [CL2MNNYQX3.org.globulation2.glob2]'):
            self.assertIn(kept, text)
        env = self.env_file.read_text().splitlines()
        self.assertEqual(env[-1], 'GOOGLE_CLIENT_SECRET=GOCSPX-s3cret')
        self.assertEqual(self.env_file.stat().st_mode & 0o777, 0o600)

    def test_replacing_and_removing(self):
        self.assertEqual(self.run_script('--client-id', CLIENT, secret='one').returncode, 0)
        other = CLIENT.replace('123456789012', '999')
        self.assertEqual(self.run_script('--client-id', other, secret='two').returncode, 0)
        self.assertEqual([p['clientId'] for p in self.providers()], [other])
        self.assertEqual(self.env_file.read_text().count('GOOGLE_CLIENT_SECRET='), 1)
        self.assertIn('GOOGLE_CLIENT_SECRET=two', self.env_file.read_text())
        self.assertEqual(self.run_script('--remove').returncode, 0)
        self.assertEqual(self.providers(), [])
        self.assertIn('  local:\n    enabled: true', self.instance.read_text())

    def test_rejects_bad_input(self):
        self.assertNotEqual(self.run_script('--client-id', 'not-a-client', secret='x').returncode, 0)
        self.assertNotEqual(self.run_script('--client-id', CLIENT, secret='').returncode, 0)
        self.assertEqual(self.instance.read_text(), INSTANCE)
        self.assertNotIn('GOOGLE_CLIENT_SECRET', self.env_file.read_text())


if __name__ == '__main__':
    unittest.main()
