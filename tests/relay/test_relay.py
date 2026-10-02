"""glob2-relay end to end: real binary, real sockets, fixture-key tickets, fake platform.

Build first with `scons role=relay release=1 relay`; GLOB2_RELAY_BINARY overrides the
binary. Run with `python3 -m unittest discover -s tests/relay -v`.
"""
import hashlib
import json
import os
import platform
import signal
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import uuid
from pathlib import Path

from relay_wire import (ROOT, SIM_VERSION, FakePlatform, SigningKey, TurnClient, checksum_report, hello,
                        order_submit, parse_record, ping, quit_message, sign_jwt, ticket_claims)

FIXTURES = ROOT / 'test' / 'fixtures' / 'relay-tickets'
RELAY_KEY = 'test-relay-key'
MAP_HASH = '00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff'
CONNECTED, LAGGING, RECONNECTING, RESYNCING, LEFT = 1, 2, 3, 4, 5
REJECT_PROTOCOL, REJECT_BAD_TICKET, REJECT_MATCH_OVER, REJECT_MALFORMED = 1, 2, 5, 6


def relay_binary():
    default = ROOT / f'build/{platform.system().lower()}/relay/release/src/glob2-relay'
    return Path(os.environ.get('GLOB2_RELAY_BINARY', default))


def setup_document(match_id, seats):
    return {'schemaVersion': 1, 'simVersion': SIM_VERSION, 'seed': 42,
            'map': {'kind': 'catalog', 'hash': MAP_HASH},
            'teams': [{'team': s, 'alliance': s} for s in seats],
            'seats': [{'seat': s, 'kind': 'human', 'team': s, 'name': f'P{s}'} for s in seats],
            'rules': {}, 'experiments': [], 'matchId': match_id}


class RelayProcess:
    def __init__(self, test, env):
        self.directory = Path(tempfile.mkdtemp(prefix='glob2-relay-'))
        self.log_path = self.directory / 'relay.log'
        self.log = open(self.log_path, 'w')
        base = {'GLOB2_RELAY_BIND': '127.0.0.1', 'GLOB2_RELAY_PORT': '0',
                'GLOB2_RELAY_SPOOL_DIR': str(self.directory / 'spool')}
        full = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_RELAY_')}
        full.update(base)
        full.update(env)
        self.process = subprocess.Popen([str(relay_binary())], env=full, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True)
        line = self.process.stdout.readline().strip()
        if not line.startswith('READY '):
            self.process.kill()
            raise AssertionError(f'relay did not start: {line!r}\n{self.log_path.read_text()}')
        self.port = int(line.split()[1])
        test.addCleanup(self.stop)
        self.test = test

    def stop(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)
        self.process.stdout.close()
        self.log.close()
        if hasattr(self.test, '_outcome') and os.environ.get('GLOB2_RELAY_TEST_LOGS'):
            print(self.log_path.read_text())

    def get(self, path, headers=None):
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}{path}', headers=headers or {})
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            with error:
                return error.code, error.read().decode()

    def client(self, ticket=None, have_horizon=0, origin=None):
        client = TurnClient(self.port, origin=origin)
        self.test.addCleanup(client.kill)
        if ticket is not None:
            client.send(hello(ticket, have_horizon))
        return client


def contiguous(test, client):
    """Bundles start at the Welcome's resume tick and never leave a gap."""
    bundles = client.bundles()
    test.assertTrue(bundles)
    for previous, current in zip(bundles, bundles[1:]):
        test.assertEqual(current['fromTick'], previous['horizon'])
        test.assertGreaterEqual(current['horizon'], current['fromTick'])
    for bundle in bundles:
        for tick, seat, _ in bundle['entries']:
            test.assertTrue(bundle['fromTick'] <= tick < bundle['horizon'])


class RelayMatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not relay_binary().exists():
            raise unittest.SkipTest(f'{relay_binary()} is not built (scons role=relay release=1 relay)')
        cls.key = SigningKey.fixture()
        jwks = json.loads((FIXTURES / 'jwks.json').read_text())
        assert cls.key.jwk('fixture-key-1')['x'] == jwks['keys'][0]['x'], 'fixture key derivation'

    def ticket(self, match_id, seat, seats, **kwargs):
        return sign_jwt(self.key, 'fixture-key-1', ticket_claims(match_id, seat, seats, **kwargs))

    def test_three_clients_reconnect_and_upload(self):
        match_id = str(uuid.uuid4())
        seats = [0, 1, 2]
        fake = FakePlatform(RELAY_KEY, {'keys': []}, {match_id: setup_document(match_id, seats)})
        self.addCleanup(fake.close)
        relay = RelayProcess(self, {
            'GLOB2_RELAY_JWKS_FILE': str(FIXTURES / 'jwks.json'),
            'GLOB2_RELAY_PLATFORM_URL': fake.url,
            'GLOB2_RELAY_PUBLIC_URL': 'ws://127.0.0.1/relay',
            'GLOB2_RELAY_KEY': RELAY_KEY,
            'GLOB2_RELAY_ID': 'relay-test-1',
            'GLOB2_RELAY_REGION': 'test',
        })
        fake.wait(lambda p: p.registrations, what='registration')
        registration = fake.registrations[0]
        self.assertEqual(registration['relayId'], 'relay-test-1')
        self.assertEqual(registration['publicUrl'], 'ws://127.0.0.1/relay')
        self.assertEqual(registration['region'], 'test')
        self.assertEqual(registration['turnProtocol'], 1)
        self.assertEqual(registration['capacity'], {'maxMatches': 200})
        self.assertFalse(registration['draining'])
        self.assertEqual(relay.get('/healthz'), (200, 'ok\n'))
        self.assertEqual(relay.get('/readyz'), (200, 'ready\n'))

        tickets = {s: self.ticket(match_id, s, seats) for s in seats}
        clients = {s: relay.client(tickets[s]) for s in seats}
        for s, c in clients.items():
            c.wait_for(lambda c: c.of('welcome'), what='Welcome')
            welcome = c.of('welcome')[0]
            self.assertEqual(welcome['seat'], s)
            self.assertEqual(welcome['humanSeatMask'], 0b111)
            self.assertEqual(welcome['tickRateMilliHz'], 25000)
            self.assertEqual(welcome['bundleInterval'], 2)
            self.assertEqual(welcome['resumeFromTick'], 0)
        for c in clients.values():
            c.wait_for(lambda c: c.of('presence') and all(
                c.of('presence')[-1].get(s) == CONNECTED for s in seats), what='all seats connected')
        fake.wait(lambda p: any(match_id in h['activeMatchIds'] for h in p.heartbeats), what='heartbeat with match')

        # Every seat submits orders; seat 0 also sends voice, which is relayed but not recorded.
        voice = bytes([72, 0, 0, 0, 0, 9, 9])
        for k in range(1, 6):
            for s, c in clients.items():
                c.send(order_submit(k, bytes([10 + s, k])))
            time.sleep(0.03)
        clients[0].send(order_submit(6, voice))
        for s, c in clients.items():
            c.send(checksum_report(0, 0xC0FFEE))
            c.send(ping(77, 0))
        expected_orders = {(s, bytes([10 + s, k])) for s in seats for k in range(1, 6)}
        for c in clients.values():
            c.wait_for(lambda c: expected_orders <= {(e[1], e[2]) for e in c.entries()}, what='all orders')
            c.wait_for(lambda c: c.of('pong'), what='Pong')
            self.assertEqual(c.of('pong')[0]['nonce'], 77)
        clients[1].wait_for(lambda c: (0, voice) in {(e[1], e[2]) for e in c.entries()}, what='voice relayed')

        # Kill seat 1's connection; the others see it reconnecting, then back.
        resume_at = clients[1].horizon()
        clients[1].kill()
        for s in (0, 2):
            clients[s].wait_for(lambda c: c.of('presence')[-1].get(1) == RECONNECTING, what='seat 1 reconnecting')
        rejoined = relay.client(tickets[1], have_horizon=resume_at)
        rejoined.wait_for(lambda c: c.of('welcome'), what='Welcome after reconnect')
        welcome = rejoined.of('welcome')[0]
        self.assertEqual(welcome['resumeFromTick'], resume_at)
        self.assertEqual(welcome['lastClientSequence'], 5)
        rejoined.wait_for(lambda c: c.bundles(), what='bundles after reconnect')
        self.assertEqual(rejoined.bundles()[0]['fromTick'], resume_at)
        rejoined.send(order_submit(6, bytes([11, 6])))
        rejoined.send(order_submit(5, bytes([11, 99])))  # stale sequence: ignored
        for c in (clients[0], clients[2], rejoined):
            c.wait_for(lambda c: (1, bytes([11, 6])) in {(e[1], e[2]) for e in c.entries()}, what='order after rejoin')
        for s in (0, 2):
            clients[s].wait_for(lambda c: c.of('presence')[-1].get(1) == CONNECTED, what='seat 1 connected again')

        # Every client saw the same turns for the ticks they share.
        streams = [clients[0], clients[2]]
        for c in streams + [rejoined]:
            contiguous(self, c)
        common = min(c.horizon() for c in streams + [rejoined])
        reference = [e for e in clients[0].entries() if e[0] < common]
        self.assertEqual([e for e in clients[2].entries() if e[0] < common], reference)
        self.assertEqual([e for e in rejoined.entries() if e[0] < common],
                         [e for e in reference if e[0] >= resume_at])
        self.assertNotIn((1, bytes([11, 99])), {(e[1], e[2]) for e in reference})

        # The game finishes: seat 0 reports it, the others quit.
        clients[0].send(quit_message(1))
        clients[2].send(quit_message(0))
        rejoined.send(quit_message(0))
        for c in (clients[0], clients[2], rejoined):
            c.wait_closed()
        fake.wait(lambda p: match_id in p.ends, what='match end report')
        data, content_type = fake.records[match_id]
        self.assertEqual(content_type, 'application/vnd.glob2.match-record')
        ended = fake.ends[match_id]
        record = parse_record(data)

        self.assertEqual(record['formatVersion'], 1)
        self.assertEqual(record['flags'], 0)
        self.assertEqual(record['matchId'], match_id)
        self.assertEqual(record['simVersion'], f"125-49-{SIM_VERSION['dataHash']}")
        self.assertEqual(record['humanSeatMask'], 0b111)
        self.assertEqual(json.loads(record['setupJson']), setup_document(match_id, seats))
        self.assertEqual(record['mapHash'], MAP_HASH)
        recorded = [(t, s, o) for t, s, o in record['turns']]
        self.assertEqual(recorded[:len([e for e in reference if e[2] != voice])],
                         [e for e in reference if e[2] != voice])
        self.assertNotIn(voice, [o for _, _, o in recorded])
        for s in seats:
            self.assertIn(bytes([67, 0, 0, 0, s]), [o for _, seat, o in recorded if seat == s])
        self.assertTrue(all(r[2] == 0xC0FFEE for r in record['reports'] if r[0] == 0))
        self.assertEqual(sorted(r[1] for r in record['reports'] if r[0] == 0), seats)
        self.assertIn(2, [kind for _, seat, kind in record['events'] if seat == 1])  # disconnected

        self.assertEqual(ended['matchId'], match_id)
        self.assertEqual(ended['relayId'], 'relay-test-1')
        self.assertEqual(ended['reason'], 'completed')
        self.assertEqual(ended['simVersion'], SIM_VERSION)
        self.assertEqual(ended['finalTick'], record['endTick'])
        self.assertEqual(ended['record'], {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
                                           'formatVersion': 1})
        self.assertEqual([s['disconnects'] for s in ended['seats']], [0, 1, 0])
        self.assertTrue(all('quitTick' in s and not s['droppedForDesync'] for s in ended['seats']))
        self.assertEqual(ended['desync'], {'flagged': False, 'minoritySeats': []})
        self.assertEqual(fake.unauthorized, 0)

        # A late ticket for the finished match cannot start it again.
        late = relay.client(tickets[0])
        late.wait_for(lambda c: c.of('reject'), what='Reject for ended match')
        self.assertEqual(late.of('reject')[0]['reason'], REJECT_MATCH_OVER)

        deadline = time.time() + 5
        while time.time() < deadline and list((relay.directory / 'spool').glob('*')):
            time.sleep(0.05)
        status, metrics = relay.get('/metrics')
        self.assertEqual(status, 200)
        self.assertIn('glob2_relay_matches_started_total 1', metrics)
        self.assertIn('glob2_relay_matches_ended_total{reason="completed"} 1', metrics)
        self.assertIn('glob2_relay_uploads_ok_total 1', metrics)
        self.assertFalse(list((relay.directory / 'spool').glob('*')), 'spool emptied after upload')

    def test_refusals(self):
        relay = RelayProcess(self, {'GLOB2_RELAY_JWKS_FILE': str(FIXTURES / 'jwks.json'),
                                    'GLOB2_RELAY_ALLOWED_ORIGINS': 'https://play.example.org',
                                    'GLOB2_RELAY_METRICS_TOKEN': 'metrics-secret'})
        match_id = str(uuid.uuid4())

        def refused(ticket, reason, **kwargs):
            client = relay.client(ticket, **kwargs)
            client.wait_for(lambda c: c.of('reject'), what=f'Reject {reason}')
            self.assertEqual(client.of('reject')[0]['reason'], reason)
            client.wait_closed()
            return client.of('reject')[0]['detail']

        # Every bad fixture is refused as a bad ticket (the fixtures are long expired too).
        for name in ('tampered-signature', 'alg-none', 'wrong-type', 'unknown-kid', 'expired', 'wrong-audience',
                     'valid'):
            self.assertIn('Ticket refused', refused((FIXTURES / f'{name}.jwt').read_text().strip(), REJECT_BAD_TICKET))
        access = sign_jwt(self.key, 'fixture-key-1', ticket_claims(match_id, 0, [0, 1]), typ='at+jwt')
        self.assertEqual(refused(access, REJECT_BAD_TICKET), 'Ticket refused: type')
        expired = self.ticket(match_id, 0, [0, 1], lifetime=-31)
        self.assertEqual(refused(expired, REJECT_BAD_TICKET), 'Ticket refused: expired')

        # Wrong protocol version, and anything other than Hello first.
        wrong = relay.client()
        wrong.send(hello(self.ticket(match_id, 0, [0, 1]), version=2))
        wrong.wait_for(lambda c: c.of('reject'), what='protocol reject')
        self.assertEqual(wrong.of('reject')[0]['reason'], REJECT_PROTOCOL)
        early = relay.client()
        early.send(ping(1, 0))
        early.wait_for(lambda c: c.of('reject'), what='malformed reject')
        self.assertEqual(early.of('reject')[0]['reason'], REJECT_MALFORMED)

        # The first ticket creates the match; later tickets must agree with it.
        first = relay.client(self.ticket(match_id, 0, [0, 1]))
        first.wait_for(lambda c: c.of('welcome'), what='Welcome')
        self.assertIn('human seats', refused(self.ticket(match_id, 1, [0, 1, 2]), REJECT_BAD_TICKET))
        other = dict(SIM_VERSION, versionMinor=126)
        self.assertIn('sim version', refused(self.ticket(match_id, 1, [0, 1], sim_version=other), REJECT_BAD_TICKET))
        second = relay.client(self.ticket(match_id, 1, [0, 1]))
        second.wait_for(lambda c: c.of('welcome'), what='second Welcome')

        # Origin: browsers must come from an allowed origin; native clients send none.
        for origin, route, status in (('https://evil.example', '/relay', 403),
                                      ('https://play.example.org', '/relay', 101), (None, '/elsewhere', 404)):
            probe = TurnClient(relay.port, origin=origin, route=route)
            self.addCleanup(probe.kill)
            self.assertEqual(probe.status, status)

        # Metrics need the token when one is configured.
        self.assertEqual(relay.get('/metrics')[0], 401)
        status, metrics = relay.get('/metrics', {'Authorization': 'Bearer metrics-secret'})
        self.assertEqual(status, 200)
        self.assertIn('glob2_relay_tickets_rejected_total{reason="signature"} 1', metrics)
        self.assertIn('glob2_relay_tickets_rejected_total{reason="human_seats_differ"} 1', metrics)
        self.assertIn('glob2_relay_tickets_rejected_total{reason="sim_version_differs"} 1', metrics)

    def test_key_rotation_and_drain(self):
        old_key, new_key = self.key, SigningKey(bytes(range(32)))
        fake = FakePlatform(RELAY_KEY, {'keys': [old_key.jwk('key-1')]})
        self.addCleanup(fake.close)
        relay = RelayProcess(self, {'GLOB2_RELAY_PLATFORM_URL': fake.url,
                                    'GLOB2_RELAY_PUBLIC_URL': 'ws://127.0.0.1/relay',
                                    'GLOB2_RELAY_KEY': RELAY_KEY,
                                    'GLOB2_RELAY_JWKS_MIN_REFRESH_SECONDS': '0'})
        fake.wait(lambda p: p.registrations and p.jwks_fetches, what='registration and JWKS')
        match_id, other_match = str(uuid.uuid4()), str(uuid.uuid4())
        first = relay.client(sign_jwt(old_key, 'key-1', ticket_claims(match_id, 0, [0, 1])))
        first.wait_for(lambda c: c.of('welcome'), what='Welcome with the old key')

        # A ticket under a kid the relay has never seen triggers a refresh; once the
        # platform publishes the new key, the same ticket is accepted.
        rotated = sign_jwt(new_key, 'key-2', ticket_claims(match_id, 1, [0, 1]))
        before = fake.jwks_fetches
        refused = relay.client(rotated)
        refused.wait_for(lambda c: c.of('reject'), what='Reject for unknown kid')
        self.assertGreater(fake.jwks_fetches, before)
        with fake.lock:
            fake.jwks = {'keys': [old_key.jwk('key-1'), new_key.jwk('key-2')]}
        second = relay.client(rotated)
        second.wait_for(lambda c: c.of('welcome'), what='Welcome with the rotated key')

        # Drain: no new matches, the running match continues and reconnects work.
        relay.process.send_signal(signal.SIGTERM)
        fake.wait(lambda p: p.heartbeats and p.heartbeats[-1]['draining'], what='draining heartbeat')
        self.assertEqual(relay.get('/readyz'), (503, 'draining\n'))
        newcomer = relay.client(sign_jwt(new_key, 'key-2', ticket_claims(other_match, 0, [0])))
        newcomer.wait_for(lambda c: c.of('reject'), what='Reject while draining')
        self.assertEqual(newcomer.of('reject')[0]['reason'], REJECT_MATCH_OVER)
        self.assertIn('draining', newcomer.of('reject')[0]['detail'])
        second.kill()
        back = relay.client(rotated, have_horizon=0)
        back.wait_for(lambda c: c.of('welcome'), what='reconnect while draining')

        first.send(quit_message(0))
        back.send(quit_message(0))
        fake.wait(lambda p: match_id in p.ends, what='match end while draining')
        self.assertEqual(fake.ends[match_id]['reason'], 'abandoned')
        self.assertEqual(relay.process.wait(timeout=15), 0, 'relay exits once drained')


if __name__ == '__main__':
    unittest.main()
