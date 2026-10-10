"""The served game must include every worker and encoder its runtimes reference."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'deploy/install-web-client.py'
spec = importlib.util.spec_from_file_location('install_web_client', SCRIPT)
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallWebRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.release = Path(self.temporary.name) / 'release'
        self.served = Path(self.temporary.name) / 'served'
        self.served.mkdir()
        (self.served / 'index.html').write_text('previous launcher')
        for name in (*installer.ENTRY_FILES, 'index.html', 'studio.html',
                     'assets/core.0123456789abcdef.data'):
            self.write(name, name)

    def write(self, name, text):
        path = self.release / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def run_installer(self):
        return subprocess.run([sys.executable, str(SCRIPT), str(self.release), str(self.served)],
                              capture_output=True, text=True)

    def test_installs_workers_encoders_notices_and_compressed_copies(self):
        support = [name for group in installer.WORKER_RUNTIMES for name in group]
        support += ['licenses/recording/FFmpeg.txt', 'licenses/opus/opus.txt']
        for name in support:
            self.write(name, 'new ' + name)
            self.write(name + '.br', 'compressed ' + name)
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in support:
            self.assertEqual((self.served / name).read_text(), 'new ' + name)
            self.assertEqual((self.served / (name + '.br')).read_text(), 'compressed ' + name)
        self.assertEqual((self.served / 'index.html').read_text(), 'index.html')

    def test_referenced_missing_worker_leaves_previous_release_untouched(self):
        for group in installer.WORKER_RUNTIMES:
            with self.subTest(worker=group[0]):
                self.write('threaded/index.js', 'new Worker("' + group[0] + '")')
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(group[0], result.stderr)
                self.assertEqual(sorted(p.name for p in self.served.iterdir()), ['index.html'])
                self.assertEqual((self.served / 'index.html').read_text(), 'previous launcher')

    def test_partial_encoder_runtime_leaves_previous_release_untouched(self):
        self.write('recording-worker.js', 'worker')
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('recording-storage.js', result.stderr)
        self.assertEqual(sorted(p.name for p in self.served.iterdir()), ['index.html'])

    def test_removes_stale_worker_compression_on_upgrade(self):
        for name in installer.WORKER_RUNTIMES[0]:
            self.write(name, 'current')
            (self.served / (name + '.br')).write_text('previous compressed runtime')
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in installer.WORKER_RUNTIMES[0]:
            self.assertEqual((self.served / name).read_text(), 'current')
            self.assertFalse((self.served / (name + '.br')).exists())


if __name__ == '__main__':
    unittest.main()
