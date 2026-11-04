#!/usr/bin/env python3
"""Exercise storage bounds with real child processes and durable collection."""
import concurrent.futures
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.tournaments.bundles import register_bundle, package_identity, platform_identity
from tools.tournaments.common import atomic_json, read_json
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.jobs import EngineJob
from tools.tournaments.model import job, validate_job, validate_experiment
from tools.tournaments.results import Results
from tools.tournaments.storage import CompressedLog
from tools.tournaments.worker import Worker, execute, pack
import test_tournaments as fixtures

FAKE = '''#!/usr/bin/env python3
import json, pathlib, sys
if sys.argv[1:2]==['help']:
 print(json.dumps({'schema_version':1,'cli_version':2,'commands':[]}));sys.exit(0)
if sys.argv[1:3]==['info','catalog']:
 print(json.dumps({'schema_version':1,'commands':['game','generate_map'],'ais':[],'generators':[]}));sys.exit(0)
out=pathlib.Path(sys.argv[sys.argv.index('--output-dir')+1]);out.mkdir(parents=True,exist_ok=True)
scenario=SCENARIO
if scenario=='file-flood':
 with (out/'oversized.trace').open('wb') as f:f.write(b'x'*(2*1024*1024))
if scenario in ('log-flood','probability'):
 for i in range(100):
  print('unused '+('x'*50000))
  if scenario=='probability':print('GLOB2_WINPROB alliance=0 tick='+str(i*512)+' permille=500')
else:print('ordinary diagnostic output')
(out/'result.json').write_text(json.dumps({'schema_version':1,'status':'completed','job_type':'game'}))
'''


class StreamTests(unittest.TestCase):
    def test_full_capture_is_lossless_and_streamed_gzip(self):
        text=b'abc\n' * 100000
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'stdout.log.gz'
            stream=CompressedLog(io.BytesIO(text), path, len(text))
            meta=stream.finish()
            self.assertIsNone(meta['error'])
            self.assertEqual(gzip.decompress(path.read_bytes()), text)
            self.assertEqual(meta['raw_sha256'], hashlib.sha256(text).hexdigest())
            self.assertFalse((Path(folder)/'stdout.log').exists())

    def test_probability_filter_handles_long_unwanted_lines_and_split_reads(self):
        text=b'ignored '+b'x'*200000+b'\nGLOB2_WINPROB tick=512 permille=600\nignored\n'
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'stdout.log.gz'
            capture=CompressedLog(io.BytesIO(text), path, 100, 'win-probability')
            meta=capture.finish()
            self.assertEqual(gzip.decompress(path.read_bytes()), b'GLOB2_WINPROB tick=512 permille=600\n')
            self.assertEqual(meta['received_bytes'],len(text))
            self.assertIsNone(meta['error'])

    def test_tactical_mode_preserves_model_and_maxima_records(self):
        text=(b'noise\nMAXIMA_TELEMETRY\t1\t0\tflag_created\n'
              b'GLOB2_WINPROB tick=512 permille=600\nGLOB2_ECON team=0 units=10\n'
              b'GLOB2_TL team=0 hp=100\nGLOB2_PERF_WORKER time=123\n')
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'stdout.log.gz'
            capture=CompressedLog(io.BytesIO(text),path,1000,'telemetry')
            self.assertIsNone(capture.finish()['error'])
            lines=gzip.decompress(path.read_bytes()).splitlines()
            self.assertEqual(len(lines),4)
            self.assertTrue(lines[0].startswith(b'MAXIMA_TELEMETRY'))

    def test_overflow_drains_and_closes_valid_bounded_gzip(self):
        source=io.BytesIO(b'x'*1000000)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'stdout.log.gz';capture=CompressedLog(source,path,70000);meta=capture.finish()
            self.assertIn('limit',meta['error'])
            self.assertLessEqual(len(gzip.decompress(path.read_bytes())),70000)
            self.assertEqual(meta['received_bytes'],1000000)
            self.assertTrue(meta['complete'])


