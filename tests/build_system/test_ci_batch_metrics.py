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
