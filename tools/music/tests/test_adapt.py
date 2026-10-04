# SPDX-License-Identifier: GPL-3.0-or-later
"""glob2music.adapt on synthetic signals: stems, beat grid, room and percussion.

No network, no GPU and no Demucs weights (separation is covered by
``test_adapt_demucs.py`` with Demucs's untrained test model).
"""
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.adapt import beatgrid, percussion, room, stems  # noqa: E402
from glob2music.spec import SAMPLE_RATE as SR  # noqa: E402


def tone(seconds, hz=220.0, amp=0.2):
    t = np.arange(int(seconds * SR)) / SR
    return np.stack([amp * np.sin(2 * np.pi * hz * t)] * 2, axis=1)


def click_loop(beats, bpm, offset_s=0.05):
    """A loop of ``beats`` short decaying 1 kHz clicks at ``bpm``, first one at ``offset_s``."""
    period = 60.0 / bpm
    y = np.zeros((int(round(beats * period * SR)), 2))
    t = np.arange(int(0.05 * SR)) / SR
    click = np.sin(2 * np.pi * 1000 * t) * np.exp(-t * 80)
    for k in range(beats):
        a = int(round((offset_s + k * period) * SR))
        n = min(len(click), len(y) - a)
        y[a:a + n] += click[:n, None]
    return y


