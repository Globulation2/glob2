"""PlatformClient against a real platform API (guest sign-in, session.hello,
requests, reconnect, token refresh, refresh-token reuse, browser sign-in start,
sign-out).

Needs a platform/ checkout with dependencies installed (the TypeScript
workspace from the multiplayer platform branch) and a Postgres role that may
create databases. The test creates and drops its own database, generates
signing keys, runs the API on loopback behind a local TLS proxy (the client
only speaks wss:// and https:// to non-loopback names, and a verified
certificate for `localhost` exercises the real TLS path), and drives the
`platform-client-probe` harness built by `scons release=1 transport-test`.

    GLOB2_PLATFORM_DIR=/path/to/platform \\
    GLOB2_PLATFORM_DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55432/postgres \\
    PATH=~/.nvm/versions/node/v22.22.1/bin:$PATH \\
    python3 -m unittest tests/online/test_platform_client.py -v

Skipped when the variables are unset.
"""
import json
import http.cookiejar
import os
from pathlib import Path
import platform
import re
import select
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.parse
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[2]
PLATFORM_DIR = os.environ.get('GLOB2_PLATFORM_DIR')
ADMIN_URL = os.environ.get('GLOB2_PLATFORM_DATABASE_URL')
INSTANCE_YAML = """name: Client integration test
guests:
  enabled: true
auth:
  providers: []
  local:
    enabled: true
    allowRegistration: true
  accessTokenMinutes: 1
limits:
  authPerMinute: 1000
  guestsPerHour: 1000
access:
  policy: allow-all
queues: []
"""


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


class TlsProxy:
    """Terminates TLS for `localhost` and forwards bytes to the API."""

    def __init__(self, cert, key, upstream):
        self.upstream = upstream
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(cert, key)
        self.listener = socket.create_server(('127.0.0.1', 0))
        self.port = self.listener.getsockname()[1]
        self.connections = []
        self.lock = threading.Lock()
        self.stopped = False
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        while not self.stopped:
            try:
                client, _ = self.listener.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(client,), daemon=True).start()

    def serve(self, raw):
        try:
            client = self.context.wrap_socket(raw, server_side=True)
            server = socket.create_connection(('127.0.0.1', self.upstream))
        except (OSError, ssl.SSLError):
            raw.close()
            return
        with self.lock:
            self.connections.append((client, server))
        try:
            while True:
                readable, _, _ = select.select([client, server], [], [], 0.5)
                if self.stopped:
                    break
                for source in readable:
                    target = server if source is client else client
                    data = source.recv(65536)
                    if not data:
                        raise OSError('closed')
                    target.sendall(data)
                # TLS may hold decrypted bytes select() cannot see.
                while client.pending():
                    server.sendall(client.recv(65536))
        except (OSError, ssl.SSLError):
            pass
        finally:
            for end in (client, server):
                try:
                    end.close()
                except OSError:
                    pass

    def drop_all(self):
        with self.lock:
            connections, self.connections = self.connections, []
        for pair in connections:
            for end in pair:
                try:
                    end.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def close(self):
        self.stopped = True
        self.listener.close()
        self.drop_all()


@unittest.skipUnless(PLATFORM_DIR and ADMIN_URL,
                     'set GLOB2_PLATFORM_DIR and GLOB2_PLATFORM_DATABASE_URL')
