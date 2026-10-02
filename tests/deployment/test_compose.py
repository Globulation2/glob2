"""Real Compose/TLS/YOG regression of the legacy stack (deploy/compose.legacy.yaml). Build deploy/Dockerfile and Wasm first.

Uses an isolated project, ephemeral host ports, and disposable volumes.
"""
import base64
import gzip
import ipaddress
import json
import http.client
import os
from pathlib import Path
import re
import socket
import ssl
import struct
import subprocess
import tempfile
import sys
import time
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/transport'))
from websocket_wire import receive, send_frame, read_frame
sys.path.insert(0, str(ROOT / 'deploy'))
from provision_tls import provision


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
        try:
            return subprocess.check_output(['docker', 'compose', '-p', cls.project,
                '-f', str(ROOT / 'deploy/compose.legacy.yaml'), *args], env=cls.env, text=True,
                stderr=subprocess.STDOUT, timeout=180)
        except subprocess.CalledProcessError as error:
            print(error.output)
            raise

    @classmethod
    def setUpClass(cls):
        cls.project = 'glob2-test-' + uuid.uuid4().hex[:12]
        cls.port = free_port()
        cls.directory = tempfile.TemporaryDirectory(prefix='glob2-compose-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.secrets = Path(cls.directory.name)/'tls'
        provision(cls.secrets)
        network_ids = subprocess.check_output(['docker', 'network', 'ls', '-q'], text=True).split()
        networks = json.loads(subprocess.check_output(['docker', 'network', 'inspect', *network_ids], text=True)) if network_ids else []
        occupied = [ipaddress.ip_network(c['Subnet']) for n in networks for c in (n['IPAM'].get('Config') or []) if c.get('Subnet')]
        subnet = next(ipaddress.ip_network(f'10.231.{i}.0/24') for i in range(256)
                      if not any(ipaddress.ip_network(f'10.231.{i}.0/24').overlaps(n) for n in occupied if n.version == 4))
        cls.env = dict(os.environ, GLOB2_BACKEND_SUBNET=str(subnet), GLOB2_PROXY_ADDRESS=str(subnet.network_address+10), GLOB2_SITE='https://localhost',
            GLOB2_ORIGIN=f'https://localhost:{cls.port}', GLOB2_HTTPS_PORT=str(cls.port),
            GLOB2_HTTP_PORT=str(free_port()), GLOB2_SECRETS_DIR=str(cls.secrets),
            GLOB2_DRAIN_SECONDS='15',
            GLOB2_LOBBY_ENDPOINT=f'wss://localhost:{cls.port}/yog',
            GLOB2_ROUTER_ENDPOINT=f'wss://localhost:{cls.port}/router')
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
        deadline = time.monotonic() + 30
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

    def control(self, service, path='/readyz'):
        port = 7492 if service == 'lobby' else 7493
        script = f"import http.client; c=http.client.HTTPConnection('localhost',{port},timeout=3); c.request('GET',{path!r}); r=c.getresponse(); print(r.status); print(r.read().decode())"
        return self.compose('exec', '-T', service, 'python3', '-c', script)

    def test_exclusive_lobby_data_owner(self):
        result = subprocess.run(['docker', 'compose', '-p', self.project, '-f', str(ROOT/'deploy/compose.legacy.yaml'),
            'run', '--rm', '--no-deps', 'lobby'], env=self.env, text=True, capture_output=True, timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('data directory already owned', result.stdout + result.stderr)

    def test_persistent_backup_can_be_restored(self):
        username = 'backup' + uuid.uuid4().hex
        self.login(username, register=True)[0].close()
        self.compose('stop', 'router', 'lobby')
        backup = Path(self.directory.name)/'backup'
        self.compose('cp', 'lobby:/var/lib/glob2/.', str(backup))
        owner = self.compose('ps', '-a', '-q', 'lobby').strip()
        info = json.loads(subprocess.check_output(['docker', 'inspect', owner], text=True))[0]
        volume = next(m['Name'] for m in info['Mounts'] if m['Destination'] == '/var/lib/glob2')
        image = info['Config']['Image']
        subprocess.check_output(['docker', 'run', '--rm', '--user', '10001:10001', '-v', volume+':/data', image,
            'python3', '-c', "import pathlib,shutil; [(shutil.rmtree(p) if p.is_dir() else p.unlink()) for p in pathlib.Path('/data').iterdir()]"], text=True, timeout=30)
        self.compose('cp', str(backup) + '/.', 'lobby:/var/lib/glob2/')
        # docker cp assigns destination ownership; restored state belongs to the service UID.
        subprocess.check_output(['docker', 'run', '--rm', '--user', '0:0', '-v', volume+':/data', image,
            'python3', '-c', "import pathlib,os; [os.chown(p,10001,10001) for p in [pathlib.Path('/data'),*pathlib.Path('/data').rglob('*')]]"], text=True, timeout=30)
        self.compose('up', '-d', '--wait', '--wait-timeout', '120', '--no-build', 'lobby', 'router')
        self.login(username)[0].close()

    def active_match(self):
        connection = self.login('drain' + uuid.uuid4().hex, register=True)
        self.send(connection, 15, text('Drain lifecycle fixture'))
        self.assertEqual(self.wait_message(connection, {16, 17})[0], 16)
        self.send(connection, 30)
        self.assertEqual(self.wait_message(connection, {27, 49})[0], 49)
        return connection

    def test_shutdown_finishes_after_match_leaves(self):
        connection = self.active_match()
        try:
            self.compose('kill', '-s', 'SIGINT', 'lobby')
            self.assertTrue(self.control('lobby').startswith('503'))
            self.assertIn('glob2_draining 1', self.control('lobby', '/metrics'))
            self.send(connection, 15, text('Must not admit during drain'))
            self.assertEqual(self.wait_message(connection, {16, 17})[0], 17)
            self.send(connection, 22)
            self.wait_exit('lobby')
        finally:
            connection[0].close()
            self.compose('up', '-d', '--force-recreate', '--wait', '--wait-timeout', '120', '--no-build', 'lobby', 'router')

    def test_router_drain_preserves_existing_game_traffic(self):
        first, second = self.connect('/router'), self.connect('/router')
        try:
            for peer in (first, second): self.send(peer, 47, struct.pack('!H', 42))
            # Registration has no application ACK. A control request runs after
            # an update tick; allow the second peer's TLS read to join the game.
            self.assertIn('glob2_games 1', self.control('router', '/metrics'))
            time.sleep(.1)
            before = struct.pack('!IBBI', 1, 51, 0, 1234)  # NullOrder, sender, simulation checksum.
            self.send(first, 44, before)
            self.assertEqual(self.wait_message(second, {44}), bytes([44]) + before)
            self.compose('kill', '-s', 'SIGTERM', 'router')
            self.assertTrue(self.control('router').startswith('503'))
            self.assertTrue(self.control('lobby').startswith('503'))
            after = struct.pack('!IBBI', 1, 51, 0, 5678)
            self.send(first, 44, after)
            self.assertEqual(self.wait_message(second, {44}), bytes([44]) + after)
            with self.assertRaises(AssertionError): self.connect('/router')
            first[0].close(); second[0].close()
            self.wait_exit('router')
        finally:
            first[0].close(); second[0].close()
            self.compose('up', '-d', '--force-recreate', '--wait', '--wait-timeout', '120', '--no-build', 'router')

    def wait_exit(self, service):
        container = self.compose('ps', '-a', '-q', service).strip()
        initial = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]
        started = initial['State']['StartedAt']
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            state = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]['State']
            # restart policy can restart a process; the finished timestamp still records draining exit.
            if state['FinishedAt'] >= started: return
            time.sleep(.1)
        self.fail('Drain did not exit within its deadline')

    def test_shutdown_deadline_interrupts_remaining_match(self):
        connection = self.active_match()
        try:
            self.compose('kill', '-s', 'SIGTERM', 'lobby')
            self.assertTrue(self.control('lobby').startswith('503'))
            self.wait_exit('lobby')
            self.assertIn('Drain deadline expired; active games will be interrupted', self.compose('logs', 'lobby'))
        finally:
            connection[0].close()
            self.compose('up', '-d', '--force-recreate', '--wait', '--wait-timeout', '120', '--no-build', 'lobby', 'router')

    def test_assets_tls_and_private_routes(self):
        for service in ('lobby', 'router', 'web'):
            container = self.compose('ps', '-q', service).strip()
            config = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]
            self.assertEqual(config['Config']['User'], '10001:10001')
            self.assertTrue(config['HostConfig']['ReadonlyRootfs'])
            self.assertIn('ALL', config['HostConfig']['CapDrop'])
            self.assertTrue(any(value.startswith('no-new-privileges') for value in config['HostConfig']['SecurityOpt']))
        assets = Path(os.environ.get('GLOB2_ASSETS', ROOT/'build/browser-static'))
        wasm = next(assets.glob('index-*.wasm')).name
        threaded_wasm = next((assets/'threaded').glob('index-*.wasm')).name
        loader = next(assets.glob('loader-*.js')).name
        for target, status in [('/', 200), ('/'+wasm, 200), ('/threaded/'+threaded_wasm, 200), ('/'+loader, 200), ('/metrics', 404), ('/healthz', 404), ('/readyz', 404), ('/livez', 404), ('/register', 404)]:
            client = http.client.HTTPSConnection('localhost', self.port, context=self.tls, timeout=10)
            try:
                client.request('HEAD', target)
                response = client.getresponse()
                self.assertEqual(response.status, status)
                if status == 200:
                    self.assertEqual(response.getheader('Cross-Origin-Opener-Policy'), 'same-origin')
                    self.assertEqual(response.getheader('Cross-Origin-Embedder-Policy'), 'require-corp')
            finally:
                client.close()
        self.connect('/router')
        malformed = self.connect()
        self.send(malformed, 52, struct.pack('!I', 0xffffffff))
        with self.assertRaises((EOFError, OSError)):
            while True: read_frame(malformed[0])
        self.assertTrue(self.control('lobby').startswith('200'))

    def test_static_gzip_negotiation_and_mime_types(self):
        assets = Path(os.environ.get('GLOB2_ASSETS', ROOT/'build/browser-static'))
        names = ['index.html'] + [next(assets.glob('index-*.'+ext)).name for ext in ('js','wasm','data')]
        names += [next(assets.glob('loader-*.js')).name]
        names += ['threaded/'+next((assets/'threaded').glob('index-*.'+ext)).name for ext in ('js','wasm')]
        for name in names:
            responses = {}
            for encoding in ('identity', 'gzip'):
                client = http.client.HTTPSConnection('localhost', self.port, context=self.tls, timeout=30)
                try:
                    client.request('GET', '/'+name, headers={'Accept-Encoding': encoding})
                    response = client.getresponse()
                    # Caddy 2.10.2 adds Range: bytes=0- for precompressed files
                    # to supply Content-Length. Accept only the complete sidecar,
                    # never a truncated range or a partial identity response.
                    if encoding == 'gzip' and response.status == 206:
                        size = (assets/(name+'.gz')).stat().st_size
                        self.assertEqual(response.getheader('Content-Range'), f'bytes 0-{size-1}/{size}')
                        self.assertEqual(response.getheader('Content-Length'), str(size))
                    else:
                        self.assertEqual(response.status, 200)
                    self.assertIn('Accept-Encoding', response.getheader('Vary'))
                    self.assertEqual(response.getheader('Content-Encoding'), 'gzip' if encoding == 'gzip' else None)
                    self.assertIn('no-cache', response.getheader('Cache-Control'))
                    responses[encoding] = (response.read(), response.getheader('Content-Type'))
                finally:
                    client.close()
            self.assertEqual(gzip.decompress(responses['gzip'][0]), responses['identity'][0])
            self.assertEqual(responses['identity'][0], (assets/name).read_bytes())
            self.assertEqual(responses['identity'][1], responses['gzip'][1])
            if name.endswith('.wasm'):
                self.assertEqual(responses['gzip'][1], 'application/wasm')
            if name.endswith('.data'):
                self.assertEqual(responses['gzip'][1], 'application/octet-stream')

    def test_account_survives_recreation_and_router_loss_is_refused(self):
        username = 'compose' + uuid.uuid4().hex
        connection = self.login(username, register=True)
        connection[0].close()
        self.compose('stop', 'router')
        self.assertTrue(self.control('lobby').startswith('503'))
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
        self.compose('up', '-d', '--force-recreate', '--wait', '--wait-timeout', '120', '--no-build', 'lobby', 'router')
        connection = self.login(username)
        self.send(connection, 15, text('Deployment test'))
        self.assertEqual(self.wait_message(connection, {16, 17})[0], 16)
        logs = self.compose('logs', 'lobby', 'router')
        self.assertNotIn('fixture-only', logs)


if __name__ == '__main__':
    unittest.main()