class SchemaTests(unittest.TestCase):
    def request(self,**extra):
        return job('game','a'*64,config={'generator':15,'players':['numbi','castor']},seeds={'map':1,'game':2},**extra)

    def test_oversized_checksum_recording_needs_explicit_allowance(self):
        with self.assertRaisesRegex(ValueError,'explicit artifact_bytes'):
            validate_job(self.request(outputs={'telemetry':['checksums']}))
        validate_job(self.request(outputs={'telemetry':['checksums']}, limits={'artifact_bytes':1000000}))

    def test_short_determinism_probes_still_work(self):
        request=self.request(outputs={'telemetry':['checksums']});request['config']['ticks']=2048
        validate_job(request)

    def test_probability_mode_requires_actual_model_records(self):
        with self.assertRaisesRegex(ValueError,'team-timeline'):
            validate_job(self.request(outputs={'log_mode':'win-probability'}))
        validate_job(self.request(outputs={'log_mode':'win-probability','telemetry':['team-timeline']}))

    def test_byte_budgets_reject_bool_negative_and_noninteger(self):
        for key in ['artifact_bytes','log_bytes']:
            for value in [True,0,-1,1.5]:
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):
                    validate_job(self.request(limits={key:value}))


class QuotaTests(unittest.TestCase):
    setUp=fixtures.Fixture.setUp
    tearDown=fixtures.Fixture.tearDown
    result=fixtures.Fixture.result

    def test_coordinator_stops_dispatch_when_artifact_budget_is_full(self):
        self.coordinator.settings['artifact_budget_bytes']=1
        (self.coordinator.root/'artifacts'/'existing').write_bytes(b'already full')
        self.assertEqual(self.coordinator.dispatch('host',self.status),[])
        self.assertFalse(self.coordinator.status()['storage']['accepting'])

    def test_parallel_collectors_cannot_reserve_the_same_free_space(self):
        self.coordinator.settings['artifact_budget_bytes']=20000
        artifacts=[{'sha256':str(n)*64,'bytes':12000} for n in [1,2]]
        with patch('tools.tournaments.coordinator.receive_file',return_value=False) as receive:
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                self.assertEqual(list(pool.map(lambda a:self.coordinator.receive_artifact(None,a),artifacts)),[False,False])
            self.assertEqual(receive.call_count,1)
        self.assertEqual(self.coordinator.storage_status()['reserved_bytes'],12000)
        self.assertLess(self.coordinator.storage_status()['used_bytes'],20000)

    def test_disk_reserve_blocks_transfer_before_any_remote_bytes(self):
        with patch('tools.tournaments.coordinator.shutil.disk_usage',return_value=Mock(free=1)),patch('tools.tournaments.coordinator.receive_file') as receive:
            self.assertFalse(self.coordinator.receive_artifact(None,{'sha256':'a'*64,'bytes':10}))
            receive.assert_not_called()

    def test_resumed_transfer_stops_when_external_disk_pressure_appears(self):
        artifact={'sha256':'a'*64,'bytes':12000}
        with patch('tools.tournaments.coordinator.receive_file',return_value=False) as receive:
            self.assertFalse(self.coordinator.receive_artifact(None,artifact))
            self.assertEqual(receive.call_count,1)
            with patch('tools.tournaments.coordinator.shutil.disk_usage',return_value=Mock(free=1)):
                self.assertFalse(self.coordinator.receive_artifact(None,artifact))
            self.assertEqual(receive.call_count,1)

    def test_existing_verified_artifact_can_be_reused_at_quota(self):
        content=b'already retained';identity=hashlib.sha256(content).hexdigest()
        (self.coordinator.root/'artifacts'/identity).write_bytes(content)
        self.coordinator.settings['artifact_budget_bytes']=1
        self.assertTrue(self.coordinator.receive_artifact(None,{'sha256':identity,'bytes':len(content)}))

    def test_storage_limit_is_recorded_once_without_automatic_retry(self):
        attempt=self.coordinator.dispatch('host',self.status)[0]
        self.assertFalse(self.coordinator.accept(self.result(attempt,'storage_limit')))
        self.assertEqual(self.coordinator.status()['jobs'],{'failed':1})
        self.assertEqual(self.coordinator.dispatch('host',self.status),[])


