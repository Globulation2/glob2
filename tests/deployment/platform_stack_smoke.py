#!/usr/bin/env python3
"""Smoke test of the self-hosted platform stack (deploy/compose.yaml).

Builds the images (unless --no-build), starts an isolated Compose project on
ephemeral ports with its own .env and instance.yaml, and checks:

  1. every service becomes healthy and the init job (keys, migrations) succeeds;
  2. TLS from Caddy's local CA, the web app, invite pages from the API, and
     denial of private routes;
  3. the instance lists the engine agent's sim version;
  4. guest sign-in over REST, then /realtime session.hello with the token;
  5. the JWKS publishes the generated signing key and the access token's kid;
  6. relays: healthy, reachable at /relay/<id> through Caddy (WebSocket 101),
     and registered with the platform under that public URL;
  7. a generate-map engine job end to end: submitted to the queue, run by the
     engine agent with the real glob2 binary, applied by the worker, and the
     map stored in the blob volume;

then tears the project down with its volumes. Python standard library only.

  python3 tests/deployment/platform_stack_smoke.py [--no-build] [--keep] [--log-dir DIR]

With --attach PROJECT --env-file FILE it checks an already running deployment
instead (run on its host, e.g. a live instance with a public certificate): the
same checks against GLOB2_PUBLIC_ORIGIN, with the replica counts from the env
file, publicly trusted TLS, and the deployed web client at /play/. Nothing is
built, started or torn down.

Not a unittest module on purpose: it needs the heavy images and runs in its own
CI job (see .github/workflows/build.yml, platform-stack).
"""
import argparse
import base64
import http.client
import ipaddress
import json
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/transport'))
sys.path.insert(0, str(ROOT / 'deploy'))
from websocket_wire import read_frame, receive, send_frame  # noqa: E402
from sim_version import sim_version_key  # noqa: E402

COMPOSE_FILE = ROOT / 'deploy/compose.yaml'

INSTANCE_YAML = """\
name: Smoke Test Instance
guests:
  enabled: true
auth:
  providers: []
  local:
    enabled: false
access:
  policy: allow-all
queues: []
"""

# Runs inside a platform-api container: submits a generate-map job and waits
# for the worker to apply the agent's result.
SUBMIT_JOB = r"""
import { contentKey, createBlobStore, createLogger, defaultMapPool, JobQueue, loadConfig, submitEngineJob } from '@glob2/core';
import { createDatabase } from '@glob2/db';
import { parseSimVersionKey } from '@glob2/protocol';
const config = loadConfig();
const database = createDatabase({ connectionString: config.databaseUrl, maxConnections: 2 });
const queue = await JobQueue.create(database.pool, createLogger('smoke', 'warn'));
const simVersion = parseSimVersionKey(process.env.SMOKE_SIM_VERSION);
const entry = defaultMapPool('1v1').find((e) => e.generatorId === 'even-ground') ?? defaultMapPool('1v1')[0];
const generator = { ...entry, seed: 20261001 };
const started = Date.now();
const jobId = await submitEngineJob(database.db, queue, { kind: 'generate-map', simVersion, payload: { generator }, maxAttempts: 1 });
let row;
for (;;) {
  row = await database.db.selectFrom('engine_jobs').selectAll().where('id', '=', jobId).executeTakeFirstOrThrow();
  if (row.status !== 'queued' || Date.now() - started > 300000) break;
  await new Promise((r) => setTimeout(r, 1000));
}
const out = { jobId, generator, status: row.status, result: row.result, error: row.error, agent: row.agent_id, seconds: (Date.now() - started) / 1000 };
if (row.status === 'succeeded') {
  out.blobBytes = await createBlobStore(config.blobs).size(contentKey(row.result.mapHash));
  out.blobRow = await database.db.selectFrom('blobs').selectAll().where('sha256', '=', row.result.mapHash).executeTakeFirst() ?? null;
}
console.log(JSON.stringify(out));
await queue.close();
await database.close();
"""


