#!/usr/bin/env python3
"""Verify overlap, archive integrity, and retry without resimulation."""
from pathlib import Path
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import threading
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import harvest_maxima_checkpoints as harvest
import maxima_harvest_transfer as transfer


class MaximaHarvestPipelineTest(unittest.TestCase):
    def test_archive_round_trip_and_compression(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'source'; source.mkdir(); (source/'saves').mkdir()
            payload = b'checkpoint state repeated\0'*10000
            (source/'saves/checkpoint-0.game').write_bytes(payload)
            (source/'engine.log').write_text('match log\n')
            (source/'engine.exit').write_text('0\n')
            archive = root/'bundle.gz'
            with archive.open('wb') as stream:
                subprocess.run([sys.executable, '-c', transfer.PACK_SCRIPT, str(source)],
                               stdout=stream, check=True)
            target = root/'target'; target.mkdir()
            hashes = transfer.unpack_verified(archive, target)
            self.assertEqual((target/'saves/checkpoint-0.game').read_bytes(), payload)
            self.assertEqual(len(hashes), 3)
            self.assertLess(archive.stat().st_size, len(payload)//5)

    def test_corrupt_bundle_does_not_replace_existing_checkpoints(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); (root/'saves').mkdir()
            original = root/'saves/checkpoint-0.game'; original.write_bytes(b'original')
            archive = root/'bad.gz'
            entries = {'engine.log': b'log', 'engine.exit': b'0', 'saves/checkpoint-0.game': b'corrupt'}
            entries['SHA256SUMS'] = ''.join('0'*64+'  '+name+'\n' for name in entries).encode()
            with tarfile.open(archive, 'w:gz') as bundle:
                for name, payload in entries.items():
                    info = tarfile.TarInfo(name); info.size = len(payload)
                    bundle.addfile(info, io.BytesIO(payload))
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                transfer.unpack_verified(archive, root)
            self.assertEqual(original.read_bytes(), b'original')

    def test_worker_starts_next_game_while_collection_is_blocked(self):
        with tempfile.TemporaryDirectory() as temp:
            cluster = object.__new__(harvest.HarvestCluster)
            cluster.output = Path(temp); cluster.workers = [('remote', 1)]
            cluster.args = SimpleNamespace(transfer_queue_per_slot=2)
            cluster._active_slots = lambda: [('remote', 0)]
            cluster._prepare_connection = lambda host, group: True
            cluster.request_hash = lambda item: 'request'
            second_started = threading.Event(); collecting = threading.Event()
            simulated = []
            def simulate(host, group, item, settings):
                simulated.append(item['id'])
                if item['id'] == 1:
                    self.assertTrue(collecting.wait(2))
                    second_started.set()
                return dict(item, remote_run='remote', worker_host=host)
            def collect(host, group, item):
                collecting.set()
                self.assertTrue(second_started.wait(2), 'compute slot waited for collection')
                return dict(item, name=f"run-{item['id']:05d}", error='')
            cluster._run_one = simulate; cluster._collect = collect
            rows = cluster.run([{'id': 0}, {'id': 1}], '')
            self.assertEqual(simulated, [0, 1]); self.assertEqual(len(rows), 2)
            self.assertTrue(all(not row['error'] for row in rows))

    def test_pending_collection_resumes_without_running_engine(self):
        with tempfile.TemporaryDirectory() as temp:
            cluster = object.__new__(harvest.HarvestCluster)
            cluster.output = Path(temp); cluster.workers = [('remote', 1)]
            cluster.args = SimpleNamespace(transfer_queue_per_slot=2)
            cluster._active_slots = lambda: [('remote', 0)]
            cluster._prepare_connection = lambda host, group: True
            cluster.request_hash = lambda item: 'request'
            path = cluster.pending_path({'id': 0}); path.parent.mkdir()
            path.write_text(json.dumps({'id': 0, 'worker_host': 'remote', 'request_hash': 'request'}))
            cluster._run_one = lambda *args: self.fail('engine must not run for pending collection')
            cluster._collect = lambda host, group, item: dict(item, name='run-00000', error='')
            self.assertEqual(len(cluster.run([{'id': 0}], '')), 1)


if __name__ == '__main__':
    unittest.main()
