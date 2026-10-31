"""The exact API-page bootstrap shares the SPA file without capturing API routes."""
import http.client
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


def marked(source, name):
    start = source.index('# Begin ' + name + '.')
    end = source.index('# End ' + name + '.', start)
    return source[start:end]


@unittest.skipUnless(shutil.which('caddy'), 'Caddy is not installed')
class SigninBootstrapTests(unittest.TestCase):
    def test_exact_bootstrap_revalidates_and_other_routes_keep_their_handlers(self):
        source = (ROOT / 'deploy/Caddyfile').read_text()
        rules = marked(source, 'canonical sign-in redirect') + marked(source, 'shared sign-in bootstrap')
        platform_matcher = next(line.strip() for line in source.splitlines() if line.strip().startswith('@platform path '))
        env = {**os.environ, 'GLOB2_DOMAIN': 'app.glob2.online',
               'GLOB2_ADDITIONAL_DOMAIN': 'app.glob2online.com',
               'GLOB2_AUTH_REDIRECT_DOMAIN': 'app.glob2online.com',
               'GLOB2_PUBLIC_ORIGIN': 'https://app.glob2.online'}
        subprocess.run(['caddy', 'adapt', '--config', str(ROOT / 'deploy/Caddyfile'),
                        '--adapter', 'caddyfile'], env=env, check=True, capture_output=True)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bootstrap = b'window.bootstrapFixture = "shared SPA bootstrap";\n'
            (root / 'theme.js').write_bytes(bootstrap)
            rules = rules.replace('root * /srv/web', 'root * ' + str(root))
            config = root / 'Caddyfile'
            config.write_text('{\nadmin off\n}\nhttp://:' + str(port) + ' {\nbind 127.0.0.1\n' + rules + '\n' +
                              platform_matcher + '\nhandle @platform {\nheader Content-Security-Policy "script-src self; fixture-api-policy"\nrespond "API handler" 200\n}\nhandle {\nrespond "SPA handler" 200\n}\n}\n')
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

                    def request(host, method, path):
                        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=2)
                        try:
                            connection.request(method, path, headers={'Host': host})
                            response = connection.getresponse()
                            return response.status, dict(response.getheaders()), response.read()
                        finally:
                            connection.close()

                    for host in ('app.glob2.online', 'app.glob2online.com'):
                        for method, path in [('GET', '/signin/assets/bootstrap.js'),
                                             ('HEAD', '/signin/assets/bootstrap.js'),
                                             ('GET', '/signin/assets/bootstrap.js?v=2')]:
                            with self.subTest(host=host, method=method, path=path):
                                status, headers, body = request(host, method, path)
                                self.assertEqual(status, 200)
                                self.assertEqual(body, bootstrap if method == 'GET' else b'')
                                self.assertIn('javascript', headers['Content-Type'])
                                self.assertEqual(headers['Cache-Control'], 'public, max-age=0, must-revalidate')
                                self.assertNotIn('Location', headers)
                    for host in ('app.glob2.online', 'app.glob2online.com'):
                        status, headers, body = request(host, 'POST', '/signin/assets/bootstrap.js')
                        self.assertEqual((status, body), (200, b'API handler'))
                        self.assertEqual(headers['Content-Security-Policy'], 'script-src self; fixture-api-policy')
                    for path in ['/signin', '/signin/assets/styles.css', '/signin/assets/bootstrap.js/child',
                                 '/signin/assets/Bootstrap.js', '/auth/google/callback?code=fixture',
                                 '/api/v1/instance', '/.well-known/assetlinks.json', '/realtime']:
                        with self.subTest(path=path):
                            status, headers, body = request('app.glob2.online', 'GET', path)
                            self.assertEqual((status, body), (200, b'API handler'))
                            self.assertEqual(headers['Content-Security-Policy'], 'script-src self; fixture-api-policy')
                    for path in ['/signin/assets/tokens.css', '/signin/assets/fonts.css',
                                 '/signin/assets/base.css', '/signin/assets/components.css',
                                 '/signin/assets/brand.css', '/signin/assets/glob2-sans.woff2',
                                 '/signin/assets/favicon-32.png', '/signin/assets/colony.webp',
                                 '/signin/assets/Bootstrap.js']:
                        with self.subTest(old_host_asset=path):
                            status, headers, body = request('app.glob2online.com', 'GET', path)
                            self.assertEqual((status, body), (200, b'API handler'))
                            self.assertNotIn('Location', headers)
                            self.assertEqual(headers['Content-Security-Policy'], 'script-src self; fixture-api-policy')
                    for path in ['/signin?return=%2Faccount', '/signin/settings', '/auth/google/start?return=%2Fplay%2F']:
                        status, headers, _ = request('app.glob2online.com', 'GET', path)
                        self.assertEqual(status, 302)
                        self.assertEqual(headers['Location'], 'https://app.glob2.online' + path)
                    status, _, body = request('app.glob2online.com', 'POST', '/signin')
                    self.assertEqual((status, body), (200, b'API handler'))
                    status, _, body = request('app.glob2online.com', 'GET', '/play/')
                    self.assertEqual((status, body), (200, b'SPA handler'))
                finally:
                    server.terminate()
                    server.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