def log(message):
    print(f'[{time.strftime("%H:%M:%S")}] {message}', flush=True)


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def free_subnet():
    ids = subprocess.check_output(['docker', 'network', 'ls', '-q'], text=True).split()
    networks = json.loads(subprocess.check_output(['docker', 'network', 'inspect', *ids], text=True)) if ids else []
    taken = [ipaddress.ip_network(c['Subnet']) for n in networks
             for c in (n['IPAM'].get('Config') or []) if c.get('Subnet') and ':' not in c['Subnet']]
    for i in range(256):
        candidate = ipaddress.ip_network(f'10.232.{i}.0/24')
        if not any(candidate.overlaps(n) for n in taken):
            return candidate
    raise RuntimeError('no free private /24 for the backend network')


SMOKE_PACKAGE = 'assets/core.0123456789abcdef.data'


class Failure(Exception):
    pass


class Smoke:
    def __init__(self, arguments):
        self.arguments = arguments
        self.results = {}
        self.log_dir = Path(arguments.log_dir) if arguments.log_dir else None
        if arguments.attach:
            self.attach(arguments)
            return
        self.project = 'glob2-smoke-' + uuid.uuid4().hex[:8]
        self.directory = Path(tempfile.mkdtemp(prefix='glob2-platform-smoke-'))
        self.https_port = free_port()
        self.origin = f'https://localhost:{self.https_port}'
        self.sim_version = sim_version_key(ROOT)
        subnet = free_subnet()
        tag = arguments.tag
        settings = {
            'GLOB2_DOMAIN': 'localhost',
            'GLOB2_PUBLIC_ORIGIN': self.origin,
            'GLOB2_BIND': '127.0.0.1',
            'GLOB2_HTTP_PORT': str(free_port()),
            'GLOB2_HTTPS_PORT': str(self.https_port),
            'POSTGRES_PASSWORD': uuid.uuid4().hex,
            'GLOB2_API_REPLICAS': '2',
            'GLOB2_RELAY_REPLICAS': '2',
            'GLOB2_ENGINE_AGENT_REPLICAS': '1',
            'GLOB2_BACKEND_SUBNET': str(subnet),
            'GLOB2_PROXY_ADDRESS': str(subnet.network_address + 10),
            'GLOB2_RELAY_DRAIN_SECONDS': '5',
            'GLOB2_RELAY_STOP_GRACE': '20s',
            'GLOB2_INSTANCE_CONFIG': str(self.directory / 'instance.yaml'),
            'GLOB2_WEB_CLIENT_DIR': str(self.directory / 'web-client'),
            'GLOB2_ENV_FILE': str(self.directory / '.env'),
            'GLOB2_SIM_VERSION': self.sim_version,
            'GLOB2_BUILD_JOBS': str(arguments.jobs),
            'GLOB2_PLATFORM_IMAGE': f'glob2-platform:{tag}',
            'GLOB2_ENGINE_AGENT_IMAGE': f'glob2-engine-agent:{tag}',
            'GLOB2_RELAY_IMAGE': f'glob2-relay:{tag}',
            'GLOB2_CADDY_IMAGE': f'glob2-caddy:{tag}',
            'LOG_LEVEL': 'info',
        }
        self.env_file = self.directory / '.env'
        self.host, self.connect_host = 'localhost', '127.0.0.1'
        self.expected_replicas = {'platform-api': 2, 'relay': 2}
        (self.directory / '.env').write_text(''.join(f'{k}={v}\n' for k, v in settings.items()))
        (self.directory / 'instance.yaml').write_text(INSTANCE_YAML)
        (self.directory / 'web-client').mkdir()
        (self.directory / 'web-client/index.html').write_text('<!doctype html><title>glob2 web client</title>')
        # A content-addressed data package with a precompressed copy, as
        # browser/precompress.py and deploy/install-web-client.py lay them out.
        (self.directory / 'web-client/assets').mkdir()
        (self.directory / f'web-client/{SMOKE_PACKAGE}').write_bytes(b'package')
        (self.directory / f'web-client/{SMOKE_PACKAGE}.br').write_bytes(b'brotli')
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(('GLOB2_', 'POSTGRES_'))}

    def attach(self, arguments):
        if not arguments.env_file:
            raise SystemExit('--attach needs --env-file')
        self.project = arguments.attach
        self.env_file = Path(arguments.env_file).resolve()
        self.directory = self.env_file.parent
        self.origin = self.env_value('GLOB2_PUBLIC_ORIGIN').rstrip('/')
        authority = self.origin.split('://', 1)[1]
        self.host = authority.split(':')[0]
        self.https_port = int(authority.split(':')[1]) if ':' in authority else 443
        self.connect_host = self.host
        self.sim_version = arguments.sim_version or sim_version_key(ROOT)
        self.expected_replicas = {'platform-api': int(self.env_value('GLOB2_API_REPLICAS', '2')),
                                  'relay': int(self.env_value('GLOB2_RELAY_REPLICAS', '1'))}
        self.env = {k: v for k, v in os.environ.items() if not k.startswith(('GLOB2_', 'POSTGRES_'))}

    # ------------------------------------------------------------ helpers

    def compose(self, *args, timeout=600, check=True):
        command = ['docker', 'compose', '-p', self.project, '-f', str(COMPOSE_FILE),
                   '--env-file', str(self.env_file), *args]
        result = subprocess.run(command, env=self.env, text=True, capture_output=True, timeout=timeout)
        if check and result.returncode != 0:
            raise Failure(f'{" ".join(args[:2])} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}')
        return result.stdout

    def https(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection(self.connect_host, self.https_port, timeout=20)
        # Verified TLS for the public name (locally "localhost" while connecting to 127.0.0.1).
        connection.sock = self.tls.wrap_socket(
            socket.create_connection((self.connect_host, self.https_port), timeout=20), server_hostname=self.host)
        all_headers = {'Host': self.authority(), **(headers or {})}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            all_headers['Content-Type'] = 'application/json'
        connection.request(method, path, body=data, headers=all_headers)
        response = connection.getresponse()
        payload = response.read()
        connection.close()
        return response.status, dict(response.getheaders()), payload

    def json(self, method, path, body=None, expect=200, headers=None):
        status, _, payload = self.https(method, path, body, headers)
        if status != expect:
            raise Failure(f'{method} {path}: {status} {payload[:400]!r}')
        return json.loads(payload)

    def websocket(self, path, origin=None):
        sock = self.tls.wrap_socket(socket.create_connection((self.connect_host, self.https_port), timeout=20),
                                    server_hostname=self.host)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f'GET {path} HTTP/1.1\r\nHost: {self.authority()}\r\nUpgrade: websocket\r\n'
                   f'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n')
        if origin:
            request += f'Origin: {origin}\r\n'
        sock.sendall((request + '\r\n').encode())
        headers = b''
        while not headers.endswith(b'\r\n\r\n'):
            headers += receive(sock, 1)
        status = int(headers.split(b' ')[1])
        return sock, status

    def authority(self):
        return self.host if self.https_port == 443 else f'{self.host}:{self.https_port}'

    def check(self, name, function):
        log(f'CHECK {name}')
        started = time.monotonic()
        try:
            detail = function()
        except Exception as error:  # noqa: BLE001 - every failure is reported
            self.results[name] = {'ok': False, 'error': str(error)}
            log(f'FAIL  {name}: {error}')
            return False
        self.results[name] = {'ok': True, 'seconds': round(time.monotonic() - started, 2), 'detail': detail}
        log(f'PASS  {name}: {json.dumps(detail)[:300]}')
        return True

    # ------------------------------------------------------------ checks

    def up(self):
        if self.arguments.attach:
            self.tls = ssl.create_default_context()
            return {'attached': self.project, 'origin': self.origin}
        if not self.arguments.no_build:
            log('building images (platform, engine-agent, relay, caddy)')
            started = time.monotonic()
            self.compose('build', 'init', 'engine-agent', 'relay', 'caddy', timeout=7200)
            self.results['build_seconds'] = round(time.monotonic() - started)
        log(f'starting project {self.project} on {self.origin}')
        started = time.monotonic()
        self.compose('up', '-d', '--wait', '--wait-timeout', '240', '--no-build', timeout=600)
        self.results['up_seconds'] = round(time.monotonic() - started)
        ca = self.compose('exec', '-T', 'caddy', 'cat', '/data/caddy/pki/authorities/local/root.crt')
        self.tls = ssl.create_default_context(cadata=ca)
        return {'seconds': self.results['up_seconds']}

    def services_healthy(self):
        rows = [json.loads(line) for line in self.compose('ps', '-a', '--format', 'json').splitlines() if line.strip()]
        summary = {}
        for row in rows:
            summary[row['Name']] = row.get('Health') or row['State']
            if row['Service'] == 'init':
                if row['State'] != 'exited' or row.get('ExitCode') != 0:
                    raise Failure(f'init: {row["State"]} exit {row.get("ExitCode")}')
            elif row['State'] != 'running' or row.get('Health') not in ('healthy', ''):
                raise Failure(f'{row["Name"]}: {row["State"]} {row.get("Health")}')
        counts = {s: sum(1 for r in rows if r['Service'] == s) for s in {r['Service'] for r in rows}}
        if any(counts.get(service) != n for service, n in self.expected_replicas.items()):
            raise Failure(f'unexpected replica counts {counts}')
        init_log = self.compose('logs', '--no-color', 'init')
        return {'services': summary, 'init': [l.split('|', 1)[-1].strip() for l in init_log.splitlines()][-6:]}

    def edge(self):
        status, headers, body = self.https('GET', '/')
        if status != 200 or b'<div id="root">' not in body:
            raise Failure(f'web app: {status} {body[:200]!r}')
        # Invite pages are rendered by the API; an unknown code gets its own page.
        status_j, _, body_j = self.https('GET', '/j/ABCDEFGH')
        if b'Invite not found' not in body_j or b'<div id="root">' in body_j:
            raise Failure(f'/j/ is not served by the API: {status_j} {body_j[:200]!r}')
        status_play, play_headers, body_play = self.https('GET', '/play/')
        marker = b'<canvas' if self.arguments.attach else b'glob2 web client'
        if status_play != 200 or marker not in body_play:
            raise Failure(f'/play/: {status_play} {body_play[:200]!r}')
        for name, expected in (('Cross-Origin-Opener-Policy', 'same-origin'),
                               ('Cross-Origin-Embedder-Policy', 'require-corp')):
            if play_headers.get(name) != expected:
                raise Failure(f'/play/ missing isolation header {name}: {play_headers.get(name)!r}')
        if 'no-cache' not in play_headers.get('Cache-Control', ''):
            raise Failure(f'/play/ must revalidate: {play_headers.get("Cache-Control")!r}')
        if not self.arguments.attach:
            # Data packages: served from their Brotli copy, cached for good, isolated.
            status_package, package_headers, body_package = self.https(
                'GET', '/play/' + SMOKE_PACKAGE, headers={'Accept-Encoding': 'br, gzip'})
            if (status_package != 200 or body_package != b'brotli' or package_headers.get('Content-Encoding') != 'br'
                    or 'immutable' not in package_headers.get('Cache-Control', '')
                    or package_headers.get('Cross-Origin-Embedder-Policy') != 'require-corp'):
                raise Failure(f'/play/{SMOKE_PACKAGE}: {status_package} {package_headers} {body_package[:40]!r}')
        denied = {}
        for path in ('/internal/v1/relays/register', '/internal', '/healthz', '/readyz', '/metrics'):
            for method in ('GET', 'POST'):
                code = self.https(method, path, body={} if method == 'POST' else None)[0]
                denied[f'{method} {path}'] = code
                if code != 404:
                    raise Failure(f'{method} {path} is reachable publicly: {code}')
        connection = http.client.HTTPConnection(self.connect_host, int(self.env_value('GLOB2_HTTP_PORT')), timeout=10)
        connection.request('GET', '/api/v1/instance', headers={'Host': self.host})
        redirect = connection.getresponse()
        if redirect.status not in (301, 308) or not redirect.getheader('Location', '').startswith('https://'):
            raise Failure(f'HTTP is not redirected to HTTPS: {redirect.status}')
        detail = {'web': status, 'invite': status_j, 'play': status_play, 'private': denied,
                  'http': redirect.status, 'hsts_or_server': headers.get('Server')}
        if self.arguments.attach:
            certificate = self.tls.wrap_socket(socket.create_connection((self.connect_host, self.https_port),
                                                                        timeout=20), server_hostname=self.host)
            peer = certificate.getpeercert()
            certificate.close()
            detail['certificate'] = {'issuer': dict(x[0] for x in peer['issuer']), 'notAfter': peer['notAfter'],
                                     'subjectAltName': [v for _, v in peer.get('subjectAltName', [])]}
        return detail

    def env_value(self, name, default=None):
        for line in self.env_file.read_text().splitlines():
            if line.startswith(name + '='):
                return line.split('=', 1)[1]
        if default is not None:
            return default
        raise KeyError(name)

    def instance(self):
        deadline = time.monotonic() + 60
        while True:
            info = self.json('GET', '/api/v1/instance')
            keys = ['-'.join([str(v['versionMinor']), str(v['netProtocol']), v['dataHash']])
                    for v in info['supportedSimVersions']]
            if self.sim_version in keys:
                break
            if time.monotonic() > deadline:
                raise Failure(f'sim version {self.sim_version} not served; instance lists {keys}')
            time.sleep(2)
        if info['origin'] != self.origin or info['realtimeUrl'] != self.origin.replace('https', 'wss') + '/realtime':
            raise Failure(f'instance origin {info["origin"]} / {info["realtimeUrl"]}')
        return {'name': info['name'], 'supportedSimVersions': keys, 'realtimeUrl': info['realtimeUrl']}

    def guest_and_realtime(self):
        signed_in = self.json('POST', '/api/v1/auth/guest', {'platform': 'browser'})
        token = signed_in['tokens']['accessToken']
        self.access_token = token
        account = signed_in['account']
        again = self.json('POST', '/api/v1/auth/guest', {'platform': 'browser',
                                                         'deviceCredential': signed_in['deviceCredential']})
        if again['account']['id'] != account['id']:
            raise Failure('device credential signed in a different account')

        sock, status = self.websocket('/realtime', origin=self.origin)
        if status != 101:
            raise Failure(f'/realtime upgrade: {status}')
        minor, net, data_hash = self.sim_version.split('-')
        hello = {'type': 'request', 'id': '1', 'method': 'session.hello', 'params': {
            'protocol': 1, 'accessToken': token,
            'client': {'platform': 'browser', 'version': 'smoke',
                       'simVersion': {'versionMinor': int(minor), 'netProtocol': int(net), 'dataHash': data_hash}}}}
        send_frame(sock, json.dumps(hello).encode(), opcode=1)
        deadline = time.monotonic() + 15
        while True:
            opcode, data = read_frame(sock)
            if opcode == 9:
                send_frame(sock, data, opcode=10)
                continue
            message = json.loads(data)
            if message.get('type') == 'response' and message.get('id') == '1':
                break
            if time.monotonic() > deadline:
                raise Failure('no session.hello response')
        sock.close()
        if not message.get('ok'):
            raise Failure(f'session.hello refused: {message}')
        result = message['result']
        if not result.get('simSupported') or result.get('account', {}).get('id') != account['id']:
            raise Failure(f'session.hello result {result}')
        bad_origin = self.websocket('/realtime', origin='https://evil.example')[1]
        if bad_origin != 403:
            raise Failure(f'foreign Origin upgrade answered {bad_origin}')
        return {'account': account['displayName'] if 'displayName' in account else account.get('id'),
                'kind': account.get('kind'), 'hello': {k: result[k] for k in ('simSupported', 'sessionId')},
                'foreignOrigin': bad_origin}

    def jwks(self):
        jwks = self.json('GET', '/.well-known/jwks.json')
        kids = [key['kid'] for key in jwks['keys']]
        files = self.compose('exec', '-T', 'platform-api', 'ls', '/var/lib/glob2/keys').split()
        header = json.loads(base64.urlsafe_b64decode(self.access_token.split('.')[0] + '=='))
        if not kids or header.get('kid') not in kids:
            raise Failure(f'access token kid {header.get("kid")} not in JWKS {kids}')
        if sorted(f[:-4] for f in files if f.endswith('.pem')) != sorted(kids):
            raise Failure(f'JWKS {kids} differs from key files {files}')
        if any(key.get('kty') != 'OKP' or key.get('crv') != 'Ed25519' or 'd' in key for key in jwks['keys']):
            raise Failure(f'unexpected JWKS key shape {jwks}')
        return {'kids': kids, 'alg': header.get('alg'), 'typ': header.get('typ')}

    def relays(self):
        # Registration happens at start-up; allow a few heartbeats for retries. Only
        # live relays count: an attached, long-running deployment keeps the rows of
        # replaced relay containers, which the platform ignores once their heartbeat is
        # older than RELAY_STALE_SECONDS (45 s, apps/worker/src/play/relays.ts).
        live = "last_heartbeat_at > now() - interval '45 seconds'"
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            count = self.compose('exec', '-T', 'postgres', 'psql', '-U', 'glob2', '-d', 'glob2', '-At', '-c',
                                 f'SELECT count(*) FROM relays WHERE {live}').strip()
            if count == str(self.expected_replicas['relay']):
                break
            time.sleep(2)
        ids = self.compose('ps', '-q', 'relay').split()
        relay_ids = []
        for container in ids:
            inspect = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]
            relay_ids.append(inspect['Config']['Hostname'])
        routed = {}
        for relay_id in relay_ids:
            sock, status = self.websocket(f'/relay/{relay_id}', origin=self.origin)
            sock.close()
            routed[relay_id] = status
            if status != 101:
                raise Failure(f'/relay/{relay_id} upgrade answered {status}')
        unknown = self.websocket('/relay/nosuchrelay', origin=self.origin)[1]
        dotted = self.websocket('/relay/example.org', origin=self.origin)[1]
        if unknown == 101 or dotted == 101:
            raise Failure(f'unknown relay ids are routed: {unknown}, {dotted}')
        logs = self.compose('logs', '--no-color', 'relay')
        registered = self.compose('exec', '-T', 'postgres', 'psql', '-U', 'glob2', '-d', 'glob2', '-At', '-c',
                                  f"SELECT id || ' ' || public_url FROM relays WHERE {live} ORDER BY id").split('\n')
        registered = [r for r in registered if r.strip()]
        detail = {'relayIds': relay_ids, 'routed': routed, 'unknownRelay': unknown, 'dottedRelay': dotted,
                  'publicUrls': [f'wss://{self.authority()}/relay/{r}' for r in relay_ids],
                  'registered': registered}
        lines = [l.split('|', 1)[-1].strip() for l in logs.splitlines() if 'regist' in l.lower()]
        detail['registrationLog'] = lines[-4:]
        expected = sorted(f'{r} {u}' for r, u in zip(relay_ids, detail['publicUrls']))
        if sorted(registered) != expected:
            raise Failure(f'relays registered as {registered}, expected {expected}; log: {lines[-4:]}')
        return detail

    def engine_job(self):
        output = self.compose('exec', '-T', '-e', f'SMOKE_SIM_VERSION={self.sim_version}', 'platform-api',
                              'node', '--input-type=module', '-e', SUBMIT_JOB, timeout=400)
        result = json.loads(output.strip().splitlines()[-1])
        if result['status'] != 'succeeded':
            raise Failure(f'generate-map job {result["status"]}: {result.get("error")}')
        facts = result['result']
        if facts['map'].get('width') != 128 or facts['map'].get('teamCount') != 2:
            raise Failure(f'unexpected map facts {facts}')
        if result.get('blobBytes') != facts['size'] or not result.get('blobRow'):
            raise Failure(f'map blob missing: {result.get("blobBytes")} vs {facts["size"]}')
        return {'generator': result['generator']['generatorId'], 'seconds': result['seconds'],
                'agent': result['agent'], 'mapHash': facts['mapHash'], 'size': facts['size'], 'map': facts['map'],
                'chosenSeed': facts.get('chosenSeed')}

    # ------------------------------------------------------------ run

    def collect_logs(self):
        if not self.log_dir:
            return
        self.log_dir.mkdir(parents=True, exist_ok=True)
        (self.log_dir / 'compose-logs.txt').write_text(self.compose('logs', '--no-color', check=False, timeout=120))
        (self.log_dir / 'compose-ps.txt').write_text(self.compose('ps', '-a', check=False))
        (self.log_dir / 'results.json').write_text(json.dumps(self.results, indent=2))

    def run(self):
        ok = True
        try:
            if not self.check('stack up', self.up):
                return False
            for name, function in (('services healthy', self.services_healthy), ('edge routing', self.edge),
                                   ('instance and engine agent', self.instance),
                                   ('guest sign-in and realtime', self.guest_and_realtime), ('jwks', self.jwks),
                                   ('relays', self.relays), ('generate-map job', self.engine_job)):
                ok = self.check(name, function) and ok
                if name == 'guest sign-in and realtime' and not self.results[name]['ok']:
                    self.results['jwks'] = {'ok': False, 'error': 'skipped: no access token'}
                    break
            return ok
        finally:
            self.collect_logs()
            if self.arguments.attach:
                pass
            elif self.arguments.keep:
                log(f'keeping project {self.project}; remove with: docker compose -p {self.project} '
                    f'-f {COMPOSE_FILE} --env-file {self.directory / ".env"} down --volumes')
            else:
                log('tearing down')
                self.compose('down', '--volumes', '--remove-orphans', '--timeout', '30', check=False, timeout=300)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--no-build', action='store_true', help='use already built images with --tag')
    parser.add_argument('--keep', action='store_true', help='leave the project running')
    parser.add_argument('--tag', default='smoke', help='image tag to build and run (default: smoke)')
    parser.add_argument('--jobs', type=int, default=3, help='C++ build jobs')
    parser.add_argument('--log-dir', help='write compose logs and results.json here')
    parser.add_argument('--attach', metavar='PROJECT', help='check this running Compose project instead')
    parser.add_argument('--env-file', help="with --attach: the deployment's env file")
    parser.add_argument('--sim-version', help='with --attach: expected sim version key (default: this checkout)')
    smoke = Smoke(parser.parse_args())
    ok = smoke.run()
    passed = [k for k, v in smoke.results.items() if isinstance(v, dict) and v.get('ok')]
    failed = [k for k, v in smoke.results.items() if isinstance(v, dict) and not v.get('ok') and not v.get('expected')]
    expected = [k for k, v in smoke.results.items() if isinstance(v, dict) and v.get('expected')]
    log(f'passed: {passed}')
    if expected:
        log(f'expected failures: {expected}')
    log(f'failed: {failed}' if failed else 'all checks passed')
    return 0 if ok and not failed else 1


if __name__ == '__main__':
    sys.exit(main())
