"""Black-box gateway tests using real TCP and RFC 6455 frames; no pip dependencies."""
import base64
import hashlib
import http.client
import os
from pathlib import Path
import platform
import socket
import socketserver
import struct
import subprocess
import threading
import unittest


class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        while data := self.request.recv(4096):
            self.request.sendall(data)


class EchoServer(socketserver.ThreadingTCPServer):
    daemon_threads = True


def receive(sock, size):
    result = b''
    while len(result) < size:
        part = sock.recv(size - len(result))
        if not part:
            raise EOFError('Connection closed')
        result += part
    return result


def send_frame(sock, payload, opcode=2, final=True):
    mask = os.urandom(4)
    length = len(payload)
    header = bytes([(128 if final else 0) | opcode])
    if length < 126:
        header += bytes([128 | length])
    elif length < 65536:
        header += b'\xfe' + struct.pack('!H', length)
    else:
        header += b'\xff' + struct.pack('!Q', length)
    sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))


def read_frame(sock):
    flags, length = receive(sock, 2)
    if length & 128:
        raise AssertionError('Server must not mask frames')
    if length == 126:
        length = struct.unpack('!H', receive(sock, 2))[0]
    elif length == 127:
        length = struct.unpack('!Q', receive(sock, 8))[0]
    return flags & 15, receive(sock, length)


class GatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.backend = EchoServer(('127.0.0.1', 0), Echo)
        cls.thread = threading.Thread(target=cls.backend.serve_forever, daemon=True)
        cls.thread.start()
        root = Path(__file__).resolve().parents[2]
        binary = os.environ.get('GLOB2_GATEWAY', str(root / 'build' / platform.system().lower() / 'gateway/release/glob2-ws-gateway'))
        cls.process = subprocess.Popen([binary, '--port', '0', '--lobby-port', str(cls.backend.server_address[1]),
                                        '--router-port', str(cls.backend.server_address[1])],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = cls.process.stdout.readline()
        if not line.startswith('gateway listening on '):
            raise RuntimeError('Gateway startup failed: ' + line + cls.process.stderr.read())
        cls.port = int(line.strip().rsplit(':', 1)[1])

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.communicate(timeout=5)
        cls.backend.shutdown()
        cls.backend.server_close()
        cls.thread.join()

    def connect(self, path='/yog', origin='http://127.0.0.1:8765', expected=101, extra=b''):
        sock = socket.create_connection(('127.0.0.1', self.port), timeout=3)
        self.addCleanup(sock.close)
        key = base64.b64encode(os.urandom(16)).decode()
        request = f'GET {path} HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n'
        if origin is not None:
            request += f'Origin: {origin}\r\n'
        sock.sendall((request + '\r\n').encode() + extra)
        response = b''
        while not response.endswith(b'\r\n\r\n'):
            response += receive(sock, 1)
        self.assertEqual(int(response.split(b' ')[1]), expected)
        if expected == 101:
            accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
            self.assertIn(accept, response)
        return sock

    def assert_echo(self, sock, payload):
        result = b''
        while len(result) < len(payload):
            opcode, data = read_frame(sock)
            self.assertIn(opcode, (0, 2))
            result += data
        self.assertEqual(result, payload)

    def test_binary_stream_both_backends(self):
        for path in ('/yog', '/router'):
            with self.subTest(path=path):
                sock = self.connect(path)
                payload = bytes(range(256)) * 240
                send_frame(sock, payload)
                self.assert_echo(sock, payload)

    def test_fragmented_message_and_consecutive_messages(self):
        sock = self.connect()
        send_frame(sock, b'first', final=False)
        send_frame(sock, b'second', opcode=0)
        send_frame(sock, b'third')
        self.assert_echo(sock, b'firstsecondthird')

    def test_native_without_origin(self):
        sock = self.connect(origin=None)
        send_frame(sock, b'native')
        self.assert_echo(sock, b'native')

    def test_reject_origin_and_arbitrary_backend(self):
        self.connect(origin='https://untrusted.example', expected=403)
        self.connect(path='/localhost:22', expected=404)

    def test_text_is_not_forwarded(self):
        sock = self.connect()
        send_frame(sock, b'text', opcode=1)
        self.assertEqual(sock.recv(1), b'')

    def test_http_input_cannot_be_forwarded_as_websocket_payload(self):
        self.connect(extra=b'not a websocket frame', expected=400)

    def test_oversized_message(self):
        sock = self.connect()
        send_frame(sock, b'x' * 65537)
        opcode, data = read_frame(sock)
        self.assertEqual(opcode, 8)
        self.assertEqual(struct.unpack('!H', data[:2])[0], 1009)

    def test_health_and_metrics(self):
        conn = http.client.HTTPConnection('127.0.0.1', self.port, timeout=3)
        self.addCleanup(conn.close)
        conn.request('GET', '/healthz')
        response = conn.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(response.read(), b'ok\n')
        conn.request('GET', '/metrics')
        response = conn.getresponse()
        self.assertEqual(response.status, 200)
        self.assertIn(b'glob2_gateway_connections ', response.read())


if __name__ == '__main__':
    unittest.main()
