"""Exercise the real Caddy sign-in transition rules without TLS or a backend."""
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('caddy'), 'Caddy is not installed')
class CanonicalSigninTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('docker'), 'Docker Compose is not installed')
    def test_compose_redirect_defaults_provision_a_valid_caddy_config(self):
        base_env = {key: value for key, value in os.environ.items()
                    if not key.startswith('GLOB2_')}
        base_env.update(POSTGRES_PASSWORD='fixture', GLOB2_ENV_FILE=os.devnull)
        for settings, expected in [({}, 'auth-redirect.invalid'),
                                   ({'GLOB2_AUTH_REDIRECT_DOMAIN': ''}, 'auth-redirect.invalid'),
                                   ({'GLOB2_AUTH_REDIRECT_DOMAIN': 'app.glob2online.com'},
                                    'app.glob2online.com')]:
            with self.subTest(settings=settings):
                result = subprocess.run(
                    ['docker', 'compose', '--env-file', os.devnull, '-f',
                     str(ROOT / 'deploy/compose.yaml'), 'config', '--format', 'json'],
                    env={**base_env, **settings}, check=True, capture_output=True, text=True)
                environment = json.loads(result.stdout)['services']['caddy']['environment']
                self.assertEqual(environment['GLOB2_AUTH_REDIRECT_DOMAIN'], expected)
                # Adapt alone accepts an empty host matcher; provisioning rejects it.
                subprocess.run(['caddy', 'validate', '--config', str(ROOT / 'deploy/Caddyfile'),
                                '--adapter', 'caddyfile'], env={**base_env, **environment},
                               check=True, capture_output=True)

    def test_only_former_host_get_signin_moves_to_the_canonical_origin(self):
        source = (ROOT / 'deploy/Caddyfile').read_text()
        start = source.index('# Begin canonical sign-in redirect.')
        end = source.index('# End canonical sign-in redirect.', start)
        rules = source[start:end]
        env = {**os.environ, 'GLOB2_DOMAIN': 'app.glob2.online',
               'GLOB2_ADDITIONAL_DOMAIN': 'app.glob2online.com',
               'GLOB2_AUTH_REDIRECT_DOMAIN': 'app.glob2online.com',
               'GLOB2_PUBLIC_ORIGIN': 'https://app.glob2.online'}
        # Adapt the complete production file too, not only the test HTTP site.
        subprocess.run(['caddy', 'adapt', '--config', str(ROOT / 'deploy/Caddyfile'),
                        '--adapter', 'caddyfile'], env=env, check=True, capture_output=True)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'Caddyfile'
            config.write_text('{\nadmin off\n}\nhttp://:' + str(port) +
                              ' {\nbind 127.0.0.1\n' + rules + '\nrespond "served" 200\n}\n')
            with subprocess.Popen(['caddy', 'run', '--config', str(config), '--adapter', 'caddyfile'],
                                  env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as server:
                try:
                    for _ in range(100):
                        if server.poll() is not None:
                            self.fail('Caddy exited before accepting requests')
                        try:
                            with socket.create_connection(('127.0.0.1', port), timeout=.1):
                                break
                        except OSError:
                            time.sleep(.02)
                    else:
                        self.fail('Caddy did not start')
                    cases = [
                        ('app.glob2online.com', 'GET', '/signin?next=%2Faccount', 302),
                        ('app.glob2online.com', 'GET', '/signin/', 302),
                        ('app.glob2online.com', 'GET', '/auth/google/start?return=%2Fplay%2F', 302),
                        ('app.glob2online.com', 'POST', '/signin', 200),
                        ('app.glob2online.com', 'POST', '/auth/google/start', 200),
                        ('app.glob2.online', 'GET', '/signin', 200),
                        ('app.glob2online.com', 'GET', '/auth/google/callback?code=fixture', 200),
                        ('app.glob2online.com', 'GET', '/play/', 200),
                        ('app.glob2online.com', 'GET', '/api/v1/instance', 200),
                        ('app.glob2online.com', 'GET', '/realtime', 200),
                        ('app.glob2online.com', 'GET', '/relay/relay-1', 200),
                        ('app.glob2online.com', 'GET', '/.well-known/assetlinks.json', 200),
                        ('app.glob2online.com', 'GET', '/.well-known/apple-app-site-association', 200),
                    ]
                    for host, method, path, status in cases:
                        with self.subTest(host=host, method=method, path=path):
                            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
                            try:
                                connection.request(method, path, headers={'Host': host})
                                response = connection.getresponse()
                                self.assertEqual(response.status, status)
                                self.assertEqual(response.getheader('Location'),
                                                 'https://app.glob2.online' + path if status == 302 else None)
                                response.read()
                            finally:
                                connection.close()
                finally:
                    server.terminate()
                    server.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
