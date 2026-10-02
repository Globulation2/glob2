"""Test-side helpers for glob2-relay: a pure-Python Ed25519 signer for match tickets,
the binary turn protocol (docs/multiplayer/turn-protocol.md), a WebSocket turn client,
a match-record parser and a fake platform. Standard library only."""
import base64
import hashlib
import http.server
import json
import os
import socket
import struct
import sys
import threading
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests' / 'transport'))
from websocket_wire import receive, send_frame, read_frame  # noqa: E402

# --- Ed25519 (RFC 8032 section 6 reference arithmetic; slow but fine for tests) -------

_P = 2 ** 255 - 19
_Q = 2 ** 252 + 27742317777372353535851937790883648493
_D = -121665 * pow(121666, _P - 2, _P) % _P
_SQRT_M1 = pow(2, (_P - 1) // 4, _P)


def _inv(x):
    return pow(x, _P - 2, _P)


def _add(a, b):
    A = (a[1] - a[0]) * (b[1] - b[0]) % _P
    B = (a[1] + a[0]) * (b[1] + b[0]) % _P
    C = 2 * a[3] * b[3] * _D % _P
    D = 2 * a[2] * b[2] % _P
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F, G * H, F * G, E * H)


def _mul(s, point):
    result = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            result = _add(result, point)
        point = _add(point, point)
        s >>= 1
    return result


def _recover_x(y, sign):
    x2 = (y * y - 1) * _inv(_D * y * y + 1)
    x = pow(x2, (_P + 3) // 8, _P)
    if (x * x - x2) % _P:
        x = x * _SQRT_M1 % _P
    if (x & 1) != sign:
        x = _P - x
    return x


_GY = 4 * _inv(5) % _P
_GX = _recover_x(_GY, 0)
_G = (_GX, _GY, 1, _GX * _GY % _P)


def _compress(point):
    zi = _inv(point[2])
    x, y = point[0] * zi % _P, point[1] * zi % _P
    return int.to_bytes(y | ((x & 1) << 255), 32, 'little')


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], 'little')
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


class SigningKey:
    def __init__(self, seed):
        self.seed = seed
        self.a, self.prefix = _expand(seed)
        self.public = _compress(_mul(self.a, _G))

    @classmethod
    def fixture(cls):
        """The protocol package's TEST-ONLY key (fixtureFiles.ts)."""
        return cls(hashlib.sha256(b'glob2 protocol fixture key, TEST ONLY').digest())

    def sign(self, message):
        r = int.from_bytes(hashlib.sha512(self.prefix + message).digest(), 'little') % _Q
        R = _compress(_mul(r, _G))
        h = int.from_bytes(hashlib.sha512(R + self.public + message).digest(), 'little') % _Q
        return R + int.to_bytes((r + h * self.a) % _Q, 32, 'little')

    def jwk(self, kid):
        return {'kty': 'OKP', 'crv': 'Ed25519', 'x': b64url(self.public), 'kid': kid, 'alg': 'EdDSA', 'use': 'sig'}


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def sign_jwt(key, kid, claims, typ='glob2-match+jwt'):
    header = {'alg': 'EdDSA', 'typ': typ, 'kid': kid}
    signing_input = b64url(json.dumps(header).encode()) + '.' + b64url(json.dumps(claims).encode())
    return signing_input + '.' + b64url(key.sign(signing_input.encode()))


SIM_VERSION = {'versionMinor': 125, 'netProtocol': 49,
               'dataHash': '3f9a6c1e8b2d47a05e6f1c2b3a4d5e6f708192a3b4c5d6e7f8091a2b3c4d5e6f'}


def ticket_claims(match_id, seat, human_seats, account=None, lifetime=600, sim_version=None,
                  relay_url='ws://127.0.0.1/relay'):
    now = int(time.time())
    account = account or '0b8f6f2e-3c4d-4e5f-8a9b-%012x' % seat
    return {
        'iss': 'https://play.example.org', 'aud': 'glob2-relay', 'sub': account,
        'jti': '00000000-0000-4000-8000-%012x' % (now * 16 + seat), 'iat': now, 'nbf': now,
        'exp': now + lifetime, 'matchId': match_id, 'seat': seat, 'accountId': account,
        'simVersion': sim_version or SIM_VERSION, 'humanSeats': list(human_seats),
        'relayUrl': relay_url, 'entitlements': [],
    }


# --- Turn protocol ------------------------------------------------------------------

HELLO, WELCOME, REJECT, ORDER_SUBMIT, TURN_BUNDLE, CHECKSUM_REPORT, PRESENCE, \
    RESYNC_REQUEST, DESYNC_NOTICE, QUIT, PING, PONG = range(0xA0, 0xAC)


def hello(ticket, have_horizon=0, version=1):
    t = ticket.encode()
    return struct.pack('!BHI', HELLO, version, len(t)) + t + struct.pack('!I', have_horizon)


