"""Native HttpFetch against local HTTP and HTTPS servers.

Builds with `scons release=1 transport-test` (the http-fetch-test harness).
"""
import http.server
import json
import os
from pathlib import Path
import platform
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def reply(self):
        length = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(length).decode() if length else ''
        if self.path == '/slow':
            self.server.stopped.wait(10)
            return
        if self.path == '/big':
            payload = b'x' * (2 * 1024 * 1024)
            status = 200
        else:
            status = int(self.path.split('/')[2]) if self.path.startswith('/status/') else 200
            payload = json.dumps({
                'method': self.command, 'path': self.path, 'body': body,
                'content_type': self.headers.get('Content-Type'),
                'test_header': self.headers.get('X-Glob2-Test'),
                'host': self.headers.get('Host'),
                'user_agent': self.headers.get('User-Agent'),
            }).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('X-Echo-Method', self.command)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_GET = do_POST = do_PUT = reply


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        pass  # Clients that time out, cancel or exceed limits close mid-response.


class HttpFetchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='glob2-http-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.cert = Path(cls.directory.name) / 'cert.pem'
        cls.key = Path(cls.directory.name) / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(cls.key), '-out', str(cls.cert), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, capture_output=True)
        build = ROOT / f'build/{platform.system().lower()}/client/release/src'
        cls.binary = Path(os.environ.get('GLOB2_HTTP_FETCH_PROBE', build / 'http-fetch-test'))

    def serve(self, secure=False):
        server = Server(('127.0.0.1', 0), Handler)
        server.stopped = threading.Event()
        if secure:
            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(self.cert, self.key)
            server.socket = tls.wrap_socket(server.socket, server_side=True)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()

        def stop():
            server.stopped.set()
            server.shutdown()
            server.server_close()
            worker.join()
        self.addCleanup(stop)
        return server.server_address[1]

    def fetch(self, mode, url, *args, trusted=True):
        env = dict(os.environ)
        env['SSL_CERT_FILE'] = str(self.cert if trusted else Path(self.directory.name) / 'absent.pem')
        result = subprocess.run([str(self.binary), mode, url, *args], env=env,
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def check_methods(self, base):
        for mode, method in [('get', 'GET'), ('post', 'POST'), ('put', 'PUT')]:
            body = '' if mode == 'get' else '{"seat": 2}'
            result = self.fetch(mode, base + '/api/v1/echo?x=1', *([body] if body else []))
            self.assertEqual(result['state'], 'done', result)
            self.assertEqual(result['status'], 200)
            self.assertEqual(result['echo_header'], method)
            echoed = json.loads(result['body'])
            self.assertEqual(echoed['method'], method)
            self.assertEqual(echoed['path'], '/api/v1/echo?x=1')
            self.assertEqual(echoed['body'], body)
            self.assertEqual(echoed['content_type'], 'application/json')
            self.assertEqual(echoed['test_header'], 'header value')
            self.assertTrue(echoed['user_agent'].startswith('Globulation2/'))
        return echoed

    def test_plain_http_loopback_methods_headers_and_body(self):
        port = self.serve()
        echoed = self.check_methods(f'http://127.0.0.1:{port}')
        self.assertEqual(echoed['host'], f'127.0.0.1:{port}')

    def test_https_methods_with_verified_certificate(self):
        port = self.serve(secure=True)
        echoed = self.check_methods(f'https://localhost:{port}')
        self.assertEqual(echoed['host'], f'localhost:{port}')

    def test_error_status_is_a_response(self):
        port = self.serve()
        result = self.fetch('get', f'http://localhost:{port}/status/404')
        self.assertEqual((result['state'], result['status']), ('done', 404))
        result = self.fetch('post', f'http://localhost:{port}/status/503', '{}')
        self.assertEqual((result['state'], result['status']), ('done', 503))

    def test_untrusted_and_wrong_host_certificates_fail(self):
        port = self.serve(secure=True)
        self.assertEqual(self.fetch('get', f'https://localhost:{port}/', trusted=False)['state'], 'failed')
        self.assertEqual(self.fetch('get', f'https://127.0.0.1:{port}/')['state'], 'failed')

    def test_response_limit(self):
        port = self.serve()
        result = self.fetch('get', f'http://127.0.0.1:{port}/big')
        self.assertEqual(result['state'], 'failed', result)

    def test_timeout(self):
        port = self.serve()
        result = self.fetch('get', f'http://127.0.0.1:{port}/slow', '', '300')
        self.assertEqual(result['state'], 'timed-out', result)
        self.assertLess(result['elapsed_ms'], 3000)

    def test_cancel_does_not_block(self):
        port = self.serve(secure=True)
        result = self.fetch('cancel', f'https://localhost:{port}/slow')
        self.assertEqual(result['state'], 'cancelled', result)
        self.assertLess(result['elapsed_ms'], 2000)

    def test_rejected_urls(self):
        for url in ('http://example.com/', 'ftp://localhost/', 'https://user:pw@localhost/',
                    'https:///nohost', 'https://localhost:99999/', 'https://local host/'):
            result = self.fetch('get', url)
            self.assertEqual(result['state'], 'failed', url)
            self.assertLess(result['elapsed_ms'], 1000, url)


if __name__ == '__main__':
    unittest.main()
