# SPDX-License-Identifier: GPL-3.0-or-later
"""Mastering, looping, preview, sources and manifest helpers on synthetic signals."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
import scipy.signal as ss

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music import loop, master, preview  # noqa: E402
from glob2music.audio import AudioFormatError, Trio, read_trio, write_trio  # noqa: E402
from glob2music.audio import read_audio  # noqa: E402
from glob2music.manifest import ManifestError, encode_within_ceiling, parse_manifest, set_spec  # noqa: E402
from glob2music.sources import SourceError, fetch  # noqa: E402
from glob2music.spec import DEFAULT_SPEC, SAMPLE_RATE as SR  # noqa: E402


def noise_loop(seconds=3.0, seed=0):
    return np.random.default_rng(seed).standard_normal((int(seconds * SR), 2)) * 0.1


class CircularFilterTest(unittest.TestCase):
    def test_filter_matches_steady_state_of_endless_loop(self):
        y = noise_loop()
        sos = ss.butter(2, 200, 'high', fs=SR, output='sos')
        circ = master.circular_sosfilt(y, sos)
        steady = ss.sosfilt(sos, np.tile(y, (6, 1)), axis=0)[4 * len(y):5 * len(y)]
        self.assertLess(np.abs(circ - steady).max(), 1e-9)

    def test_no_seam_discontinuity(self):
        t = np.arange(int(2 * SR)) / SR
        y = np.stack([np.sin(2 * np.pi * 3 * t) + 0.5] * 2, axis=1)    # whole cycles: smooth loop
        for fn in (lambda z: master.highpass(z, 30), lambda z: master.lowpass(z, 500),
                   lambda z: master.shelf(z, 2000, -6), lambda z: master.peak_eq(z, 300, 3)):
            out = fn(y)
            wrap = np.abs(out[0] - out[-1]).max()
            inner = np.abs(np.diff(out, axis=0)).max()
            self.assertLessEqual(wrap, inner * 1.01 + 1e-12)
        naive = ss.sosfilt(ss.butter(2, 30, 'high', fs=SR, output='sos'), y, axis=0)
        self.assertGreater(np.abs(naive[0] - naive[-1]).max(), 0.1)    # what circular avoids

    def test_circular_convolve_wraps_tail(self):
        x = np.zeros(1000)
        x[-1] = 1.0
        out = master.circular_convolve(x, np.array([1.0, 0.5, 0.25]))
        np.testing.assert_allclose(out[:2], [0.5, 0.25])
        self.assertAlmostEqual(out[-1], 1.0)


class LimiterTest(unittest.TestCase):
    def test_limiter_respects_ceiling(self):
        y = noise_loop(4.0) * 6.0            # peaks far above full scale
        y[-5] = [3.0, -3.0]                  # a peak right before the seam
        out = master.limit(y, -1.5)
        self.assertLessEqual(master.loop_true_peak_dbtp(out), -1.5 + 1e-6)
        self.assertGreater(master.integrated_lufs(out), master.integrated_lufs(y) - 12)

    def test_attack_ramps_over_an_isolated_over(self):
        # A steady low-frequency tone with one over far from the seam: the gain must
        # glide down over the look-ahead instead of stepping, and the ceiling holds.
        t = np.arange(2 * SR) / SR
        y = np.stack([0.3 * np.sin(2 * np.pi * 2.0 * t) + 0.4] * 2, axis=1)
        y[SR] = [1.0, 1.0]
        lookahead_s, ceiling_db = 0.003, -1.5
        out = master.limit(y, ceiling_db, lookahead_s=lookahead_s)
        self.assertLessEqual(master.loop_true_peak_dbtp(out), ceiling_db + 1e-6)
        gain = out[:, 0] / y[:, 0]
        depth = 1.0 - gain.min()
        self.assertGreater(depth, 0.1)                       # the limiter engaged
        look = int(round(lookahead_s * SR))
        steps = np.abs(np.diff(gain[SR - 4 * look:SR + 1]))   # the attack, up to the over
        # A linear ramp over the look-ahead moves at most depth / look per sample; the
        # former minimum-filter edge dropped the whole depth in one sample.
        self.assertLessEqual(steps.max(), depth / look * 1.05)
        # Well away from the over (and its release, which wraps over the seam) the gain
        # is a constant: the final static trim, if any.
        self.assertLess(np.ptp(gain[SR // 4:3 * SR // 4]), 1e-6)

    def test_finish_hits_targets(self):
        moods = {m: noise_loop(6.0, k) * g for k, (m, g) in enumerate((('calm', 0.3), ('building', 2.0),
                                                                        ('combat', 5.0)))}
        out = master.finish(Trio(**moods))
        for mood in ('calm', 'building', 'combat'):
            self.assertAlmostEqual(master.integrated_lufs(out[mood]), DEFAULT_SPEC.target_lufs[mood], delta=0.1)
            self.assertLessEqual(master.loop_true_peak_dbtp(out[mood]), DEFAULT_SPEC.master_ceiling_dbtp + 1e-6)
        self.assertIn('master', out.meta)


class LoopTest(unittest.TestCase):
    def test_fold_tail_and_preroll(self):
        y = np.zeros(14)
        y[2:12] = 1.0                        # pre-roll 2, loop 10
        y[12:] = [0.5, 0.25]                 # tail
        y[:2] = [0.1, 0.2]                   # early notes in the pre-roll
        out = loop.fold_tail(y, 10, preroll_frames=2)
        np.testing.assert_allclose(out, [1.5, 1.25, 1, 1, 1, 1, 1, 1, 1.1, 1.2])

    def test_crossfade_loop_is_seamless(self):
        t = np.arange(SR * 3) / SR
        src = np.sin(2 * np.pi * 220.7 * t)                  # not periodic in the cut
        y = loop.crossfade_loop(src, 4410, 4410 + SR, 2205)
        self.assertLess(abs(y[0] - y[-1]), np.abs(np.diff(y)).max() * 1.01)


class TrioIoTest(unittest.TestCase):
    def test_trio_contract_and_frame_exact_roundtrip(self):
        with self.assertRaises(AudioFormatError):
            Trio(calm=np.zeros(10), building=np.zeros(11), combat=np.zeros(10))
        n = SR * 2 + 123
        trio = Trio(**{m: np.random.default_rng(k).standard_normal((n, 2)) * 0.1 for k, m in enumerate(('calm', 'building', 'combat'))})
        with tempfile.TemporaryDirectory() as tmp:
            write_trio(trio, tmp)
            back = read_trio(tmp)
            self.assertEqual(back.frames, n)
            p = preview.make(back, Path(tmp) / 'preview.opus', segment_s=1.0, crossfade_s=0.2)
            self.assertTrue(p.exists())

    def test_failed_trio_encode_preserves_all_previous_files(self):
        from unittest import mock
        from glob2music import audio
        trio = Trio(**{m: np.zeros((4813, 2)) for m in ('calm', 'building', 'combat')})
        with tempfile.TemporaryDirectory() as tmp:
            paths = write_trio(trio, tmp)
            before = {m: p.read_bytes() for m, p in paths.items()}
            writer = audio.write_opus
            count = 0
            def fail_second(y, path, **kwargs):
                nonlocal count
                count += 1
                if count == 2:
                    raise RuntimeError('encoder failed')
                return writer(y, path, **kwargs)
            with mock.patch.object(audio, 'write_opus', side_effect=fail_second):
                with self.assertRaises(RuntimeError):
                    write_trio(trio, tmp)
            self.assertEqual({m: p.read_bytes() for m, p in paths.items()}, before)
            self.assertFalse(list(Path(tmp).glob('.trio-*')))

    def test_preview_follows_shared_position(self):
        n = SR
        trio = Trio(calm=np.full((n, 2), 0.1), building=np.full((n, 2), 0.2), combat=np.full((n, 2), 0.3))
        y = preview.render(trio, segment_s=1.5, crossfade_s=0.1, fade_out_s=0)
        self.assertEqual(len(y), int(1.5 * SR) * 4)
        self.assertAlmostEqual(y[int(2.0 * SR), 0], 0.2)
        self.assertAlmostEqual(y[int(3.5 * SR), 0], 0.3)


class SeamPreparationTest(unittest.TestCase):
    def test_configured_taper_keeps_frames_and_other_moods(self):
        trio = Trio(**{m: noise_loop(1.0, k) for k, m in enumerate(('calm', 'building', 'combat'))})
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp, mock.patch('glob2music.master.loop_true_peak_dbtp', return_value=-10):
            written = encode_within_ceiling(trio, Path(tmp), seam_ms={'calm': 2})
            self.assertEqual(written.frames, trio.frames)
            np.testing.assert_array_equal(written.building, trio.building)
            np.testing.assert_array_equal(written.combat, trio.combat)
            self.assertEqual(written.calm[0].tolist(), [0, 0])
            self.assertEqual(written.calm[-1].tolist(), [0, 0])
            np.testing.assert_array_equal(written.calm[96:-96], trio.calm[96:-96])
            self.assertEqual(written.meta['encode_seam_ms'], {'calm': 2})


class SourcesTest(unittest.TestCase):
    def test_fetch_verifies_and_caches(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'input.wav'
            src.write_bytes(b'not really audio')
            digest = hashlib.sha256(b'not really audio').hexdigest()
            got = fetch(src.as_uri(), digest, cache_dir=Path(tmp) / 'cache')
            self.assertEqual(got.read_bytes(), b'not really audio')
            self.assertEqual(fetch('https://invalid.example/input.wav', digest, cache_dir=Path(tmp) / 'cache',
                                   filename='input.wav', offline=True), got)
            with self.assertRaises(SourceError):
                fetch(src.as_uri(), '0' * 64, cache_dir=Path(tmp) / 'other')


class ManifestTest(unittest.TestCase):
    BASE = {'title': 'Test', 'method': 'adapted', 'license': 'CC0-1.0'}

    def test_waiver_needs_reason(self):
        with self.assertRaises(ManifestError):
            parse_manifest({**self.BASE, 'qa': {'waivers': {'seam': ''}}}, 'test-set')
        m = parse_manifest({**self.BASE, 'qa': {'waivers': {'seam.calm.gap': 'The source ends with a rest.'}}},
                           'test-set')
        self.assertIn('seam.calm.gap', m.waivers)

    def test_rejects_thresholds_and_bad_ids(self):
        with self.assertRaises(ManifestError):
            parse_manifest({**self.BASE, 'qa': {'repetition_fail': 0.99}}, 'test-set')
        with self.assertRaises(ManifestError):
            parse_manifest(self.BASE, 'Bad_Id')
        with self.assertRaises(ManifestError):
            parse_manifest({**self.BASE, 'ai_generated': True}, 'test-set')

    def test_loudness_offsets_move_the_target_within_tolerance(self):
        m = parse_manifest({**self.BASE, 'master': {'loudness_offset_db': {'calm': -1.0}}}, 'test-set')
        spec = set_spec(m)
        self.assertEqual(spec.target_lufs['calm'], DEFAULT_SPEC.target_lufs['calm'] - 1.0)
        self.assertEqual(spec.target_lufs['combat'], DEFAULT_SPEC.target_lufs['combat'])
        too_far = parse_manifest({**self.BASE, 'master': {'loudness_offset_db': {'calm': -3.0}}}, 'test-set')
        with self.assertRaises(ManifestError):
            set_spec(too_far)


class EncodeCeilingTest(unittest.TestCase):
    def test_bounded_peak_correction_enforces_the_actual_ceiling(self):
        from unittest import mock
        trio = Trio(**{m: np.zeros((4813, 2)) for m in ('calm', 'building', 'combat')})
        for peak, safe in [(-1.1, True), (-0.9, False)]:
            with self.subTest(peak=peak), tempfile.TemporaryDirectory() as tmp, \
                    mock.patch('glob2music.master.loop_true_peak_dbtp', return_value=peak):
                if safe:
                    encode_within_ceiling(trio, Path(tmp), attempts=0)
                else:
                    with self.assertRaises(RuntimeError):
                        encode_within_ceiling(trio, Path(tmp), attempts=0)

    def test_encoded_true_peak_stays_under_the_qa_ceiling(self):
        # Dense full-scale noise right at the mastering ceiling: low-quality Vorbis
        # overshoots it, and encode_within_ceiling must trim until it doesn't.
        rng = np.random.default_rng(3)
        y = rng.standard_normal((SR * 4, 2))
        y = master.limit(y / np.abs(y).max(), DEFAULT_SPEC.master_ceiling_dbtp)
        trio = Trio(y, y.copy(), y.copy())
        with tempfile.TemporaryDirectory() as tmp:
            written = encode_within_ceiling(trio, Path(tmp), DEFAULT_SPEC)
            for mood, name in (('calm', 'a1.opus'), ('building', 'a2.opus'), ('combat', 'a3.opus')):
                decoded, _ = read_audio(Path(tmp) / name)
                self.assertLessEqual(master.loop_true_peak_dbtp(decoded), DEFAULT_SPEC.qa.true_peak_max_dbtp)
                self.assertLessEqual(written.meta['encode_trim_db'][mood], 0.0)


if __name__ == '__main__':
    unittest.main()