def order_submit(sequence, order):
    return struct.pack('!BIH', ORDER_SUBMIT, sequence, len(order)) + bytes(order)


def checksum_report(tick, checksum):
    return struct.pack('!BII', CHECKSUM_REPORT, tick, checksum)


def ping(nonce, executed_tick):
    return struct.pack('!BII', PING, nonce, executed_tick)


def quit_message(reason):
    return struct.pack('!BB', QUIT, reason)


def decode(payload):
    kind = payload[0]
    body = payload[1:]
    if kind == WELCOME:
        names = ('protocolVersion', 'seat', 'humanSeatMask', 'tickRateMilliHz', 'bundleInterval',
                 'checksumInterval', 'relayTick', 'resumeFromTick', 'graceTicks', 'lastClientSequence')
        return 'welcome', dict(zip(names, struct.unpack('!HBIIBHIIII', body)))
    if kind == REJECT:
        reason, length = struct.unpack('!BI', body[:5])
        return 'reject', {'reason': reason, 'detail': body[5:5 + length].decode()}
    if kind == TURN_BUNDLE:
        from_tick, horizon, count = struct.unpack('!IIH', body[:10])
        offset, entries = 10, []
        for _ in range(count):
            tick, seat, length = struct.unpack('!IBH', body[offset:offset + 7])
            entries.append((tick, seat, bytes(body[offset + 7:offset + 7 + length])))
            offset += 7 + length
        return 'bundle', {'fromTick': from_tick, 'horizon': horizon, 'entries': entries}
    if kind == PRESENCE:
        count = body[0]
        seats = {}
        for i in range(count):
            seat, state, grace, lag = struct.unpack('!BBII', body[1 + 10 * i:11 + 10 * i])
            seats[seat] = state
        return 'presence', seats
    if kind == DESYNC_NOTICE:
        return 'desync', dict(zip(('tick', 'verdict', 'mask'), struct.unpack('!IBI', body)))
    if kind == PONG:
        return 'pong', dict(zip(('nonce', 'relayTick', 'lastClientSequence'), struct.unpack('!III', body)))
    return 'unknown', payload


class TurnClient:
    """A turn-protocol client over a plain WebSocket, reading on a thread."""

    def __init__(self, port, origin=None, route='/relay', tls=None):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=10)
        if tls:
            self.sock = tls.wrap_socket(self.sock, server_hostname='localhost')
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f'GET {route} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n'
                   f'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n')
        if origin:
            request += f'Origin: {origin}\r\n'
        self.sock.sendall((request + '\r\n').encode())
        response = b''
        while not response.endswith(b'\r\n\r\n'):
            response += receive(self.sock, 1)
        self.status = int(response.split(b' ', 2)[1])
        self.messages = []
        self.bundle_arrivals = []  # (time.monotonic(), horizon) per bundle
        self.closed = False
        self.cond = threading.Condition()
        self.send_lock = threading.Lock()
        if self.status == 101:
            self.thread = threading.Thread(target=self._read, daemon=True)
            self.thread.start()
        else:
            self.closed = True
            self.sock.close()

    def send(self, payload):
        with self.send_lock:
            send_frame(self.sock, struct.pack('!H', len(payload)) + payload)

    def _read(self):
        stream = b''
        try:
            while True:
                opcode, data = read_frame(self.sock)
                if opcode == 8:
                    break
                if opcode == 9:
                    with self.send_lock:
                        send_frame(self.sock, data, opcode=10)
                    continue
                if opcode != 2:
                    continue
                stream += data
                while len(stream) >= 2:
                    length = struct.unpack('!H', stream[:2])[0]
                    if len(stream) < 2 + length:
                        break
                    message = decode(stream[2:2 + length])
                    stream = stream[2 + length:]
                    with self.cond:
                        self.messages.append(message)
                        if message[0] == 'bundle':
                            self.bundle_arrivals.append((time.monotonic(), message[1]['horizon']))
                        self.cond.notify_all()
        except (OSError, EOFError, AssertionError):
            pass
        with self.cond:
            self.closed = True
            self.cond.notify_all()

    def wait_for(self, predicate, timeout=10, what='condition'):
        with self.cond:
            self.cond.wait_for(lambda: predicate(self) or self.closed, timeout)
            if not predicate(self):
                state = 'connection closed' if self.closed else 'timed out'
                raise AssertionError(f'{state} waiting for {what}; last messages: {self.messages[-5:]}')

    def of(self, kind):
        with self.cond:
            return [m[1] for m in self.messages if m[0] == kind]

    def bundles(self):
        return self.of('bundle')

    def horizon(self):
        bundles = self.bundles()
        return bundles[-1]['horizon'] if bundles else 0

    def entries(self):
        return [e for b in self.bundles() for e in b['entries']]

    def wait_closed(self, timeout=10):
        with self.cond:
            if not self.cond.wait_for(lambda: self.closed, timeout):
                raise AssertionError('connection did not close')

    def kill(self):
        """Drops the TCP connection without a WebSocket close."""
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()


