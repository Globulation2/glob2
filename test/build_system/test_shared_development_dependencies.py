import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scons'))
import dev_store
import shared_dependencies

def build(prefix, work, **kwargs):
    prefix.mkdir(parents=True)
    (prefix / 'library-manifest.json').write_text('{}')
    (prefix / 'library.a').write_bytes(b'library')

class SharedDevelopmentDependenciesTests(unittest.TestCase):
    def tearDown(self):
        dev_store.finish()

    def test_reuse_and_corruption_detection(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
            first = shared_dependencies.ensure(build, Path(task) / 'one', Path(task) / 'work')
            second = shared_dependencies.ensure(build, Path(task) / 'two', Path(task) / 'work')
            self.assertEqual(first, second)
            (first / 'library.a').write_bytes(b'corrupt')
            with self.assertRaises(ValueError):
                shared_dependencies.ensure(build, Path(task) / 'two', Path(task) / 'work')
            dev_store.finish()
            repaired = shared_dependencies.ensure(build, Path(task) / 'two', Path(task) / 'work')
            self.assertEqual((repaired / 'library.a').read_bytes(), b'library')

    def test_active_consumers_share_reader_leases(self):
        with tempfile.TemporaryDirectory() as task, patch.dict(os.environ, {'GLOB2_DEV_HOME': task, 'GLOB2_DEV_MODE': 'shared'}), patch.object(shared_dependencies, 'fingerprint', return_value={'version': 1}):
            first = shared_dependencies.ensure(build, Path(task) / 'one', Path(task) / 'work')
            code = """import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import shared_dependencies as shared
shared.fingerprint = lambda builder, args: {'version': 1}
def forbidden(*args, **kwargs):
    raise RuntimeError('Compatible readers must not rebuild')
print(shared.ensure(forbidden, Path(sys.argv[2])/'two', Path(sys.argv[2])/'work'))
"""
            result = subprocess.run([sys.executable, '-c', code, str(ROOT / 'scons'), task], capture_output=True, text=True, timeout=5, check=True)
            self.assertEqual(result.stdout.strip(), str(first))