class PlatformClientIntegration(unittest.TestCase):
    @classmethod
    def node(cls, *args, env=None, cwd=None, check=True):
        result = subprocess.run(['node', *args], cwd=cwd or PLATFORM_DIR, env=env,
                                capture_output=True, text=True, timeout=120)
        if check and result.returncode:
            raise AssertionError(f'node {args} failed: {result.stdout}{result.stderr}')
        return result

    @classmethod
    def sql(cls, statement):
        script = ("import pg from 'pg'; const c = new pg.Client(process.env.URL); "
                  "await c.connect(); await c.query(process.env.SQL); await c.end();")
        cls.node('--input-type=module', '-e', script,
                 env=dict(os.environ, URL=ADMIN_URL, SQL=statement),
                 cwd=str(Path(PLATFORM_DIR) / 'packages/db'))

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='glob2-platform-client-')
        cls.addClassCleanup(cls.directory.cleanup)
        work = Path(cls.directory.name)
        build = ROOT / f'build/{platform.system().lower()}/client/release/src'
        cls.binary = Path(os.environ.get('GLOB2_PLATFORM_CLIENT_PROBE', build / 'platform-client-probe'))
        if not cls.binary.exists():
            raise unittest.SkipTest(f'{cls.binary} is missing; build transport-test')

        cls.database = 'glob2_client_it_' + uuid.uuid4().hex[:10]
        cls.sql(f'CREATE DATABASE {cls.database}')
        cls.addClassCleanup(cls.sql, f'DROP DATABASE IF EXISTS {cls.database} WITH (FORCE)')
        database_url = ADMIN_URL.rsplit('/', 1)[0] + '/' + cls.database
        env = dict(os.environ, DATABASE_URL=database_url)
        cls.node('packages/db/src/cli.ts', 'latest', env=env)
        cls.node('apps/api/src/cli.ts', 'keys', 'generate', '--kid', 'it', '--dir', str(work / 'keys'))

        cls.cert, cls.key = work / 'cert.pem', work / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(cls.key), '-out', str(cls.cert), '-days', '1',
                        '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, capture_output=True)
        (work / 'instance.yaml').write_text(INSTANCE_YAML)
        cls.api_port = free_port()
        cls.proxy = TlsProxy(cls.cert, cls.key, cls.api_port)
        cls.addClassCleanup(cls.proxy.close)
        cls.origin = f'https://localhost:{cls.proxy.port}'
        api_env = dict(env, PUBLIC_ORIGIN=cls.origin, HTTP_HOST='127.0.0.1',
                       HTTP_PORT=str(cls.api_port), JWT_KEYS_DIR=str(work / 'keys'),
                       INSTANCE_CONFIG=str(work / 'instance.yaml'), LOG_LEVEL='warn',
                       BLOB_DIR=str(work / 'blobs'))
        cls.api_log = open(work / 'api.log', 'w')
        cls.api = subprocess.Popen(['node', 'apps/api/src/main.ts'], cwd=PLATFORM_DIR, env=api_env,
                                   stdout=cls.api_log, stderr=subprocess.STDOUT)

        def stop_api():
            cls.api.terminate()
            try:
                cls.api.wait(timeout=30)
            except subprocess.TimeoutExpired:
                cls.api.kill()
            cls.api_log.close()
            evidence = os.environ.get('GLOB2_PLATFORM_CLIENT_EVIDENCE')
            if evidence:
                Path(evidence).mkdir(parents=True, exist_ok=True)
                (Path(evidence) / 'api.log').write_text((work / 'api.log').read_text())
        cls.addClassCleanup(stop_api)
        deadline = time.time() + 60
        while True:
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{cls.api_port}/api/v1/instance', timeout=2) as reply:
                    if reply.status == 200:
                        break
            except OSError:
                pass
            if cls.api.poll() is not None or time.time() > deadline:
                raise AssertionError('platform API did not start: ' + (work / 'api.log').read_text())
            time.sleep(0.5)
        cls.state = work / 'state'
        cls.state.mkdir()
        cls.transcript = []

    def probe(self, scenario, on_line=None, timeout=180):
        env = dict(os.environ, SSL_CERT_FILE=str(self.cert))
        process = subprocess.Popen([str(self.binary), self.origin, str(self.state), scenario], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        lines = []
        deadline = time.time() + timeout
        for raw in process.stdout:
            line = json.loads(raw)
            lines.append(line)
            self.transcript.append({'scenario': scenario, **line})
            if on_line:
                on_line(line)
            if time.time() > deadline:
                process.kill()
                break
        process.wait(timeout=30)
        errors = process.stderr.read()
        process.stdout.close()
        process.stderr.close()
        self.write_evidence()
        self.assertEqual(process.returncode, 0, f'{lines}\n{errors}')
        return {line['event']: line for line in lines}

    def write_evidence(self):
        evidence = os.environ.get('GLOB2_PLATFORM_CLIENT_EVIDENCE')
        if evidence:
            Path(evidence).mkdir(parents=True, exist_ok=True)
            def redact(value):
                if isinstance(value, dict):
                    return {key: (f'<{len(item)} chars>' if key in ('refreshToken', 'deviceCredential',
                                                                    'previousRefreshToken') and item
                                  else redact(item)) for key, item in value.items()}
                if isinstance(value, list):
                    return [redact(item) for item in value]
                return value
            redacted = json.dumps(redact(self.transcript), indent=2)
            (Path(evidence) / 'probe-transcript.json').write_text(redacted)

    def stored(self):
        return json.loads((self.state / 'online/instances.json').read_text())['instances'][self.origin]

    def refresh_status(self, token):
        request = urllib.request.Request(
            f'http://127.0.0.1:{self.api_port}/api/v1/auth/refresh',
            data=json.dumps({'refreshToken': token}).encode(), method='POST',
            headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=10) as reply:
                return reply.status
        except urllib.error.HTTPError as error:
            error.close()
            return error.code

    def browser_sign_in(self, sign_in_url, username, code):
        """Registers a local account on the sign-in page, as a browser would."""
        context = ssl.create_default_context(cafile=str(self.cert))
        browser = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()),
            urllib.request.HTTPSHandler(context=context))
        with browser.open(sign_in_url, timeout=20) as page:
            body = page.read().decode()
        attempt = urllib.parse.parse_qs(urllib.parse.urlparse(sign_in_url).query)['attempt'][0]
        confirm = urllib.request.Request(self.origin + '/signin/confirm',
            data=urllib.parse.urlencode({'attempt': attempt, 'code': code}).encode(),
            headers={'Content-Type': 'application/x-www-form-urlencoded', 'Origin': self.origin})
        with browser.open(confirm, timeout=20) as confirmed:
            self.assertEqual(confirmed.status, 200)
        form = urllib.parse.urlencode({'attempt': attempt, 'username': username,
                                       'password': 'correct horse battery', 'action': 'register'})
        request = urllib.request.Request(self.origin + '/signin/local', data=form.encode(), method='POST',
                                         headers={'Content-Type': 'application/x-www-form-urlencoded',
                                                  'Origin': self.origin})
        with browser.open(request, timeout=20) as reply:
            return body, reply.status, reply.read().decode()

    def test_client_lifecycle(self):
        # 1. A fresh client becomes a guest, talks to the socket, survives a
        # dropped connection and refreshes its one-minute access token.
        def on_line(line):
            if line['event'] == 'await-drop':
                self.proxy.drop_all()
        first = self.probe('session', on_line)
        online = first['online']
        self.assertEqual((online['connection'], online['auth'], online['kind']), ('online', 'signed-in', 'guest'))
        self.assertRegex(online['displayName'], r'^Guest-\d+$')
        self.assertRegex(online['deviceCredential'], r'^[A-Za-z0-9_-]{43}$')
        account = online['accountId']
        self.assertTrue(first['ping']['response']['ok'])
        self.assertFalse(first['unsupported']['response']['ok'])
        self.assertEqual(first['bad-params']['response']['code'], 'bad_request')
        self.assertEqual(first['me']['response']['result']['id'], account)
        self.assertTrue(first['reconnected']['newSession'])
        self.assertEqual(first['reconnected']['state']['accountId'], account)
        refreshed = first['refreshed']
        self.assertNotEqual(refreshed['state']['refreshToken'], refreshed['previousRefreshToken'])
        self.assertTrue(refreshed['ping']['ok'])
        self.assertEqual(refreshed['me']['result']['id'], account)
        handoff = first['handoff']
        self.assertEqual(handoff['state'], 2)  # Waiting
        self.assertTrue(handoff['signInUrl'].startswith(self.origin + '/signin?attempt='))
        self.assertRegex(handoff['confirmationCode'], r'^[A-Z0-9]{4,12}$')
        self.assertEqual(handoff['opened'], [handoff['signInUrl']])
        self.assertEqual(first['handoff-cancelled']['failure'], 'cancelled')
        stored = self.stored()
        self.assertEqual(stored['deviceCredential'], online['deviceCredential'])
        rotated_away = refreshed['previousRefreshToken']

        # 2. The next launch signs in with the stored refresh token.
        before = self.stored()['refreshToken']
        second = self.probe('returning')['online']
        self.assertEqual((second['auth'], second['accountId']), ('signed-in', account))
        self.assertNotEqual(self.stored()['refreshToken'], before)
        # Concurrent retries are accepted while the new successor is unused.
        self.assertEqual(self.refresh_status(before), 200)

        # 3. Presenting a rotated refresh token revokes its family; the client
        # falls back to the device credential and keeps the same account.
        live = self.stored()['refreshToken']
        data = json.loads((self.state / 'online/instances.json').read_text())
        data['instances'][self.origin]['refreshToken'] = rotated_away
        (self.state / 'online/instances.json').write_text(json.dumps(data))
        third = self.probe('returning')['online']
        self.assertEqual((third['auth'], third['accountId']), ('signed-in', account))
        self.assertEqual(self.refresh_status(live), 401)  # the whole family was revoked

        # 4. A browser sign-in links a provider to the guest, upgrading it in
        # place; the socket drops while the browser is in front and the client
        # resumes the attempt on its next socket.
        pages = {}

        def browser(line):
            if line['event'] == 'handoff-ready':
                self.proxy.drop_all()
                pages['page'], pages['status'], pages['result'] = self.browser_sign_in(line['signInUrl'], 'probe.player', line['confirmationCode'])
                pages['code'] = line['confirmationCode']
        linked = self.probe('link', browser)['handoff-finished']
        self.assertIn('Code from the game', pages['page'])
        self.assertNotIn(pages['code'], pages['page'])  # the browser asks for the code; never supplies it
        self.assertEqual(pages['status'], 200)
        self.assertTrue(linked['completed'], linked)
        self.assertTrue(linked['linked'])
        self.assertEqual((linked['accountId'], linked['kind'], linked['auth']), (account, 'registered', 'signed-in'))
        self.assertEqual(linked['me']['result']['identities'][0]['provider'], 'local')
        self.assertEqual(self.probe('returning')['online']['kind'], 'registered')

        # 5. Signing out revokes the sign-in and stays signed out.
        before = self.stored()['refreshToken']
        fourth = self.probe('signout')
        self.assertEqual((fourth['signed-out']['connection'], fourth['signed-out']['auth']),
                         ('online', 'signed-out'))
        self.assertFalse(fourth['signed-out']['autoSignIn'])
        self.assertEqual(self.refresh_status(before), 401)
        fifth = self.probe('returning')['online']
        self.assertEqual((fifth['connection'], fifth['auth'], fifth['accountId']), ('online', 'signed-out', ''))
        self.assertEqual(self.stored()['deviceCredential'], online['deviceCredential'])


if __name__ == '__main__':
    unittest.main()
