#!/usr/bin/env python3
"""Adversarial concurrency tests for independent tournament stages."""
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import sys
import threading
import time
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import test_tournaments as fixtures
from tools.tournaments.common import atomic_json, read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.model import job
from tools.tournaments.transport import Transport
from tools.tournaments.worker import Worker, pack


class PipelineTests(unittest.TestCase):
    setUp = fixtures.Fixture.setUp
    tearDown = fixtures.Fixture.tearDown
    result = fixtures.Fixture.result

    def expand(self, count):
        self.coordinator.close()
        root = self.root / 'expanded'
        manifest = dict(self.manifest, jobs=[job('generate_map',self.bundle['id'],seeds={'map':i},
                                               config={'generator':15}) for i in range(count)],
                        settings={'heartbeat_seconds':.1,'lease_seconds':2,'poll_seconds':.05,
                                  'prefetch':0,'input_transfer_slots':2,'result_transfer_slots':2})
        self.coordinator = Coordinator.submit(root,manifest,[self.root/'bundles'/self.bundle['id']])
        return self.coordinator

    def test_completed_backlog_frees_compute_buffer_but_missing_delivery_does_not(self):
        c=self.expand(4)
        status=self.status | {'slots':2}
        first=c.dispatch('local',status)
        self.assertEqual(len(first),2)
        self.assertEqual(c.dispatch('local',status),[])
        status['attempts']=[dict(a,state='packing') for a in first]
        self.assertEqual(len(c.dispatch('local',status)),2)

    def test_worker_reload_and_backlog_backpressure(self):
        worker=Worker(self.root/'worker')
        try:
            other=Worker(worker.root)
            other.configure({'slots':5,'collect_slots':3,'result_backlog_limit':1})
            other.close()
            worker.tick()
            self.assertEqual(worker.config['slots'],5)
            self.assertEqual(worker.config['collect_slots'],3)
            worker.db.execute("INSERT INTO queue VALUES ('test','token','fixture','{}','done',NULL,NULL,0)")
            self.assertFalse(worker.status()['accepting'])
        finally:worker.close()

    def test_stream_reuse_disconnect_and_reconnect(self):
        t=Transport({'name':'local','transport':'local','directory':str(self.root/'remote')},self.coordinator.root/'worker.pyz')
        try:
            t.rpc('status'); pid=t.process.pid
            t.rpc('status'); self.assertEqual(pid,t.process.pid)
            t.process.kill();t.process.wait()
            with self.assertRaises((OSError,ConnectionError)):t.rpc('status')
            t.rpc('status');self.assertNotEqual(pid,t.process.pid)
        finally:
            t.rpc('stop');t.close()

    def test_stream_timeout_is_bounded_even_when_stdin_is_not_read(self):
        t=Transport({'name':'local','transport':'local','directory':str(self.root/'timeout'),
                     'transport_timeout_seconds':.15},self.coordinator.root/'worker.pyz')
        t.command=lambda args:[sys.executable,'-c','import time;time.sleep(10)']
        import subprocess
        started=time.monotonic()
        try:
            with self.assertRaises(subprocess.TimeoutExpired):t.exchange(b'x'*1048576)
            self.assertLess(time.monotonic()-started,2)
            self.assertIsNone(t.process)
        finally:t.close()

    def test_two_packers_overlap_and_ack_keeps_shared_objects(self):
        c=self.expand(2);attempts=c.dispatch('local',self.status|{'slots':2})
        worker=Worker(self.root/'pack-worker')
        shutil.copytree(self.root/'bundles'/self.bundle['id'],worker.root/'bundles'/self.bundle['id'])
        for a in attempts:
            worker.enqueue(a)
            d=worker.root/'attempts'/a['id'];(d/'output').mkdir()
            (d/'output/shared.log').write_bytes(b'same data'*1000)
            atomic_json(d/'execution.json',{'category':'success','finished':time.time()})
        from tools.tournaments.worker import store_artifact
        barrier=threading.Barrier(2)
        def compress(path,*args,**kwargs):
            if path.name=='shared.log':barrier.wait(timeout=5)
            return store_artifact(path,*args,**kwargs)
        try:
            with patch('tools.tournaments.worker.store_artifact',side_effect=compress):
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                    list(pool.map(lambda a:pack(worker.root,a['id']),attempts))
            worker.db.execute("UPDATE queue SET state='done'")
            records=[read_json(worker.root/'attempts'/a['id']/'record.json') for a in attempts]
            sha=next(x['sha256'] for x in records[0]['artifacts'] if x['path']=='shared.log')
            worker.acknowledge(attempts[0]['id'],attempts[0]['token'])
            self.assertTrue((worker.root/'spool'/sha).exists())
            worker.acknowledge(attempts[1]['id'],attempts[1]['token'])
            self.assertFalse((worker.root/'spool'/sha).exists())
        finally:worker.close()

    def test_slow_collection_does_not_starve_execution_or_control(self):
        c=self.expand(8)
        h={'name':'local','transport':'local','directory':str(self.root/'slow-result-worker'),
           'slots':2,'collect_slots':2,'input_slots':2,'result_slots':2}
        blocked=threading.Event();release=threading.Event()
        original=Coordinator.collect_attempt
        def slow(coordinator,transport,item):
            blocked.set()
            if not release.wait(15):raise AssertionError('test did not release collector')
            return original(coordinator,transport,item)
        errors=[]
        def runner():
            local=Coordinator(c.root)
            try:local.run([h])
            except BaseException as error:errors.append(error)
            finally:local.close()
        t=Transport(h,c.root/'worker.pyz')
        try:
            with patch.object(Coordinator,'collect_attempt',slow):
                thread=threading.Thread(target=runner);thread.start()
                self.assertTrue(blocked.wait(10))
                deadline=time.monotonic()+10
                while time.monotonic()<deadline:
                    status=t.rpc('status')
                    if status['counts'].get('done',0)>=6:break
                    time.sleep(.1)
                self.assertGreaterEqual(status['counts'].get('done',0),6,status)
                self.assertGreaterEqual(c.status()['jobs'].get('active',0),6)
                # Collectors remain blocked longer than the lease, so renewal
                # must be independent of their completion.
                time.sleep(2.2)
                self.assertEqual(c.status()['failures'],[])
                # Control must remain responsive even while both result lanes block.
                c.control('paused')
                deadline=time.monotonic()+3
                while time.monotonic()<deadline:
                    w=Worker(t.root)
                    mode=w.db.execute("SELECT mode FROM controls WHERE experiment='fixture'").fetchone()
                    w.close()
                    if mode and mode[0]=='paused':break
                    time.sleep(.05)
                self.assertEqual(mode[0],'paused')
                c.control('running');release.set();thread.join(15)
                self.assertFalse(thread.is_alive());self.assertEqual(errors,[])
                self.assertEqual(c.status()['jobs'],{'completed':8})
        finally:
            release.set();c.control('cancelled')
            if 'thread' in locals():thread.join(15)
            t.rpc('stop');t.close()

    def test_cancellation_during_input_delivery_never_enqueues(self):
        c=self.coordinator;a=c.dispatch('local',self.status)[0]
        entered=threading.Event();release=threading.Event();calls=[]
        class Channel:
            def rpc(self,op,**kwargs):
                calls.append(op)
                if op=='has_bundle':
                    entered.set();release.wait(5);return {'present':True}
                raise AssertionError('enqueue after cancellation')
        def deliver():
            local=Coordinator(c.root)
            try:return local.deliver_attempt(Channel(),a['id'])
            finally:local.close()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            future=pool.submit(deliver)
            self.assertTrue(entered.wait(5));c.control('cancelled');release.set()
            self.assertFalse(future.result(timeout=5))
        self.assertEqual(calls,['has_bundle'])

    def test_terminal_cancellation_is_sent_after_last_running_poll(self):
        c=self.coordinator
        h={'name':'local','transport':'local','directory':str(self.root/'cancel-worker')}
        original=Coordinator.poll_host
        modes=[]
        def poll(coordinator,*args,**kwargs):
            status=original(coordinator,*args,**kwargs)
            modes.append(coordinator.mode)
            if len(modes)==1:coordinator.control('cancelled')
            return status
        t=Transport(h,c.root/'worker.pyz')
        try:
            with patch.object(Coordinator,'poll_host',poll):c.run([h])
            w=Worker(t.root)
            try:
                mode=w.db.execute("SELECT mode FROM controls WHERE experiment='fixture'").fetchone()[0]
                self.assertEqual(mode,'cancelled')
                self.assertEqual(modes[0],'running')
                self.assertIn('cancelled',modes[1:])
            finally:w.close()
        finally:t.rpc('stop');t.close()

    def test_worker_rejects_input_arriving_after_durable_cancellation(self):
        a=self.coordinator.dispatch('local',self.status)[0]
        w=Worker(self.root/'cancel-enqueue-worker')
        try:
            shutil.copytree(self.root/'bundles'/self.bundle['id'],w.root/'bundles'/self.bundle['id'])
            w.control(a['experiment'],'cancelled')
            self.assertEqual(w.enqueue(a),{'enqueued':False,'reason':'cancelled'})
            self.assertEqual(w.db.execute('SELECT count(*) FROM queue').fetchone()[0],0)
        finally:w.close()

    def test_transfer_capacity_smaller_than_host_count_does_not_starve_hosts(self):
        c=self.expand(28)
        hosts=[{'name':'host'+str(i),'transport':'local','directory':str(self.root/('host'+str(i))),
                'input_slots':1} for i in range(7)]
        for h in hosts:self.assertEqual(len(c.dispatch(h['name'],self.status|{'slots':4})),4)
        seen=[];mutex=threading.Lock()
        def poll(coordinator,host,*args):
            return self.status|{'slots':4,'counts':{},'spool_bytes':0,'attempts':[]}
        def deliver(coordinator,transport,identity):
            host=coordinator.db.execute('SELECT host FROM attempts WHERE id=?',(identity,)).fetchone()[0]
            with mutex:
                seen.append(host)
                if len(seen)>=14:coordinator.control('cancelled')
            return True
        with patch.object(Coordinator,'poll_host',poll), patch.object(Coordinator,'deliver_attempt',deliver):
            c.run(hosts)
        # Seven continuously backlogged hosts share only two transfer slots.
        # New work from early hosts must not displace hosts still awaiting service.
        self.assertEqual(set(seen[:8]),{h['name'] for h in hosts},seen)

    def test_paused_delivery_is_not_marked_enqueued(self):
        c=self.coordinator;a=c.dispatch('local',self.status)[0];c.control('paused')
        self.assertFalse(c.deliver_attempt(None,a['id']))


if __name__=='__main__':unittest.main()
