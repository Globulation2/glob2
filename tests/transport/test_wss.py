"""Native WSS certificate, hostname, framing, cancellation, and timeout tests."""
import base64
import hashlib
import os
from pathlib import Path
import platform
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/gateway'))
from test_gateway import receive


class Peer(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            sock = self.request
            sock.settimeout(15)
            if self.server.mode == 'stall':
                self.server.stopped.wait(13)
                return
            with self.server.tls.wrap_socket(sock, server_side=True) as sock:
                headers = b''
                while not headers.endswith(b'\r\n\r\n'):
                    headers += receive(sock, 1)
                self.server.path = headers.split(b' ')[1].decode()
                values = dict(line.split(b':', 1) for line in headers.split(b'\r\n')[1:] if b':' in line)
                key = values[b'Sec-WebSocket-Key'].strip()
                accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                sock.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                while True:
                    flags, length = receive(sock, 2)
                    if not length & 128:
                        raise AssertionError('Client frame was not masked')
                    length &= 127
                    if length == 126: length = struct.unpack('!H', receive(sock, 2))[0]
                    elif length == 127: length = struct.unpack('!Q', receive(sock, 8))[0]
                    mask = receive(sock, 4)
                    body = receive(sock, length)
                    body = bytes(b ^ mask[i % 4] for i, b in enumerate(body))
                    if self.server.mode == 'oversized': body = bytes(65537)
                    opcode = 1 if self.server.mode == 'text' else 2
                    if len(body) < 126: header = bytes([128 | opcode, len(body)])
                    elif len(body) < 65536: header = bytes([128 | opcode, 126]) + struct.pack('!H', len(body))
                    else: header = bytes([128 | opcode, 127]) + struct.pack('!Q', len(body))
                    sock.sendall(header + body)
        except (OSError, EOFError):
            pass


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


class WssTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='glob2-wss-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.cert = Path(cls.directory.name) / 'cert.pem'
        key = Path(cls.directory.name) / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
            '-keyout', str(key), '-out', str(cls.cert), '-days', '1', '-subj', '/CN=localhost',
            '-addext', 'subjectAltName=DNS:localhost'], check=True, capture_output=True)
        cls.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        cls.tls.load_cert_chain(cls.cert, key)
        cls.binary = ROOT / f'build/{platform.system().lower()}/client/release/src/wss-transport-test'

    def run_peer(self, mode='echo', probe='echo', trusted=True, hostname='localhost'):
        with Server(('127.0.0.1', 0), Peer) as server:
            server.mode = mode; server.tls = self.tls; server.path = None
            server.stopped = threading.Event()
            worker = threading.Thread(target=server.serve_forever, daemon=True); worker.start()
            try:
                env = dict(os.environ)
                env['SSL_CERT_FILE'] = str(self.cert) if trusted else str(Path(self.directory.name) / 'absent.pem')
                result = subprocess.run([str(self.binary), f'wss://{hostname}:{server.server_address[1]}', probe],
                    env=env, capture_output=True, text=True, timeout=16)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if probe in ('echo', 'router'):
                    self.assertEqual(server.path, '/router' if probe == 'router' else '/yog')
            finally:
                server.stopped.set(); server.shutdown(); worker.join()

    def test_verified_echo_and_fixed_routes(self):
        self.run_peer(); self.run_peer(probe='router')

    def test_untrusted_certificate(self):
        self.run_peer(probe='refuse', trusted=False)

    def test_wrong_hostname(self):
        self.run_peer(probe='refuse', hostname='127.0.0.1')

    def test_text_and_oversized_messages_close(self):
        self.run_peer(mode='text', probe='badframe')
        self.run_peer(mode='oversized', probe='badframe')

    def test_stalled_tls_can_be_cancelled(self):
        self.run_peer(mode='stall', probe='cancel')

    def test_stalled_tls_times_out(self):
        self.run_peer(mode='stall', probe='timeout')

    def test_credential_and_path_urls_are_rejected(self):
        for url in ('wss://user:password@localhost', 'wss://localhost/router', 'wss://localhost?token=secret'):
            result = subprocess.run([str(self.binary), url, 'refuse'], capture_output=True, text=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
