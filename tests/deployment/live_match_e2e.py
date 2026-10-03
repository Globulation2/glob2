#!/usr/bin/env python3
"""End-to-end match against a deployed platform instance.

--mode room (default): two guests sign in over REST and the realtime socket.
Guest A creates a link-only room with a generated map and AI seats, guest B
joins it by invite code, and A starts the match. A sudden-death rule ends the
game after a few game minutes. Room matches are unrated.

--mode queue: two new local accounts join a rated 1v1 queue (--queue), accept
the ranked prompt and get a quick match. Player A quits after
--client-seconds A. B's colony is then the last one standing and wins; if the
game went on, B would quit later still and A's early leave would decide the
rating instead. Either way the verified match is rated and both players'
ratings change.

Each player's match.start assignment then goes to a headless native client
(`glob2 --turn-client`), which plays its seat in real time through the
instance's relay with a small order bot, while every client computes the AI
seats. --engine-command runs the binary another way, e.g. inside the
engine-agent image, so that it matches the verifier's sim version exactly.

Afterwards the script checks each client's result, that the relay reported the
match and uploaded its record, that the verify-match job judged it and, in queue
mode, that ratings were applied. The last three need database access: pass
--psql with a command that runs psql on the deployment (it receives SQL on
stdin), e.g.

  --psql "docker compose -p glob2-platform exec -T postgres psql -U glob2 -d glob2 -At"

Python standard library only.

  python3 tests/deployment/live_match_e2e.py --origin https://play.example.org \\
      --glob2 build/darwin/client/release/src/glob2 --out artifacts/live-e2e [--psql ...]

platform_stack_smoke.py --match-e2e runs the queue mode against a fresh Compose
stack: the one-command local end-to-end test (docs/hosting/README.md).
"""
import argparse
import base64
import json
import os
from pathlib import Path
import queue
import shlex
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/transport'))
from websocket_wire import read_frame, receive, send_frame  # noqa: E402


def log(message):
    print(f'[{time.strftime("%H:%M:%S")}] {message}', flush=True)


class Failure(Exception):
    pass


class Instance:
    def __init__(self, origin, ca_file=None):
        self.origin = origin.rstrip('/')
        authority = self.origin.split('://', 1)[1]
        self.host = authority.split(':')[0]
        self.port = int(authority.split(':')[1]) if ':' in authority else 443
        self.tls = ssl.create_default_context(cafile=ca_file)

    def request(self, method, path, body=None, token=None, raw=False):
        headers = {'User-Agent': 'glob2-live-e2e'}
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers['Content-Type'] = 'application/json'
        if token:
            headers['Authorization'] = 'Bearer ' + token
        url = path if path.startswith('https://') else self.origin + path
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, context=self.tls, timeout=60) as response:
                payload = response.read()
                return response.status, payload if raw else json.loads(payload or b'null')
        except urllib.error.HTTPError as error:
            payload = error.read()
            raise Failure(f'{method} {path}: {error.code} {payload[:300]!r}') from None


