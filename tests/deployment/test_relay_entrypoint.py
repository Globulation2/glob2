"""deploy/relay-entrypoint.sh: stable relay ids from spool-volume slots, and adoption
of spooled matches that no running relay holds. Needs flock(1) (Linux util-linux);
skipped elsewhere.

    python3 -m unittest tests/deployment/test_relay_entrypoint.py -v
"""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
ENTRYPOINT = ROOT / 'deploy/relay-entrypoint.sh'
# What the "relay" does here: report its identity, then hold its slot until told to stop.
REPORT = 'echo "$GLOB2_RELAY_ID $GLOB2_RELAY_SPOOL_DIR $GLOB2_RELAY_PUBLIC_URL" > "$1"; while [ ! -e "$2" ]; do sleep 0.05; done'


@unittest.skipUnless(shutil.which('flock'), 'needs flock(1)')
class RelayEntrypointTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='relay-entrypoint-'))
        self.spool = self.dir / 'spool'
        self.spool.mkdir()
        self.relays = []

    def tearDown(self):
        for relay in list(self.relays):
            self.stop(relay)
        shutil.rmtree(self.dir, ignore_errors=True)

    def start(self, name):
        report = self.dir / f'{name}.report'
        stop = self.dir / f'{name}.stop'
        env = {
            'PATH': os.environ['PATH'],
            'GLOB2_RELAY_SPOOL_DIR': str(self.spool),
            'GLOB2_PUBLIC_ORIGIN': 'https://play.example.org',
        }
        process = subprocess.Popen(['sh', str(ENTRYPOINT), 'sh', '-c', REPORT, 'relay', str(report), str(stop)],
                                   env=env, stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 10
        while not report.exists() or not report.read_text().strip():
            if process.poll() is not None:
                self.fail(f'entrypoint exited: {process.stderr.read()}')
            if time.time() > deadline:
                self.fail('relay did not start')
            time.sleep(0.02)
        relay_id, spool, url = report.read_text().split()
        relay = {'id': relay_id, 'spool': Path(spool), 'url': url, 'process': process, 'stop': stop}
        self.relays.append(relay)
        return relay

    def stop(self, relay):
        relay['stop'].touch()
        relay['process'].wait(timeout=10)
        relay['process'].stderr.close()
        self.relays.remove(relay)

    def spooled(self, directory, match):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f'{match}.g2mr').write_bytes(b'G2MR')
        (directory / f'{match}.end.json').write_text('{}')

    def test_ids_are_stable_slots_and_free_again_when_a_relay_exits(self):
        first = self.start('a')
        second = self.start('b')
        self.assertEqual({first['id'], second['id']}, {'relay-1', 'relay-2'})
        self.assertEqual(first['spool'], self.spool / first['id'])
        # The URL label is the host name Caddy routes to, not the relay id.
        self.assertEqual(first['url'], f'wss://play.example.org/relay/{socket.gethostname()}')
        # A record spooled by relay-1 survives its restart and is found again.
        self.spooled(first['spool'], 'match-1')
        freed = first['id']
        self.stop(first)
        again = self.start('c')
        self.assertEqual(again['id'], freed)
        self.assertTrue((again['spool'] / 'match-1.end.json').exists())

    def test_spools_no_running_relay_holds_are_adopted(self):
        busy = self.start('busy')
        self.spooled(busy['spool'], 'match-busy')
        legacy = self.spool / '3f2a9c1b7e44'  # a container id, from before stable ids
        self.spooled(legacy, 'match-legacy')
        (legacy / 'match-half.g2mr').write_bytes(b'G2MR')  # no end report: not adopted
        self.spooled(self.spool / 'relay-5', 'match-orphan')  # scaled down
        relay = self.start('new')
        adopted = sorted(p.name for p in relay['spool'].iterdir())
        self.assertEqual(adopted, ['match-legacy.end.json', 'match-legacy.g2mr',
                                   'match-orphan.end.json', 'match-orphan.g2mr'])
        self.assertFalse((self.spool / 'relay-5').exists())
        self.assertTrue((legacy / 'match-half.g2mr').exists())
        # A spool held by a running relay is left alone.
        self.assertTrue((busy['spool'] / 'match-busy.end.json').exists())

    def test_an_explicit_id_wins(self):
        report = self.dir / 'explicit.report'
        stop = self.dir / 'explicit.stop'
        stop.touch()
        env = {'PATH': os.environ['PATH'], 'GLOB2_RELAY_SPOOL_DIR': str(self.spool),
               'GLOB2_RELAY_ID': 'eu-west-a', 'GLOB2_RELAY_PUBLIC_URL': 'wss://relay.example.org/relay'}
        subprocess.run(['sh', str(ENTRYPOINT), 'sh', '-c', REPORT, 'relay', str(report), str(stop)],
                       env=env, check=True, timeout=10)
        self.assertEqual(report.read_text().split(),
                         ['eu-west-a', str(self.spool / 'eu-west-a'), 'wss://relay.example.org/relay'])


if __name__ == '__main__':
    unittest.main()
