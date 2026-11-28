"""tools/release/release_check.py and make_release_check_save.py (docs/releases/release-check.md)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'release'))
import make_release_check_save  # noqa: E402
import release_check  # noqa: E402


class VerdictTests(unittest.TestCase):
    def test_passed_with_exit_zero(self):
        self.assertIsNone(release_check.verdict('x\nGlob2 release check passed\n', 0))

    def test_reported_failure_wins(self):
        output = 'Glob2 release check: menu ok\n12:00 Glob2 release check failed: load: timed out\n'
        self.assertEqual(release_check.verdict(output, 1), 'Glob2 release check failed: load: timed out')

    def test_crash_after_passing(self):
        self.assertIn('-11 after passing', release_check.verdict('Glob2 release check passed', -11))

    def test_exit_before_passing(self):
        self.assertIn('0 before passing', release_check.verdict('Glob2 release check: menu ok', 0))


class EnvironmentTests(unittest.TestCase):
    def test_scratch_profile_and_no_display(self):
        with tempfile.TemporaryDirectory() as profile:
            env = release_check.environment(profile, 'menu,victory', {'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'w', 'HOME': '/h'})
            self.assertEqual(env['GLOB2_RELEASE_CHECK'], 'menu,victory')
            self.assertEqual(env['GLOB2_USER_DATA_DIR'], profile)
            self.assertEqual(env['SDL_AUDIODRIVER'], 'dummy')
            self.assertNotIn('DISPLAY', env)
            self.assertNotIn('WAYLAND_DISPLAY', env)
            self.assertEqual(env['HOME'], '/h')
            if sys.platform.startswith('linux'):
                self.assertEqual(env['SDL_VIDEODRIVER'], 'offscreen')
                self.assertTrue(Path(env['XDG_RUNTIME_DIR']).is_dir())

    def test_keeps_log_and_crashes(self):
        with tempfile.TemporaryDirectory() as work:
            profile, out = Path(work) / 'p', Path(work) / 'out'
            (profile / 'reports/crashes').mkdir(parents=True)
            (profile / 'reports/crashes/a.json').write_text('{}')
            kept = release_check.keep(profile, out, 'the log')
            self.assertEqual([crash.name for crash in kept], ['a.json'])
            self.assertEqual((out / 'release-check.log').read_text(), 'the log')
            self.assertTrue((out / 'a.json').exists())


class SaveTests(unittest.TestCase):
    def test_shipped_save_and_record(self):
        record = json.loads(make_release_check_save.RECORD.read_text())
        self.assertTrue(make_release_check_save.SAVE.is_file())
        self.assertLess(record['tick'], record['ends'])

    def test_header_constant_names_the_save(self):
        header = (ROOT / 'src/app/ReleaseCheck.h').read_text()
        relative = make_release_check_save.SAVE.relative_to(ROOT / 'content').as_posix()
        self.assertIn(f'"{relative}"', header)

    def test_make_picks_checkpoint_before_win(self):
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            match = work / 'match'
            match.mkdir()
            for tick in (250, 500, 750, 1000):
                (match / f'checkpoint-{tick}.game.gz').write_text(str(tick))
            save, record = work / 'out/victory.game.gz', work / 'out/victory.json'
            result = {'status': 'completed', 'winning_teams': [0], 'ticks': 1100}
            with mock.patch.object(make_release_check_save, 'run', return_value=result), \
                    mock.patch.object(make_release_check_save, 'SAVE', save), \
                    mock.patch.object(make_release_check_save, 'RECORD', record):
                make_release_check_save.make(Path('glob2'), 200, work)
            self.assertEqual(save.read_text(), '750')
            self.assertEqual(json.loads(record.read_text()), {'tick': 750, 'ends': 1100})

    def test_verify_rejects_another_winner(self):
        result = {'status': 'completed', 'winning_teams': [1], 'ticks': 900, 'termination': 'win'}
        with tempfile.TemporaryDirectory() as work, \
                mock.patch.object(make_release_check_save, 'run', return_value=result):
            with self.assertRaises(SystemExit):
                make_release_check_save.verify(Path('glob2'), 2000, Path(work))


class WorkflowTests(unittest.TestCase):
    def test_every_desktop_release_job_runs_it(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        for name in ('release-check-linux', 'release-check-windows', 'release-check-macos-'):
            self.assertIn(name, workflow)
        self.assertIn('--release-check', workflow)


if __name__ == '__main__':
    unittest.main()
