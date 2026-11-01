"""The production /play rules preserve MIME and bytes across gzip rewrites."""
import gzip
import hashlib
import http.client
import importlib.util
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


def play_rules(source):
    """Extract the real route; balanced placeholders do not change brace depth."""
    start = source.index('\thandle_path /play/* {')
    lines, depth = [], 0
    for line in source[start:].splitlines(keepends=True):
        lines.append(line)
        depth += line.count('{') - line.count('}')
        if depth == 0:
            return ''.join(lines)
    raise ValueError('Unclosed production browser route')


@unittest.skipUnless(shutil.which('caddy'), 'Caddy is not installed')
class BrowserFontTypeTests(unittest.TestCase):
    def test_actual_play_rules_serve_pinned_fonts_css_and_json_with_gzip(self):
        spec = importlib.util.spec_from_file_location('launcher_web_assets', ROOT / 'scons/web_assets.py')
        assets = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(assets)
        manifest = json.loads((ROOT / 'data/fonts/scripts/manifest.json').read_text())
        rules = play_rules((ROOT / 'deploy/Caddyfile').read_text())
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            assets.launcher_script_fonts(ROOT, root)
            (root / 'locales').mkdir()
            (root / 'locales/hi.json').write_bytes('{"Game text field":"खेल में टेक्स्ट दर्ज करने का बॉक्स"}\n'.encode())
            (root / 'locales/bn.json').write_bytes('{"Game text field":"গেমে লেখার ঘর"}\n'.encode())
            paths = [('script-fonts.css', 'text/css'), ('locales/hi.json', 'application/json'),
                     ('locales/bn.json', 'application/json')]
            paths += [('fonts/' + font['name'] + '-Regular.ttf', 'font/ttf') for font in manifest['fonts']]
            for name, _ in paths:
                file = root / name
                file.with_name(file.name + '.gz').write_bytes(gzip.compress(file.read_bytes(), mtime=0))
            rules = rules.replace('root * /srv/play', 'root * ' + json.dumps(str(root)))
            config = root / 'Caddyfile'
            config.write_text('{\nadmin off\n}\nhttp://127.0.0.1:' + str(port) + ' {\n' + rules + '\n}\n')
            with (root / 'server.log').open('w+') as log:
                with subprocess.Popen(['caddy', 'run', '--config', str(config), '--adapter', 'caddyfile'],
                                      stdout=log, stderr=log) as server:
                    try:
                        for _ in range(100):
                            if server.poll() is not None:
                                log.seek(0)
                                self.fail('Caddy exited: ' + log.read())
                            try:
                                with socket.create_connection(('127.0.0.1', port), timeout=.1):
                                    break
                            except OSError:
                                time.sleep(.02)
                        else:
                            self.fail('Caddy did not start')
                        for name, mime in paths:
                            original = (root / name).read_bytes()
                            for encoding in ('identity', 'gzip'):
                                for method in ('GET', 'HEAD'):
                                    with self.subTest(asset=name, encoding=encoding, method=method):
                                        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                                        try:
                                            connection.request(method, '/play/' + name,
                                                               headers={'Accept-Encoding': encoding})
                                            response = connection.getresponse()
                                            body = response.read()
                                            self.assertEqual(response.status, 200)
                                            self.assertEqual(response.getheader('Content-Type').split(';')[0], mime)
                                            self.assertEqual(response.getheader('Cache-Control'), 'no-cache')
                                            self.assertIn('Accept-Encoding', response.getheader('Vary'))
                                            self.assertIn("font-src 'self'", response.getheader('Content-Security-Policy'))
                                            if encoding == 'gzip':
                                                self.assertEqual(response.getheader('Content-Encoding'), 'gzip')
                                            else:
                                                self.assertIsNone(response.getheader('Content-Encoding'))
                                            if method == 'HEAD':
                                                self.assertEqual(body, b'')
                                            else:
                                                decoded = gzip.decompress(body) if encoding == 'gzip' else body
                                                self.assertEqual(decoded, original)
                                                if name.endswith('.ttf'):
                                                    font = next(f for f in manifest['fonts'] if f['name'] + '-Regular.ttf' == Path(name).name)
                                                    self.assertEqual(hashlib.sha256(decoded).hexdigest(), font['sha256'])
                                        finally:
                                            connection.close()
                    finally:
                        server.terminate()
                        server.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