class Realtime:
    """A realtime socket: requests with correlated responses, and buffered events."""

    def __init__(self, instance, name):
        self.name = name
        self.sock = instance.tls.wrap_socket(socket.create_connection((instance.host, instance.port), timeout=30),
                                             server_hostname=instance.host)
        key = base64.b64encode(os.urandom(16)).decode()
        authority = instance.host if instance.port == 443 else f'{instance.host}:{instance.port}'
        self.sock.sendall((f'GET /realtime HTTP/1.1\r\nHost: {authority}\r\nUpgrade: websocket\r\n'
                           f'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n\r\n')
                          .encode())
        headers = b''
        while not headers.endswith(b'\r\n\r\n'):
            headers += receive(self.sock, 1)
        if int(headers.split(b' ')[1]) != 101:
            raise Failure(f'/realtime upgrade answered {headers[:60]!r}')
        self.sock.settimeout(None)
        self.lock = threading.Lock()
        self.responses = {}
        self.events = queue.Queue()
        self.next_id = 0
        self.closed = False
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        try:
            while True:
                opcode, data = read_frame(self.sock)
                if opcode == 9:
                    with self.lock:
                        send_frame(self.sock, data, opcode=10)
                    continue
                if opcode == 8:
                    break
                if opcode != 1:
                    continue
                message = json.loads(data)
                if message.get('type') == 'response':
                    self.responses[message['id']] = message
                elif message.get('type') == 'event':
                    self.events.put(message)
        except (EOFError, OSError, ValueError):
            pass
        self.closed = True

    def call(self, method, params, timeout=60):
        with self.lock:
            self.next_id += 1
            request_id = f'{self.name}-{self.next_id}'
            send_frame(self.sock, json.dumps({'type': 'request', 'id': request_id, 'method': method,
                                              'params': params}).encode(), opcode=1)
        deadline = time.monotonic() + timeout
        while request_id not in self.responses:
            if time.monotonic() > deadline or self.closed:
                raise Failure(f'{self.name}: no response to {method}')
            time.sleep(0.02)
        response = self.responses.pop(request_id)
        if not response.get('ok'):
            raise Failure(f'{self.name}: {method} failed: {response.get("error")}')
        return response['result']

    def event(self, name, timeout=60, match=lambda data: True):
        deadline = time.monotonic() + timeout
        skipped = []
        try:
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise Failure(f'{self.name}: no {name} event within {timeout} s')
                try:
                    message = self.events.get(timeout=remaining)
                except queue.Empty:
                    continue
                if message.get('event') == name and match(message.get('data') or {}):
                    return message['data']
                skipped.append(message)
        finally:
            for message in skipped:
                self.events.put(message)

    def close(self):
        try:
            with self.lock:
                send_frame(self.sock, b'\x03\xe8', opcode=8)
            self.sock.close()
        except OSError:
            pass


