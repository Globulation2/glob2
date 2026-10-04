import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'.github/scripts'))
import ci_batch_metrics as batch

class BatchTest(unittest.TestCase):
    def test_previous_state_is_inert_json(self):
        archive=io.BytesIO()
        state=dict(schema=1,measured=['1:1'],runs=[])
        with zipfile.ZipFile(archive,'w') as output: output.writestr('state.json',json.dumps(state))
        with patch.object(batch,'api',side_effect=[{'workflow_runs':[{'id':2}]},{'artifacts':[dict(id=3,name='ci-metrics-batch',expired=False)]},archive.getvalue()]):
            self.assertEqual(batch.previous_batch('o/r','token'),state)

    def test_cold_batch_bounds_api_work_and_retries_unavailable_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            old=Path.cwd()
            try:
                os.chdir(directory)
                runs=[dict(id=n,conclusion='success') for n in range(100)]
                with patch.dict(os.environ,{'GITHUB_REPOSITORY':'o/r','GH_TOKEN':'token'},clear=True),patch.object(batch,'api',return_value={'workflow_runs':runs}),patch.object(batch,'previous_batch',return_value=dict(schema=1,measured=[],runs=[])),patch.object(batch.subprocess,'run',return_value=type('Result',(),{'returncode':1})()) as measure:
                    batch.main()
                self.assertEqual(measure.call_count,10)
                self.assertEqual(json.loads(Path('artifacts/ci-metrics/state.json').read_text())['measured'],[])
            finally: os.chdir(old)

    def test_batch_skips_measured_attempts_and_cancelled_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            old=Path.cwd()
            try:
                os.chdir(directory)
                runs=[dict(id=1,run_attempt=1,conclusion='success'),dict(id=2,conclusion='cancelled')]
                with patch.dict(os.environ,{'GITHUB_REPOSITORY':'o/r','GH_TOKEN':'token'},clear=True),patch.object(batch,'api',return_value={'workflow_runs':runs}),patch.object(batch,'previous_batch',return_value=dict(schema=1,measured=['1:1'],runs=[])),patch.object(batch.subprocess,'run') as measure:
                    batch.main()
                measure.assert_not_called()
                self.assertEqual(json.loads(Path('artifacts/ci-metrics/state.json').read_text())['measured'],['1:1'])
            finally: os.chdir(old)

    def test_batch_retains_requested_draft_tests_but_excludes_cheap_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            old = Path.cwd()
            try:
                os.chdir(directory)
                runs = [dict(id=n,conclusion='success') for n in range(1,5)]
                rows = {
                    1: dict(draft=False,verification_mode='cheap-contracts',selection={'native':False}),
                    2: dict(draft=False,verification_mode='nightly-reused',selection={'native':False}),
                    3: dict(draft=True,verification_mode='affected',selection={'platform':True}),
                    4: dict(draft=True,verification_mode=None,selection={'native':True}),
                }
                def measure(command, check):
                    run_id = int(command[command.index('--run-id')+1])
                    path = Path(command[command.index('--output')+1])
                    path.parent.mkdir(parents=True,exist_ok=True)
                    path.write_text(json.dumps(dict(rows[run_id],run_id=run_id)))
                    return type('Result',(),{'returncode':0})()
                with patch.dict(os.environ,{'GITHUB_REPOSITORY':'o/r','GH_TOKEN':'token'},clear=True), \
                     patch.object(batch,'api',return_value={'workflow_runs':runs}), \
                     patch.object(batch,'previous_batch',return_value=dict(schema=1,measured=[],runs=[])), \
                     patch.object(batch.subprocess,'run',side_effect=measure):
                    batch.main()
                state = json.loads(Path('artifacts/ci-metrics/state.json').read_text())
                self.assertEqual({row['run_id'] for row in state['runs']}, {3})
                self.assertEqual(len(state['measured']),4)
            finally:
                os.chdir(old)
