"""Packaging stages the notices from the resolved dependency, without an SDK."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('scons'), 'SCons command unavailable')
class MobileRecordingNoticesTests(unittest.TestCase):
    def setUp(self):
        self.task = tempfile.TemporaryDirectory()
        self.addCleanup(self.task.cleanup)
        self.root = Path(self.task.name)
        self.prefix = self.root / 'managed-dependencies/recording/prefix'
        self.notices = self.prefix / 'share/licenses/recording'
        self.notices.mkdir(parents=True)
        (self.notices / 'ffmpeg-COPYING.GPLv2').write_text('Pinned FFmpeg notice\n')
        (self.notices / 'configuration.json').write_text('{"video_codec":"h264"}\n')
        (self.root / 'SConstruct').write_text(
            'import sys\n'
            f'sys.path.insert(0, {str(ROOT / "scons")!r})\n'
            'from mobile_artifacts import stage_recording_notices\n'
            'env = Environment(tools=[])\n'
            'def native(target, source, env):\n'
            '    open(str(target[0]), "w").write("unchanged native artifact\\n")\n'
            'program = env.Command("libmain.so", [], native)\n'
            'Default(program, stage_recording_notices(env, ARGUMENTS["prefix"], Dir("#").abspath))\n')

    def build(self, success=True):
        result = subprocess.run([shutil.which('scons') or 'scons', '-Q',
                                 'prefix=' + str(self.prefix)], cwd=self.root,
                                capture_output=True, text=True, timeout=60)
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
        return result

    def assert_exact_notices(self):
        staged = self.root / 'licenses/recording'
        self.assertEqual({p.name: p.read_bytes() for p in staged.iterdir()},
                         {p.name: p.read_bytes() for p in self.notices.iterdir()})
        self.assertFalse((self.root / 'recording/prefix').exists())

    def test_managed_prefix_stages_without_a_nominal_local_install(self):
        self.build()
        self.assert_exact_notices()

    def test_missing_staged_notice_is_restored(self):
        self.build()
        (self.root / 'licenses/recording/ffmpeg-COPYING.GPLv2').unlink()
        self.build()
        self.assert_exact_notices()

    def test_changed_staged_notice_is_repaired_from_resolved_prefix(self):
        self.build()
        (self.root / 'licenses/recording/ffmpeg-COPYING.GPLv2').write_text('damaged staging\n')
        self.build()
        self.assert_exact_notices()

    def test_dependency_notice_change_refreshes_staging(self):
        self.build()
        (self.notices / 'configuration.json').write_text('{"video_codec":"h264","updated":true}\n')
        self.build()
        self.assert_exact_notices()

    def test_notice_refresh_does_not_rebuild_native_library(self):
        self.build()
        native = self.root / 'libmain.so'
        timestamp = native.stat().st_mtime_ns
        (self.notices / 'configuration.json').write_text('{"video_codec":"h264","updated":true}\n')
        self.build()
        self.assert_exact_notices()
        self.assertEqual(native.stat().st_mtime_ns, timestamp)

    def test_absent_dependency_notices_fail_instead_of_packaging_empty_directory(self):
        shutil.rmtree(self.notices)
        result = self.build(success=False)
        self.assertIn('contains no complete recording notices', result.stdout + result.stderr)
        self.assertFalse((self.root / 'licenses/recording').exists())


if __name__ == '__main__':
    unittest.main()