class Match:
    def __init__(self, arguments):
        self.arguments = arguments
        self.instance = Instance(arguments.origin, arguments.ca_file)
        self.out = Path(arguments.out).resolve()
        self.out.mkdir(parents=True, exist_ok=True)
        self.results = {}
        # The client binary, or a command prefix that runs it (it receives host paths).
        self.glob2 = (shlex.split(arguments.engine_command) if arguments.engine_command
                      else [str(Path(arguments.glob2).resolve())])
        self.workdir = str(Path(arguments.engine_workdir).resolve())

    def check(self, name, function):
        log(f'CHECK {name}')
        started = time.monotonic()
        try:
            detail = function()
        except Exception as error:  # noqa: BLE001 - every failure is reported
            self.results[name] = {'ok': False, 'error': str(error)}
            log(f'FAIL  {name}: {error}')
            return False
        self.results[name] = {'ok': True, 'seconds': round(time.monotonic() - started, 1), 'detail': detail}
        log(f'PASS  {name}: {json.dumps(detail)[:400]}')
        return True

    def psql(self, sql):
        if not self.arguments.psql:
            return None
        result = subprocess.run(shlex.split(self.arguments.psql), input=sql, text=True, capture_output=True,
                                timeout=120)
        if result.returncode != 0:
            raise Failure(f'psql failed: {result.stderr[-500:]}')
        return result.stdout.strip()

    # ------------------------------------------------------------ steps

    def sim_version(self):
        output = subprocess.check_output([*self.glob2, '--sim-version'], cwd=self.workdir, text=True, timeout=300)
        self.sim = json.loads(output.strip().splitlines()[-1])
        info = self.instance.request('GET', '/api/v1/instance')[1]
        served = [v for v in info['supportedSimVersions'] if v == self.sim]
        if not served:
            raise Failure(f'the instance does not serve this client\'s sim version {self.sim}; '
                          f'it lists {info["supportedSimVersions"]}')
        return {'simVersion': f'{self.sim["versionMinor"]}-{self.sim["netProtocol"]}-{self.sim["dataHash"]}'}

    def guests(self):
        self.players = {}
        for name in ('A', 'B'):
            signed = self.instance.request('POST', '/api/v1/auth/guest', {'platform': 'desktop'})[1]
            socket_ = Realtime(self.instance, name)
            hello = socket_.call('session.hello', {
                'protocol': 1, 'accessToken': signed['tokens']['accessToken'],
                'client': {'platform': 'desktop', 'version': 'live-e2e', 'simVersion': self.sim}})
            if not hello.get('simSupported'):
                raise Failure(f'guest {name}: session.hello says simSupported false: {hello}')
            self.players[name] = {'account': signed['account'], 'socket': socket_,
                                  'token': signed['tokens']['accessToken']}
        return {n: {'id': p['account']['id'], 'name': p['account'].get('displayName')}
                for n, p in self.players.items()}

    def accounts(self):
        """Queue mode: two fresh local accounts (rated queues refuse guests)."""
        self.players = {}
        suffix = os.urandom(4).hex()
        for name in ('A', 'B'):
            signed = self.instance.request('POST', '/api/v1/auth/local/register', {
                'username': f'e2e-{name.lower()}-{suffix}', 'password': os.urandom(12).hex(),
                'platform': 'desktop'})[1]
            socket_ = Realtime(self.instance, name)
            hello = socket_.call('session.hello', {
                'protocol': 1, 'accessToken': signed['tokens']['accessToken'],
                'client': {'platform': 'desktop', 'version': 'live-e2e', 'simVersion': self.sim}})
            if not hello.get('simSupported'):
                raise Failure(f'player {name}: session.hello says simSupported false: {hello}')
            self.players[name] = {'account': signed['account'], 'socket': socket_,
                                  'token': signed['tokens']['accessToken']}
        return {n: {'id': p['account']['id'], 'kind': p['account'].get('kind')} for n, p in self.players.items()}

    def quick_match(self):
        """Queue mode: both join the rated queue, accept the prompt and get match.start."""
        tickets = {}
        for name in ('A', 'B'):
            joined = self.players[name]['socket'].call('queue.join', {
                'queueId': self.arguments.queue, 'regions': [], 'allowAiOpponent': False})
            tickets[name] = joined['ticketId']
        proposals = {}
        for name in ('A', 'B'):
            proposal = self.players[name]['socket'].event(
                'queue.proposal', timeout=self.arguments.queue_timeout,
                match=lambda d, ticket=tickets[name]: d.get('ticketId') == ticket)
            proposals[name] = proposal
            if proposal.get('requiresAccept'):
                self.players[name]['socket'].call('queue.respond',
                                                  {'proposalId': proposal['proposalId'], 'accept': True})
        if proposals['A']['proposalId'] != proposals['B']['proposalId']:
            raise Failure(f'the players were proposed different matches: {proposals}')
        if proposals['A'].get('rated') is not True:
            raise Failure(f'queue {self.arguments.queue} is not rated: {proposals["A"]}')
        # match.start can take a while: without a warm map the starter generates one.
        found = self.players['A']['socket'].event('queue.matchFound', timeout=self.arguments.queue_timeout,
                                                  match=lambda d: d.get('ticketId') == tickets['A'])
        self.match_id = found['matchId']
        self.assignments = {}
        for name in ('A', 'B'):
            data = self.players[name]['socket'].event('match.start', timeout=self.arguments.queue_timeout,
                                                      match=lambda d: d.get('matchId') == self.match_id)
            self.assignments[name] = data
            (self.out / f'assignment-{name}.json').write_text(json.dumps(data, indent=2))
        return {'queue': self.arguments.queue, 'proposalId': proposals['A']['proposalId'],
                'requiresAccept': proposals['A'].get('requiresAccept'), 'matchId': self.match_id,
                'relayUrl': self.assignments['A']['relayUrl'],
                'seats': [self.assignments[n]['seat'] for n in ('A', 'B')]}

    def room(self):
        a, b = self.players['A']['socket'], self.players['B']['socket']
        teams = 2 + self.arguments.ais
        seed = int(time.time()) % 1000000
        rules = {'prestigeVictory': True, 'suddenDeathMinutes': self.arguments.sudden_death_minutes,
                 'mapDiscovered': False, 'allyTeamsFixed': True, 'resourceGrowthDisabled': False,
                 'resourceScarcityLevel': 0, 'instantConstruction': False, 'stockpileStartLevel': 0,
                 'hungerDisabled': False, 'unitUpgradesDisabled': False, 'glassCannonLevel': 0,
                 'unitsFearless': False, 'permadeathDisabled': False, 'peacefulMode': False, 'buildingHpLevel': 0}
        generator = {'generatorId': self.arguments.generator, 'revision': self.arguments.generator_revision,
                     'params': {'width': 7, 'height': 7, 'teams': teams}, 'seed': seed, 'candidates': 3,
                     'startingUnitLevel': 0}
        room = a.call('room.create', {'name': 'Live end-to-end', 'visibility': 'link',
                                      'map': {'kind': 'generated', 'generator': generator}, 'rules': rules})['room']
        self.room_state = room
        # Wait for the generate-map job.
        deadline = time.monotonic() + 600
        while room.get('mapStatus') != 'ready':
            if room.get('mapStatus') == 'failed':
                raise Failure(f'map generation failed: {room.get("mapProblem")}')
            if time.monotonic() > deadline:
                raise Failure('the generated map was not ready within 600 s')
            room = a.event('room.state', timeout=600, match=lambda d: d.get('room', d).get('id') == room['id'])
            room = room.get('room', room)
        landing = self.instance.request('GET', f'/j/{room["code"]}', raw=True)[1]
        if room['code'].encode() not in landing:
            raise Failure('the invite page does not name its code')
        invite = self.instance.request('GET', f'/api/v1/invites/{room["code"]}')[1]
        joined = b.call('room.join', {'code': room['code']})['room']
        b.call('room.setSeat', {'roomId': room['id'], 'seat': 1, 'occupant': {'kind': 'self'}})
        for seat in range(2, teams):
            ai = self.arguments.ai_ids[(seat - 2) % len(self.arguments.ai_ids)]
            a.call('room.setSeat', {'roomId': room['id'], 'seat': seat, 'occupant': {'kind': 'ai', 'ai': ai}})
        b.call('room.setReady', {'roomId': room['id'], 'ready': True})
        started = a.call('room.start', {'roomId': room['id']})
        self.match_id = started['matchId']
        self.assignments = {}
        for name in ('A', 'B'):
            data = self.players[name]['socket'].event('match.start', timeout=120,
                                                      match=lambda d: d.get('matchId') == self.match_id)
            self.assignments[name] = data
            (self.out / f'assignment-{name}.json').write_text(json.dumps(data, indent=2))
        return {'roomId': room['id'], 'code': room['code'], 'inviteUrl': room.get('inviteUrl'),
                'invite': {k: invite.get(k) for k in ('code', 'roomName', 'status') if k in invite},
                'mapHash': room['map'].get('hash'), 'members': len(joined.get('members', [])),
                'matchId': self.match_id,
                'relayUrl': self.assignments['A']['relayUrl'],
                'seats': [self.assignments[n]['seat'] for n in ('A', 'B')]}

    def play(self):
        assignment = self.assignments['A']
        status, raw = self.instance.request('GET', assignment['mapUrl'], token=self.players['A']['token'], raw=True)
        # The blob endpoint serves the stored bytes, gzip or raw.
        map_path = self.out / ('match.map.gz' if raw[:2] == b'\x1f\x8b' else 'match.map')
        map_path.write_bytes(raw)
        processes = {}
        seconds = self.arguments.client_seconds or [self.arguments.max_seconds] * 2
        for index, name in enumerate(('A', 'B')):
            directory = self.out / f'client-{name}'
            if directory.exists():
                raise Failure(f'{directory} exists; use a fresh --out')
            command = [*self.glob2, '--turn-client', str(self.out / f'assignment-{name}.json'), '--map',
                       str(map_path), '--out', str(directory), '--orders-per-second',
                       str(self.arguments.orders_per_second), '--max-seconds', str(seconds[index]),
                       '--seed', str(index + 1)]
            log_file = open(self.out / f'client-{name}.log', 'w')
            processes[name] = (subprocess.Popen(command, cwd=self.workdir, stdout=log_file, stderr=subprocess.STDOUT),
                               log_file)
        deadline = time.monotonic() + max(seconds) + 120
        codes = {}
        while len(codes) < len(processes):
            for name, (process, log_file) in processes.items():
                if name not in codes and process.poll() is not None:
                    codes[name] = process.returncode
                    log_file.close()
                    log(f'client {name} exited with {process.returncode}')
            if time.monotonic() > deadline:
                for name, (process, _) in processes.items():
                    if name not in codes:
                        process.kill()
                raise Failure('the clients did not finish in time')
            time.sleep(1)
        clients = {}
        for name in ('A', 'B'):
            result = json.loads((self.out / f'client-{name}' / 'result.json').read_text())
            clients[name] = {k: result.get(k) for k in ('status', 'ended_by', 'executed_ticks', 'game_ended',
                                                        'desync_flagged', 'reloads', 'orders_queued', 'rtt_ms',
                                                        'jitter_ms', 'wall_seconds', 'diagnostic')}
            clients[name]['exit'] = codes[name]
            clients[name]['teams'] = [{'team': t.get('team'), 'outcome': t.get('outcome')}
                                      for t in result.get('teams', [])]
            if codes[name] != 0 or result.get('status') != 'completed':
                raise Failure(f'client {name} failed: {clients[name]}')
        traces = {}
        for name in ('A', 'B'):
            lines = (self.out / f'client-{name}' / 'checksums.txt').read_text().splitlines()[1:]
            traces[name] = dict(line.split() for line in lines)
        common = sorted(set(traces['A']) & set(traces['B']), key=int)
        mismatches = [t for t in common if traces['A'][t] != traces['B'][t]]
        if not common or mismatches:
            raise Failure(f'client checksums differ at {mismatches[:5]} ({len(common)} common ticks)')
        return {'clients': clients, 'commonChecksumTicks': len(common), 'lastCommonTick': int(common[-1])}

    def platform_record(self):
        if not self.arguments.psql:
            return {'skipped': 'no --psql'}
        deadline = time.monotonic() + self.arguments.verify_timeout
        while True:
            row = self.psql(f"SELECT row_to_json(m) FROM (SELECT status, end_reason, final_tick, verification, "
                            f"desync_flagged, relay_id, ended_at FROM matches WHERE id = '{self.match_id}') m;")
            match = json.loads(row) if row else {}
            if match.get('verification') not in (None, 'pending'):
                break
            if time.monotonic() > deadline:
                raise Failure(f'no verdict within {self.arguments.verify_timeout} s: {match}')
            time.sleep(5)
        artifacts = self.psql(f"SELECT json_agg(kind ORDER BY kind) FROM match_artifacts WHERE match_id = "
                              f"'{self.match_id}';")
        participants = self.psql(f"SELECT json_agg(row_to_json(p) ORDER BY seat) FROM (SELECT seat, team, kind, "
                                 f"ai_id, display_name, outcome, quit_tick, disconnects FROM match_participants "
                                 f"WHERE match_id = '{self.match_id}') p;")
        job = self.psql(f"SELECT row_to_json(j) FROM (SELECT status, result -> 'verdict' AS verdict, "
                        f"result -> 'reason' AS reason, result -> 'clients' AS clients, agent_id, completed_at - created_at AS took "
                        f"FROM engine_jobs WHERE kind = 'verify-match' AND payload ->> 'matchId' = "
                        f"'{self.match_id}' ORDER BY created_at DESC LIMIT 1) j;")
        history = self.psql(f"SELECT count(*) FROM match_results_view WHERE match_id = '{self.match_id}';")
        detail = {'match': match, 'artifacts': json.loads(artifacts or 'null'),
                  'participants': json.loads(participants or 'null'), 'verifyJob': json.loads(job or 'null'),
                  'historyRows': int(history or 0)}
        if match.get('status') != 'ended' or match.get('verification') != 'verified':
            raise Failure(f'match not ended and verified: {detail}')
        if 'record' not in (detail['artifacts'] or []):
            raise Failure(f'no record artifact: {detail}')
        if self.arguments.mode == 'queue':
            detail['ratings'] = self.ratings()
        return detail

    def ratings(self):
        """Queue mode: the verified result moved both players' ratings on the queue's ladder."""
        deadline = time.monotonic() + 120
        while True:
            row = self.psql(f"SELECT row_to_json(m) FROM (SELECT rated, rating_status, rating_note FROM matches "
                            f"WHERE id = '{self.match_id}') m;")
            match = json.loads(row) if row else {}
            if match.get('rating_status') not in (None, 'pending'):
                break
            if time.monotonic() > deadline:
                raise Failure(f'ratings still pending: {match}')
            time.sleep(2)
        history = json.loads(self.psql(
            f"SELECT coalesce(json_agg(row_to_json(h) ORDER BY h.result), '[]') FROM (SELECT ladder, result, "
            f"round(mu_before::numeric, 3) AS mu_before, round(mu_after::numeric, 3) AS mu_after "
            f"FROM rating_history WHERE match_id = '{self.match_id}') h;") or '[]')
        detail = {'match': match, 'history': history}
        if match.get('rating_status') != 'applied' or len(history) != 2:
            raise Failure(f'ratings not applied: {detail}')
        if sorted(h['result'] for h in history) != ['lost', 'won'] or any(
                h['ladder'] != self.arguments.queue or h['mu_before'] == h['mu_after'] for h in history):
            raise Failure(f'unexpected rating changes: {detail}')
        return detail

    def run(self):
        ok = True
        try:
            start = ((('accounts and realtime', self.accounts), ('rated quick match', self.quick_match))
                     if self.arguments.mode == 'queue' else
                     (('guests and realtime', self.guests), ('room, invite and start', self.room)))
            for name, function in (('sim version served', self.sim_version), *start,
                                   ('match through the relay', self.play),
                                   ('record, verification and history', self.platform_record)):
                if not self.check(name, function):
                    ok = False
                    break
        finally:
            for player in getattr(self, 'players', {}).values():
                player['socket'].close()
            (self.out / 'results.json').write_text(json.dumps(self.results, indent=2))
        return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--origin', required=True, help='instance origin, e.g. https://play.example.org')
    parser.add_argument('--glob2', help='glob2 binary built from the deployed sim version')
    parser.add_argument('--engine-command', help='command prefix that runs glob2 instead of --glob2 '
                        '(e.g. docker run ... <engine-agent image>); it receives host paths')
    parser.add_argument('--ca-file', help="CA bundle for the instance's certificate (e.g. Caddy's local CA)")
    parser.add_argument('--mode', choices=('room', 'queue'), default='room')
    parser.add_argument('--queue', default='ranked-1v1', help='queue mode: a rated 1v1 queue id')
    parser.add_argument('--queue-timeout', type=float, default=300,
                        help='queue mode: seconds to wait for the proposal and match.start (includes map generation)')
    parser.add_argument('--client-seconds', type=float, nargs=2, metavar=('A', 'B'),
                        help='per-client --max-seconds (queue mode default: 40 70)')
    parser.add_argument('--engine-workdir', default=str(ROOT), help='directory with data/ (default: repository)')
    parser.add_argument('--out', required=True, help='fresh directory for assignments, logs and results')
    parser.add_argument('--psql', help='command running psql on the deployment, reading SQL from stdin')
    parser.add_argument('--ais', type=int, default=2, help='AI seats besides the two guests (default 2)')
    parser.add_argument('--ai-ids', nargs='+', default=['nicowar', 'warrush'])
    parser.add_argument('--generator', default='even-ground')
    parser.add_argument('--generator-revision', type=int, default=2)
    parser.add_argument('--sudden-death-minutes', type=int, default=3)
    parser.add_argument('--orders-per-second', type=float, default=0.5)
    parser.add_argument('--max-seconds', type=float, default=600, help='clients quit after this long')
    parser.add_argument('--verify-timeout', type=float, default=600)
    arguments = parser.parse_args()
    if not arguments.glob2 and not arguments.engine_command:
        parser.error('--glob2 or --engine-command is required')
    if arguments.mode == 'queue' and not arguments.client_seconds:
        # A leaves first, so B wins; B quits 30 s later at the latest (over MUTUAL_LEAVE_TICKS).
        arguments.client_seconds = [40.0, 70.0]
    match = Match(arguments)
    ok = match.run()
    log('all checks passed' if ok else 'FAILED')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