class DecodeTest(unittest.TestCase):
    def test_decode_round_trips_a_wav(self):
        y = tone(0.5)
        with tempfile.TemporaryDirectory() as d:
            import soundfile as sf
            path = Path(d) / 'x.wav'
            sf.write(path, y.astype(np.float32), SR, subtype='FLOAT')
            out = stems.decode(path)
        self.assertEqual(out.shape, y.shape)
        self.assertLess(np.abs(out - y).max(), 1e-6)

    def test_join_split_archive(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            whole = d / 'pack.zip'
            with zipfile.ZipFile(whole, 'w') as z:
                z.writestr('stems/a.txt', 'hello' * 1000)
            data = whole.read_bytes()
            parts = []
            for i, chunk in enumerate((data[:300], data[300:700], data[700:]), 1):
                parts.append(d / f'pack.zip.00{i}')
                parts[-1].write_bytes(chunk)
            joined = stems.join_split_archive(parts, d / 'joined.zip')
            with zipfile.ZipFile(joined) as z:
                self.assertEqual(z.read('stems/a.txt'), b'hello' * 1000)


class MixTest(unittest.TestCase):
    def setUp(self):
        rng = np.random.default_rng(0)
        self.parts = {'drums': rng.standard_normal((SR, 2)) * 0.05, 'bass': tone(1.0, 55.0), 'other': tone(1.0, 440.0)}
        self.residual = rng.standard_normal((SR, 2)) * 0.001
        self.mix = sum(self.parts.values()) + self.residual

    def test_remix_without_changes_is_the_mix(self):
        np.testing.assert_allclose(stems.remix(self.mix, self.parts, {}), self.mix)
        np.testing.assert_allclose(stems.remix(self.mix, self.parts, {'drums': 0.0}), self.mix)

    def test_remix_removes_a_stem_but_keeps_the_residual(self):
        out = stems.remix(self.mix, self.parts, {'drums': None})
        np.testing.assert_allclose(out, self.parts['bass'] + self.parts['other'] + self.residual, atol=1e-12)

    def test_remix_gain(self):
        out = stems.remix(self.mix, self.parts, {'bass': 6.0})
        np.testing.assert_allclose(out - self.mix, (10 ** 0.3 - 1) * self.parts['bass'], atol=1e-12)

    def test_sum_stems_mutes_unlisted_and_none(self):
        out = stems.sum_stems(self.parts, {'bass': 0.0, 'drums': None})
        np.testing.assert_allclose(out, self.parts['bass'])
        with self.assertRaises(ValueError):
            stems.sum_stems(self.parts, {'bass': None})

    def test_level_under(self):
        layer = stems.level_under(self.parts['drums'], self.mix, 10.0)
        self.assertAlmostEqual(stems.rms_db(self.mix) - stems.rms_db(layer), 10.0, places=6)


class LoopCutTest(unittest.TestCase):
    def test_cut_of_a_periodic_signal_is_seamless(self):
        # 441 Hz has exactly 100 samples per cycle; a 300-cycle loop must wrap without a step.
        y = tone(4.0, 441.0)
        start, end = SR, SR + 100 * 300
        loop = stems.cut_loop(y, start, end, crossfade_s=0.01, pre_s=0.005)
        self.assertEqual(len(loop), end - start)
        wrap = np.abs(loop[0] - loop[-1]).max()
        inner = np.abs(np.diff(loop, axis=0)).max()
        self.assertLessEqual(wrap, inner + 1e-9)

    def test_cut_needs_continuation(self):
        with self.assertRaises(ValueError):
            stems.cut_loop(tone(1.0), 1000, SR - 10, crossfade_s=0.01, pre_s=0.0)


class ColourTest(unittest.TestCase):
    def test_tilt_shelf_lifts_highs_only(self):
        lo, hi = tone(1.0, 100.0), tone(1.0, 10000.0)
        self.assertAlmostEqual(stems.rms_db(stems.tilt_shelf(lo, 4000, 6.0)) - stems.rms_db(lo), 0.0, delta=0.2)
        self.assertGreater(stems.rms_db(stems.tilt_shelf(hi, 4000, 6.0)) - stems.rms_db(hi), 4.0)

    def test_widen_keeps_mid_and_mono(self):
        mono = tone(1.0, 1000.0)
        np.testing.assert_allclose(stems.widen(mono, 1.5), mono, atol=1e-12)
        rng = np.random.default_rng(1)
        y = rng.standard_normal((SR, 2)) * 0.1
        out = stems.widen(y, 1.5)
        np.testing.assert_allclose(out.mean(axis=1), y.mean(axis=1), atol=1e-12)
        self.assertGreater(np.std(out[:, 0] - out[:, 1]), np.std(y[:, 0] - y[:, 1]))

    def test_level_ride_lifts_a_quiet_section(self):
        y = tone(16.0, 330.0)
        y[:4 * SR] *= 0.05                               # a 26 dB quieter opening
        out = stems.level_ride(y)
        before = stems.rms_db(y[SR:3 * SR]) - stems.rms_db(y[8 * SR:12 * SR])
        after = stems.rms_db(out[SR:3 * SR]) - stems.rms_db(out[8 * SR:12 * SR])
        self.assertGreater(after - before, 8.0)


class BeatGridTest(unittest.TestCase):
    def test_recovers_beat_count_and_phase(self):
        y = click_loop(32, 100.0, offset_s=0.1)
        grid = beatgrid.fit_loop_grid(y, 24, 40, latency_s=0.0)
        self.assertEqual(grid.beats, 32)
        self.assertAlmostEqual(grid.bpm, 100.0, delta=0.01)
        # phase within one onset-envelope frame (~12 ms) of the clicks
        self.assertLess(abs(grid.phase_s - 0.1), 0.015)
        self.assertEqual(grid.bars, 8)
        self.assertEqual(grid.beat(1) - grid.beat(0), int(round(0.6 * SR)))


class RoomTest(unittest.TestCase):
    def test_preset_is_deterministic_and_unit_energy(self):
        a, b = room.impulse_response(7, decay_s=1.0), room.impulse_response(7, decay_s=1.0)
        np.testing.assert_array_equal(a, b)
        np.testing.assert_allclose(np.sum(a ** 2, axis=0), 1.0)
        self.assertFalse(np.allclose(a, room.impulse_response(8, decay_s=1.0)))

    def test_reverb_tail_wraps_to_the_start(self):
        y = np.zeros((2 * SR, 2))
        y[-10] = 1.0                                     # an impulse just before the seam
        wet = room.add_room(y, 1.0, seed=1, decay_s=0.5, predelay_s=0.0)
        self.assertGreater(np.abs(wet[:int(0.2 * SR)]).max(), 1e-3)


class PercussionTest(unittest.TestCase):
    def test_voices_are_finite_and_bounded(self):
        rng = np.random.default_rng(3)
        for hit in (percussion.taiko(rng), percussion.timpani(rng), percussion.frame_drum(rng, open_stroke=True),
                    percussion.frame_drum(rng, open_stroke=False), percussion.shaker(rng)):
            self.assertEqual(hit.shape[1], 2)
            self.assertTrue(np.all(np.isfinite(hit)))
            self.assertLess(np.abs(hit).max(), 1.5)
            self.assertLess(np.abs(hit[-1]).max(), 1e-6)    # faded out: no step at the end

    def test_track_wraps_hits(self):
        track = percussion.Track(1000)
        hit = np.ones((300, 2))
        track.add(hit, 900)
        self.assertEqual(track.audio[900:].sum(), 200.0)
        self.assertEqual(track.audio[:200].sum(), 400.0)
        self.assertEqual(track.audio[200:900].sum(), 0.0)

    def test_humaniser_is_seeded(self):
        a, b = percussion.Humaniser(np.random.default_rng(5)), percussion.Humaniser(np.random.default_rng(5))
        self.assertEqual([a.time(1000) for _ in range(5)], [b.time(1000) for _ in range(5)])
        v = a.velocity(1.0)
        self.assertTrue(0.2 <= v <= 1.4)

    def test_pan_is_constant_power(self):
        mono = np.ones(SR)
        for p in (-1.0, -0.3, 0.0, 0.7, 1.0):
            st = percussion.pan(mono, p)[:SR // 2]               # before the end fade
            np.testing.assert_allclose(st[:, 0] ** 2 + st[:, 1] ** 2, 1.0)


if __name__ == '__main__':
    unittest.main()
