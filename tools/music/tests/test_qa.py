# SPDX-License-Identifier: GPL-3.0-or-later
"""Each QA check must fail on the defect it exists to catch, and pass the clean signal.

Run from the repository root with the music venv:
    tools/music/.venv/bin/python -m unittest discover -s tools/music/tests
"""
from pathlib import Path
import sys
import tempfile
import unittest
import warnings

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from glob2music.audio import Trio, write_opus, write_trio  # noqa: E402
from glob2music.qa import run_checks  # noqa: E402
from glob2music.qa.analysis import MoodAudio, TrioAudio  # noqa: E402
import synthetic  # noqa: E402

warnings.filterwarnings('ignore', category=UserWarning)
SR = synthetic.SR
_CLEAN = {}


def clean():
    """The shared clean synthetic trio (rendered once, roughly level-matched, not
    limited: the checks under test here do not depend on mastering)."""
    if 'trio' not in _CLEAN:
        moods = {}
        for mood, gain_db in (('calm', -18), ('building', -17), ('combat', -16)):
            y = synthetic.render_mood(mood, 60.0, seed=3)
            moods[mood] = y / np.sqrt(np.mean(y ** 2)) * 10 ** ((gain_db + 3) / 20)
        _CLEAN['trio'] = Trio(**moods)
    return _CLEAN['trio']


def replace(trio, **moods):
    arrays = {m: trio[m] for m in ('calm', 'building', 'combat')}
    arrays.update(moods)
    return Trio(**arrays)


def statuses(report, check):
    return {m.name: m.status for r in report.results if r.name == check for m in r.measures}


class SeamTest(unittest.TestCase):
    def test_clean_loop_passes_step(self):
        st = statuses(run_checks(clean(), only={'seam'}), 'seam')
        self.assertNotEqual(st['seam.calm.step'], 'fail')

    def test_click_at_seam_fails(self):
        calm = clean()['calm'].copy()
        calm[-1] += 0.6                      # one-sample discontinuity right at the wrap
        st = statuses(run_checks(replace(clean(), calm=calm), only={'seam'}), 'seam')
        self.assertEqual(st['seam.calm.step'], 'fail')

    def test_gap_at_seam_fails(self):
        calm = clean()['calm'].copy()
        calm[-int(2.5 * SR):] = 0.0          # 2.5 s of silence before the wrap
        st = statuses(run_checks(replace(clean(), calm=calm), only={'seam'}), 'seam')
        self.assertEqual(st['seam.calm.gap'], 'fail')


class RepetitionTest(unittest.TestCase):
    def test_through_composed_passes(self):
        st = statuses(run_checks(clean(), only={'repetition'}), 'repetition')
        self.assertNotIn('fail', st.values())

    def test_exact_four_times_repeat_fails(self):
        phrase = synthetic.render_mood('building', 15.0, seed=5)
        looped = np.tile(phrase, (4, 1))
        trio = Trio(calm=looped, building=looped, combat=looped)
        st = statuses(run_checks(trio, only={'repetition'}), 'repetition')
        self.assertEqual(st['repetition.calm.envelope'], 'fail')
        self.assertEqual(st['repetition.calm.copy'], 'fail')


class AlignmentTest(unittest.TestCase):
    def test_shared_timeline_passes(self):
        st = statuses(run_checks(clean(), only={'alignment'}), 'alignment')
        self.assertEqual(st['alignment.building_combat.lag'], 'pass')

    def test_time_offset_copy_fails(self):
        shifted = np.roll(clean()['combat'], int(0.030 * SR), axis=0)     # 30 ms late
        st = statuses(run_checks(replace(clean(), combat=shifted), only={'alignment'}), 'alignment')
        self.assertEqual(st['alignment.building_combat.lag'], 'fail')

    def test_bar_offset_breaks_harmony(self):
        shifted = np.roll(clean()['combat'], int(2 * synthetic.BEAT * SR), axis=0)   # half a bar
        st = statuses(run_checks(replace(clean(), combat=shifted), only={'alignment'}), 'alignment')
        self.assertIn(st['alignment.building_combat.harmony'], ('warn', 'fail'))


class ContrastTest(unittest.TestCase):
    def test_distinct_moods_pass(self):
        st = statuses(run_checks(clean(), only={'contrast'}), 'contrast')
        self.assertNotIn('fail', st.values())

    def test_identical_moods_fail(self):
        y = clean()['building']
        trio = Trio(calm=y * 0.8, building=y, combat=y * 1.2)   # same music at three levels
        st = statuses(run_checks(trio, only={'contrast'}), 'contrast')
        self.assertEqual(st['contrast.calm_building.distance'], 'fail')
        self.assertEqual(st['contrast.escalation'], 'fail')


class NoiseTest(unittest.TestCase):
    def test_clean_passes(self):
        st = statuses(run_checks(clean(), only={'noise'}), 'noise')
        self.assertEqual(st['noise.calm.hf_floor'], 'pass')

    def test_broadband_hiss_fails(self):
        calm = clean()['calm']
        level = np.sqrt(np.mean(calm ** 2))
        hiss = np.random.default_rng(0).standard_normal(calm.shape) * level * 10 ** (-24 / 20)
        st = statuses(run_checks(replace(clean(), calm=calm + hiss), only={'noise'}), 'noise')
        self.assertEqual(st['noise.calm.hf_floor'], 'fail')


class FormatTest(unittest.TestCase):
    def test_written_trio_passes_and_mismatched_frames_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            short = {m: clean()[m][:52 * SR] for m in ('calm', 'building', 'combat')}
            write_trio(Trio(**short), tmp)
            st = statuses(run_checks(tmp, only={'format'}), 'format')
            self.assertTrue(all(s == 'pass' for s in st.values()), st)
            write_opus(short['combat'][:-441], Path(tmp) / 'a3.opus')       # 10 ms short
            st = statuses(run_checks(tmp, only={'format'}), 'format')
            self.assertEqual(st['format.frames'], 'fail')

    def test_chained_stream_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            short = {m: clean()[m][:52 * SR] for m in ('calm', 'building', 'combat')}
            paths = write_trio(Trio(**short), tmp)
            data = paths['calm'].read_bytes()
            paths['calm'].write_bytes(data + data)                         # two logical streams
            st = statuses(run_checks(tmp, only={'format'}), 'format')
            self.assertEqual(st['format.calm.container'], 'fail')

    def test_in_memory_rate_and_length(self):
        y = np.zeros((10 * SR, 2), dtype=np.float32)
        audio = TrioAudio({'calm': MoodAudio('calm', y, 44100), 'building': MoodAudio('building', y, SR),
                           'combat': MoodAudio('combat', y, SR)})
        audio.errors = {}
        st = statuses(run_checks(audio, only={'format'}), 'format')
        self.assertEqual(st['format.calm.rate'], 'fail')
        self.assertEqual(st['format.length'], 'fail')


class WaiverTest(unittest.TestCase):
    def test_waiver_turns_fail_into_waived(self):
        calm = clean()['calm'].copy()
        calm[-1] += 0.6
        trio = replace(clean(), calm=calm)
        report = run_checks(trio, only={'seam'}, waivers={'seam.calm.step': 'Test: deliberate click.'})
        self.assertEqual(statuses(report, 'seam')['seam.calm.step'], 'waived')
        self.assertFalse(report.failed)
        report = run_checks(trio, only={'seam'}, waivers={'seam.nothing': 'Matches no measure at all.'})
        self.assertTrue(report.failed)
        self.assertIsNotNone(report.measure('waivers.seam_nothing'))


if __name__ == '__main__':
    unittest.main()
