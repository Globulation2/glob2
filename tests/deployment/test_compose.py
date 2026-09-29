"""Real Compose/TLS/YOG regression. Build deploy/Dockerfile and Wasm first.

Uses an isolated project, ephemeral host ports, and disposable volumes.
"""
import base64
import http.client
import os
from pathlib import Path
import re
import socket
import ssl
import struct
import subprocess
import sys
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/gateway'))
from test_gateway import receive, send_frame, read_frame


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def text(value):
    data = value.encode()
    return struct.pack('!I', len(data)) + data


class ComposeTests(unittest.TestCase):
    @classmethod
    def compose(cls, *args):
        return subprocess.check_output(['docker', 'compose', '-p', cls.project,
            '-f', str(ROOT / 'deploy/compose.yaml'), *args], env=cls.env, text=True,
            stderr=subprocess.STDOUT, timeout=180)

    @classmethod
    def setUpClass(cls):
        cls.project = 'glob2-test-' + uuid.uuid4().hex[:12]
        cls.port = free_port()
        cls.env = dict(os.environ, GLOB2_SITE='https://localhost',
            GLOB2_ORIGIN=f'https://localhost:{cls.port}', GLOB2_HTTPS_PORT=str(cls.port),
            GLOB2_HTTP_PORT=str(free_port()))
        cls.protocol = int(re.search(r'#define NET_PROTOCOL_VERSION (\d+)',
            (ROOT / 'src/Version.h').read_text()).group(1))
        cls.addClassCleanup(cls.compose, 'down', '--volumes', '--remove-orphans')
        try:
            cls.compose('up', '-d', '--wait', '--wait-timeout', '120', '--no-build')
            ca = cls.compose('exec', '-T', 'web', 'cat', '/data/caddy/pki/authorities/local/root.crt')
            cls.tls = ssl.create_default_context(cadata=ca)
        except Exception:
            print(cls.compose('logs', '--tail', '50'))
            raise

    def connect(self, path='/yog'):
        sock = self.tls.wrap_socket(socket.create_connection(('127.0.0.1', self.port), timeout=10),
                                   server_hostname='localhost')
        self.addCleanup(sock.close)
        key = base64.b64encode(os.urandom(16)).decode()
        sock.sendall((f'GET {path} HTTP/1.1\r\nHost: localhost:{self.port}\r\n'
            f'Origin: https://localhost:{self.port}\r\nUpgrade: websocket\r\n'
            f'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n\r\n').encode())
        headers = b''
        while not headers.endswith(b'\r\n\r\n'):
            headers += receive(sock, 1)
        self.assertEqual(int(headers.split(b' ')[1]), 101)
        return sock, bytearray()

    def send(self, connection, opcode, payload=b''):
        send_frame(connection[0], struct.pack('!H', len(payload) + 1) + bytes([opcode]) + payload)

    def wait_message(self, connection, types):
        sock, buffered = connection
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            while len(buffered) >= 2:
                size = int.from_bytes(buffered[:2], 'big')
                if len(buffered) < size + 2:
                    break
                message = bytes(buffered[2:size + 2]); del buffered[:size + 2]
                if message[0] == 5:
                    self.send(connection, 6)
                if message[0] in types:
                    return message
            opcode, data = read_frame(sock)
            if opcode == 9:
                send_frame(sock, data, opcode=10)
            else:
                self.assertEqual(opcode, 2)
                buffered.extend(data)
        self.fail('Timed out waiting for YOG message')

    def login(self, username, register=False):
        connection = self.connect()
        self.send(connection, 9, struct.pack('!H', self.protocol))
        self.wait_message(connection, {10})
        self.send(connection, 2 if register else 1, text(username) + text('fixture-only'))
        response = self.wait_message(connection, {0, 4, 7, 8})
        self.assertEqual(response[0], 0 if register else 4)
        return connection

    def test_assets_tls_and_private_routes(self):
        for target, status in [('/', 200), ('/index.wasm', 200), ('/metrics', 404), ('/healthz', 404)]:
            client = http.client.HTTPSConnection('localhost', self.port, context=self.tls, timeout=10)
            try:
                client.request('HEAD', target)
                response = client.getresponse()
                self.assertEqual(response.status, status)
            finally:
                client.close()
        self.connect('/router')

    def test_account_survives_recreation_and_router_loss_is_refused(self):
        username = 'compose' + uuid.uuid4().hex
        connection = self.login(username, register=True)
        connection[0].close()
        self.compose('stop', 'router')
        connection = self.login(username)
        # Wait for the lobby to observe router closure before requesting a room.
        deadline = time.monotonic() + 10
        while True:
            self.send(connection, 15, text('Deployment test'))
            response = self.wait_message(connection, {16, 17})
            if response[0] == 17:
                self.assertEqual(response[1], 1)  # No router available.
                break
            self.send(connection, 22)  # Leave a room accepted before loss was observed.
            self.assertLess(time.monotonic(), deadline)
        connection[0].close()
        self.compose('up', '-d', '--force-recreate', '--wait', '--wait-timeout', '120', '--no-build', 'lobby', 'router', 'gateway')
        connection = self.login(username)
        self.send(connection, 15, text('Deployment test'))
        self.assertEqual(self.wait_message(connection, {16, 17})[0], 16)
        logs = self.compose('logs', 'lobby', 'router', 'gateway')
        self.assertNotIn('fixture-only', logs)


if __name__ == '__main__':
    unittest.main()