# --- Match record (docs/multiplayer/turn-protocol.md, "Match record") ----------------

def parse_record(data):
    assert zlib.crc32(data[:-4]) == struct.unpack('!I', data[-4:])[0], 'record CRC'
    offset = 0

    def take(fmt):
        nonlocal offset
        values = struct.unpack_from('!' + fmt, data, offset)
        offset += struct.calcsize('!' + fmt)
        return values if len(values) > 1 else values[0]

    def text():
        nonlocal offset
        length = take('I')
        value = data[offset:offset + length].decode()
        offset += length
        return value

    assert data[:4] == b'G2MR'
    offset = 4
    record = {'formatVersion': take('H'), 'flags': take('I'), 'matchId': text(), 'simVersion': text()}
    record.update(zip(('tickRateMilliHz', 'bundleInterval', 'checksumInterval', 'humanSeatMask', 'endTick'),
                      take('IBHII')))
    record['setupJson'] = text()
    record['mapHash'] = data[offset:offset + 32].hex()
    offset += 32
    turns = []
    for _ in range(take('I')):
        tick, seat, length = take('IBH')
        turns.append((tick, seat, bytes(data[offset:offset + length])))
        offset += length
    record['turns'] = turns
    record['reports'] = [take('IBI') for _ in range(take('I'))]
    record['events'] = [take('IBB') for _ in range(take('I'))]
    assert offset == len(data) - 4, 'trailing bytes'
    return record


# --- Fake platform --------------------------------------------------------------------

class FakePlatform:
    """The relay-facing part of the platform's /internal/v1 API and its JWKS."""

    def __init__(self, relay_key, jwks, setups=None, tls=None):
        self.relay_key = relay_key
        self.jwks = jwks
        self.setups = setups or {}
        self.registrations, self.heartbeats, self.records, self.ends = [], [], {}, {}
        self.jwks_fetches = 0
        self.unauthorized = 0
        self.lock = threading.Condition()
        platform = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def reply(self, status, body=b'', content_type='application/json'):
                if isinstance(body, (dict, list)):
                    body = json.dumps(body).encode()
                self.send_response(status)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def body(self):
                return self.rfile.read(int(self.headers.get('Content-Length', 0)))

            def authorized(self):
                if self.headers.get('Authorization') != f'Bearer {platform.relay_key}':
                    with platform.lock:
                        platform.unauthorized += 1
                    self.reply(401, {'code': 'unauthenticated', 'message': 'relay key'})
                    return False
                return True

            def do_GET(self):
                if self.path == '/.well-known/jwks.json':
                    with platform.lock:
                        platform.jwks_fetches += 1
                        body = platform.jwks
                    return self.reply(200, body)
                if not self.authorized():
                    return
                parts = self.path.strip('/').split('/')
                if parts[:3] == ['internal', 'v1', 'matches'] and len(parts) == 5 and parts[4] == 'setup':
                    setup = platform.setups.get(parts[3])
                    return self.reply(200, setup) if setup else self.reply(404, {'code': 'not_found', 'message': ''})
                self.reply(404)

            def do_PUT(self):
                if not self.authorized():
                    return
                parts = self.path.strip('/').split('/')
                if parts[:3] == ['internal', 'v1', 'matches'] and len(parts) == 5 and parts[4] == 'record':
                    data = self.body()
                    with platform.lock:
                        platform.records[parts[3]] = (data, self.headers.get('Content-Type'))
                        platform.lock.notify_all()
                    return self.reply(204)
                self.reply(404)

            def do_POST(self):
                if not self.authorized():
                    return
                data = json.loads(self.body() or b'{}')
                parts = self.path.strip('/').split('/')
                with platform.lock:
                    if self.path == '/internal/v1/relays/register':
                        platform.registrations.append(data)
                        platform.lock.notify_all()
                        return self.reply(200, {'relayId': data['relayId'], 'heartbeatIntervalSeconds': 1,
                                                'jwksUrl': platform.url + '/.well-known/jwks.json'})
                    if self.path == '/internal/v1/relays/heartbeat':
                        platform.heartbeats.append(data)
                        platform.lock.notify_all()
                        return self.reply(200, {'ok': True})
                    if parts[:3] == ['internal', 'v1', 'matches'] and len(parts) == 5 and parts[4] == 'end':
                        platform.ends[parts[3]] = data
                        platform.lock.notify_all()
                        return self.reply(204)
                self.reply(404)

        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        if tls:
            self.server.socket = tls.wrap_socket(self.server.socket, server_side=True)
        scheme = 'https' if tls else 'http'
        self.url = f'{scheme}://localhost:{self.server.server_address[1]}'
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def wait(self, predicate, timeout=15, what='platform condition'):
        with self.lock:
            if not self.lock.wait_for(lambda: predicate(self), timeout):
                raise AssertionError(f'Timed out waiting for {what}')

    def close(self):
        self.server.shutdown()
        self.server.server_close()