class SupervisorTests(unittest.TestCase):
    def scenario(self, scenario, *, log_limit=65536, artifact_limit=1024*1024, probability=False, inherited_file_limit=None):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);root=Path(temp.name);source=root/'source';source.mkdir()
        script=FAKE.replace('SCENARIO',repr(scenario));(source/'glob2').write_text(script);(source/'glob2').chmod(0o755)
        bundle=register_bundle(source,root/'bundles','glob2','storage-test')
        outputs={'required':['stdout.log']} if not probability else {'telemetry':['team-timeline'],'log_mode':'win-probability'}
        request=job('game',bundle['id'],config={'generator':15,'players':['numbi','castor']},seeds={'map':1,'game':2},outputs=outputs)
        coordinator=Coordinator.submit(root/'results',{'schema_version':1,'id':'storage','jobs':[request]},[root/'bundles'/bundle['id']]);self.addCleanup(coordinator.close)
        worker=Worker(root/'worker');self.addCleanup(worker.close);worker.configure({'slots':1,'attempt_budget_bytes':artifact_limit,'log_budget_bytes':log_limit})
        shutil.copytree(root/'bundles'/bundle['id'],worker.root/'bundles'/bundle['id'])
        attempt=coordinator.dispatch('host',worker.status())[0];worker.enqueue(attempt)
        import contextlib, resource
        original_getrlimit=resource.getrlimit
        def inherited_limit(kind):
            return (inherited_file_limit,inherited_file_limit) if kind==resource.RLIMIT_FSIZE else original_getrlimit(kind)
        with patch('resource.getrlimit',side_effect=inherited_limit) if inherited_file_limit is not None else contextlib.nullcontext():
            execute(worker.root,attempt['id'])
        directory=worker.root/'attempts'/attempt['id'];execution=read_json(directory/'execution.json')
        return coordinator,worker,attempt,directory,execution

    def test_noisy_child_cannot_fill_disk_or_be_counted_successful(self):
        _,_,_,directory,execution=self.scenario('log-flood',log_limit=8192)
        self.assertEqual(execution['category'],'storage_limit')
        self.assertFalse((directory/'stdout.log').exists())
        self.assertLessEqual(len(gzip.decompress((directory/'stdout.log.gz').read_bytes())),8192)

    def test_child_written_detailed_traces_have_a_hard_file_limit(self):
        _,_,_,directory,execution=self.scenario('file-flood',artifact_limit=32768)
        self.assertEqual(execution['category'],'storage_limit')
        self.assertLessEqual((directory/'output/oversized.trace').stat().st_size,32768)

    def test_tighter_inherited_file_limit_is_preserved_and_reported(self):
        _,_,_,directory,execution=self.scenario('file-flood',inherited_file_limit=32768)
        self.assertEqual(execution['category'],'storage_limit')
        self.assertEqual(execution['storage_limits']['file_bytes'],32768)
        self.assertLessEqual((directory/'output/oversized.trace').stat().st_size,32768)

    def test_probability_capture_remains_small_and_packs_as_readable_stdout(self):
        c,w,a,d,e=self.scenario('probability',log_limit=8192,probability=True)
        self.assertEqual(e['category'],'success',e)
        self.assertGreater(e['streamed_logs']['stdout.log.gz']['received_bytes'],5_000_000)
        self.assertLess(e['streamed_logs']['stdout.log.gz']['raw_bytes'],8192)
        pack(w.root,a['id']);record=read_json(d/'record.json')
        for artifact in record['artifacts']:
            shutil.copyfile(w.root/'spool'/artifact['sha256'],c.root/'artifacts'/artifact['sha256'])
        self.assertTrue(c.accept(record))
        self.assertFalse((d/'stdout.log.gz').exists())
        with Results(c.root).open_artifact(record,'stdout.log') as f:lines=f.readlines()
        self.assertEqual(len(lines),100)
        self.assertTrue(all(line.startswith('GLOB2_WINPROB ') for line in lines))
        w.db.execute("UPDATE queue SET state='done' WHERE id=?",(a['id'],))
        w.acknowledge(a['id'],a['token'])
        self.assertFalse(d.exists())
        self.assertFalse(list((w.root/'spool').iterdir()))


if __name__=='__main__':unittest.main()
