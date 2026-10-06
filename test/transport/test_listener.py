"""Native WSS listener, LAN pairing, and mutual-TLS integration regression."""
import base64
import os
from pathlib import Path
import platform
import socket
import ssl
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'deploy'))
from provision_tls import provision
from websocket_wire import receive, send_frame, send_frame_header, read_frame

class ListenerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='glob2-listener-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.secrets = Path(cls.directory.name)/'tls'
        provision(cls.secrets)
        build = ROOT/f'build/{platform.system().lower()}/client/release/src'
        cls.listener = Path(os.environ.get('GLOB2_WSS_LISTENER', build/'wss-listener-test'))
        cls.probe = Path(os.environ.get('GLOB2_WSS_PROBE', build/'wss-transport-test'))
        cls.text_message_limit = int(subprocess.check_output(
            [str(cls.listener), '--text-message-limit'], text=True))

    def start(self, mode='public', limit=256, proxies=''):
        with socket.socket() as socket_:
            socket_.bind(('127.0.0.1', 0)); self.port = socket_.getsockname()[1]
        env = dict(os.environ, GLOB2_TLS_CERT=str(self.secrets/'lobby.pem'),
                   GLOB2_TLS_KEY=str(self.secrets/'lobby.key'), GLOB2_TLS_CA=str(self.secrets/'ca.pem'),
                   GLOB2_ALLOWED_ORIGINS='https://test.example', GLOB2_TRUSTED_PROXY_ADDRESSES=proxies, GLOB2_CONNECTION_LIMIT=str(limit))
        process = subprocess.Popen([str(self.listener), str(self.port), mode], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop, process)
        line = process.stdout.readline().strip()
        self.assertTrue(line.startswith('READY '), line + (process.stderr.read() if process.poll() is not None else ''))
        return line.removeprefix('READY ')

    @staticmethod
    def stop(process):
        if process.poll() is None: process.terminate()
        process.communicate(timeout=5)

    def connect(self, route='/yog', origin=None, client=False, forwarded=None, context=None):
        context = context or ssl.create_default_context(cafile=self.secrets/'ca.pem')
        if client: context.load_cert_chain(self.secrets/'router.pem', self.secrets/'router.key')
        sock = context.wrap_socket(socket.create_connection(('127.0.0.1', self.port), timeout=3), server_hostname='localhost')
        self.addCleanup(sock.close)
        key = base64.b64encode(os.urandom(16)).decode()
        headers = f'GET {route} HTTP/1.1\r\nHost: localhost:{self.port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n'
        if origin: headers += f'Origin: {origin}\r\n'
        if forwarded: headers += f'X-Forwarded-For: {forwarded}\r\n'
        sock.sendall((headers+'\r\n').encode())
        response = b''
        while not response.endswith(b'\r\n\r\n'): response += receive(sock, 1)
        self.assertIn(b'101 Switching Protocols', response)
        return sock

    def test_verified_binary_echo_and_fragmentation(self):
        self.start()
        sock = self.connect(origin='https://test.example')
        send_frame(sock, b'first-', final=False); send_frame(sock, b'second', opcode=0)
        self.assertEqual(read_frame(sock), (2, b'first-second'))
        send_frame(sock, bytes(65536))
        echoed = bytearray()
        while len(echoed) < 65536:
            opcode, chunk = read_frame(sock)
            self.assertIn(opcode, (0, 2))  # Beast may fragment each binary message.
            echoed.extend(chunk)
        self.assertEqual(echoed, bytes(65536))

    def test_native_origin_optional_and_wrong_origin_rejected(self):
        self.start(); sock = self.connect()
        send_frame(sock, b'native'); self.assertEqual(read_frame(sock), (2, b'native'))
        with self.assertRaises((EOFError, OSError)): self.connect(origin='https://untrusted.example')
        with self.assertRaises((EOFError, OSError)): self.connect(route='/router')

    def test_direct_client_cannot_spoof_peer_address(self):
        self.start('peer')
        sock = self.connect(forwarded='198.51.100.50'); send_frame(sock, b'peer')
        self.assertEqual(read_frame(sock), (2, b'127.0.0.1'))

    def test_configured_proxy_preserves_numeric_client_address(self):
        self.start('peer', proxies='127.0.0.1')
        sock = self.connect(forwarded='198.51.100.50'); send_frame(sock, b'peer')
        self.assertEqual(read_frame(sock), (2, b'198.51.100.50'))
        with self.assertRaises((EOFError, OSError)):
            self.connect(forwarded='198.51.100.50, 127.0.0.1')

    def test_server_identity_is_loaded_once_at_startup(self):
        self.start()
        identities = [self.secrets/'lobby.pem', self.secrets/'lobby.key']
        try:
            for path in identities:
                path.rename(path.with_suffix(path.suffix + '.offline'))
            for _ in range(2):
                with self.connect() as sock:
                    send_frame(sock, b'cached-identity')
                    self.assertEqual(read_frame(sock), (2, b'cached-identity'))
        finally:
            for path in identities:
                offline = path.with_suffix(path.suffix + '.offline')
                if offline.exists():
                    offline.rename(path)

    def test_text_and_oversized_frames_rejected(self):
        self.start()
        for body, opcode in [(b'not-binary', 1), (bytes(65537), 2)]:
            sock = self.connect()
            try:
                send_frame(sock, body, opcode)
                self.assertEqual(read_frame(sock)[0], 8)
            except (EOFError, ConnectionResetError, BrokenPipeError, ssl.SSLEOFError): pass

    def test_text_mode_echoes_text_and_rejects_binary(self):
        self.start('text')
        sock = self.connect(origin='https://test.example')
        send_frame(sock, b'{"type":"ping","id":1}', opcode=1)
        self.assertEqual(read_frame(sock), (1, b'{"type":"ping","id":1}'))
        send_frame(sock, b'{"part":', opcode=1, final=False); send_frame(sock, b'2}', opcode=0)
        self.assertEqual(read_frame(sock), (1, b'{"part":2}'))
        send_frame(sock, b'', opcode=1)
        self.assertEqual(read_frame(sock), (1, b''))
        large = b'x' * (300 * 1024)
        send_frame(sock, large, opcode=1)
        echoed, opcode = bytearray(), None
        while len(echoed) < len(large):
            frame_opcode, chunk = read_frame(sock)
            opcode = opcode or frame_opcode
            echoed.extend(chunk)
        self.assertEqual((opcode, bytes(echoed)), (1, large))
        # Binary messages and invalid UTF-8 close the connection.
        for body, opcode in [(b'binary', 2), (b'\xff\xfe', 1)]:
            sock = self.connect()
            try:
                # A rejected frame can close TLS before its whole payload is sent.
                send_frame(sock, body, opcode)
                self.assertEqual(read_frame(sock)[0], 8)
            except (EOFError, ConnectionResetError, BrokenPipeError, ssl.SSLEOFError): pass

    def test_text_mode_rejects_oversized_declared_message(self):
        self.start('text')
        sock = self.connect()
        try:
            # Reject the compiled limit at the frame header, without allocating
            # or transmitting a large body under the socket's short timeout.
            send_frame_header(sock, self.text_message_limit + 1, opcode=1)
            self.assertEqual(read_frame(sock)[0], 8)
        except (EOFError, ConnectionResetError, BrokenPipeError, ssl.SSLEOFError): pass

    def test_plaintext_and_missing_client_certificate_rejected(self):
        self.start('register')
        with self.assertRaises((EOFError, OSError)): self.connect(route='/register')
        sock = self.connect(route='/register', client=True)
        send_frame(sock, b'private'); self.assertEqual(read_frame(sock), (2, b'private'))
        with socket.create_connection(('127.0.0.1', self.port), timeout=3) as plain:
            plain.sendall(b'GET /register HTTP/1.1\r\nHost: localhost\r\n\r\n')
            try: self.assertNotIn(b'101', plain.recv(1024))
            except OSError: pass

    def test_tls_before_12_rejected(self):
        self.start()
        import warnings
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE  # Negative protocol test exchanges no application data.
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', DeprecationWarning)
            context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_1
        context.set_ciphers('DEFAULT:@SECLEVEL=0')
        with socket.create_connection(('127.0.0.1', self.port), timeout=3) as sock:
            with self.assertRaises(ssl.SSLError):
                context.wrap_socket(sock, server_hostname='localhost')

    def test_lan_browser_trust_and_explicit_origin_allowlist(self):
        import hashlib
        endpoint = self.start('lan')
        certificate = ssl.get_server_certificate(('127.0.0.1', self.port))
        digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(certificate)).hexdigest()
        self.assertEqual(endpoint.split('#sha256=')[1], digest)
        # Model device/browser certificate provisioning without skipping trust.
        context = ssl.create_default_context(cadata=certificate)
        sock = self.connect(origin='https://test.example', context=context)
        send_frame(sock, b'paired browser'); self.assertEqual(read_frame(sock), (2, b'paired browser'))
        with self.assertRaises((EOFError, OSError)):
            self.connect(origin='https://untrusted.example', context=context)

    def test_lan_certificate_pairing_and_changed_pin(self):
        endpoint = self.start('lan')
        for url, mode in [(endpoint, 'echo'), (endpoint[:-1] + ('0' if endpoint[-1] != '0' else '1'), 'refuse')]:
            result = subprocess.run([str(self.probe), url, mode], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_connection_limit_counts_stalled_handshakes(self):
        self.start(limit=1)
        with socket.create_connection(('127.0.0.1', self.port), timeout=3) as stalled:
            stalled.sendall(b'\x16')
            import time
            time.sleep(.05)
            with self.assertRaises((TimeoutError, OSError)): self.connect()

    def test_private_ca_keys_are_not_replaced(self):
        with self.assertRaises(ValueError): provision(self.secrets)

if __name__ == '__main__': unittest.main()
