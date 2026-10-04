# SPDX-License-Identifier: GPL-3.0-or-later
"""The NumPy instrument voices and DspBackend: determinism, pitch, decay, band limits
and stem layout."""
from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.backends import dsp  # noqa: E402
from glob2music.score import Instrument, PerformedPart  # noqa: E402
from glob2music.spec import SAMPLE_RATE as SR  # noqa: E402


def hf_share_db(y, hz):
    s = np.abs(np.fft.rfft(y)) ** 2
    f = np.fft.rfftfreq(len(y), 1 / SR)
    return 10 * np.log10(s[f > hz].sum() / s.sum())


class VoiceTest(unittest.TestCase):
    def test_every_voice_is_finite_audible_decaying_and_seeded(self):
        for name, voice in dsp.VOICES.items():
            a = voice(60, 100, np.random.default_rng(1))
            b = voice(60, 100, np.random.default_rng(1))
            self.assertTrue(np.all(np.isfinite(a)), name)
            np.testing.assert_array_equal(a, b, err_msg=name)
            n = len(a) // 10
            head, tail = np.sqrt(np.mean(a[:n] ** 2)), np.sqrt(np.mean(a[-n:] ** 2))
            self.assertGreater(head, 1e-3, name)
            self.assertLess(tail, head * 0.5, name)

    def test_velocity_makes_hits_louder(self):
        for name, voice in dsp.VOICES.items():
            soft = np.abs(voice(60, 30, np.random.default_rng(2))).max()
            loud = np.abs(voice(60, 120, np.random.default_rng(2))).max()
            self.assertGreater(loud, soft * 1.5, name)

    def test_kalimba_sounds_at_its_pitch(self):
        y = dsp.kalimba(69, 90, np.random.default_rng(0))[:SR]
        s = np.abs(np.fft.rfft(y * np.hanning(len(y))))
        self.assertAlmostEqual(np.fft.rfftfreq(len(y), 1 / SR)[np.argmax(s)], 440.0, delta=2.0)

    def test_noise_percussion_is_band_limited(self):
        rng = np.random.default_rng(3)
        for voice in (dsp.snap, dsp.shaker):
            self.assertLess(hf_share_db(voice(60, 110, rng), 10000), -20, voice.__name__)


class BackendTest(unittest.TestCase):
    def test_kit_maps_notes_to_drums(self):
        backend = dsp.DspBackend({'kit': (Instrument('kit', 'perc'), 'kit')})
        for note, (drum, pitch, pan) in dsp.KIT.items():
            y = backend.render({'m': [PerformedPart('perc', 'kit', 'perc', [(0.0, 0.1, note, 100)])]}, 1.0)['m']['perc']
            self.assertGreater(np.abs(y).max(), 1e-3, drum)
            if abs(pan) > 0.1:                               # panned drums lean to their side
                left, right = np.abs(y[:, 0]).sum(), np.abs(y[:, 1]).sum()
                self.assertEqual(left > right, pan < 0, drum)

    def test_stems_have_preroll_and_tail_and_place_notes(self):
        backend = dsp.DspBackend({'k': (Instrument('k', 'ring'), 'kalimba')})
        on_time = PerformedPart('k1', 'k', 'harp', [(1.0, 1.5, 69, 100)])
        early = PerformedPart('k2', 'k', 'harp', [(-0.2, 0.3, 72, 80)])   # humanised before beat 0
        stems = backend.render({'calm': [on_time, early]}, 4.0)['calm']
        y = stems['k1']
        self.assertEqual(y.shape, (int(round((dsp.PREROLL_S + 4.0 + dsp.TAIL_S) * SR)), 2))
        onset = np.argmax(np.abs(y[:, 0]) > 1e-4)
        self.assertAlmostEqual(onset / SR, dsp.PREROLL_S + 1.0, delta=0.005)
        onset = np.argmax(np.abs(stems['k2'][:, 0]) > 1e-4)
        self.assertAlmostEqual(onset / SR, dsp.PREROLL_S - 0.2, delta=0.005)
        np.testing.assert_array_equal(y, backend.render({'calm': [on_time]}, 4.0)['calm']['k1'])


if __name__ == '__main__':
    unittest.main()
