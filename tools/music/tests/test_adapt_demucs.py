# SPDX-License-Identifier: GPL-3.0-or-later
"""glob2music.adapt.demucs with Demucs's tiny untrained test model (no weights download).

Skipped when the separation extras (requirements-separation.txt) are not installed.
Checks the parts the recipes rely on: stem names and shapes, frame-exact alignment
with the input, determinism under a seed, and the on-disk cache.
"""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from glob2music.adapt import demucs  # noqa: E402
from glob2music.spec import SAMPLE_RATE as SR  # noqa: E402

try:
    import torch  # noqa: F401
    from demucs.pretrained import demucs_unittest
except ImportError:  # pragma: no cover
    demucs_unittest = None


def test_mix(seconds=2.0):
    t = np.arange(int(seconds * SR)) / SR
    rng = np.random.default_rng(0)
    return np.stack([0.2 * np.sin(2 * np.pi * 220 * t), 0.2 * np.sin(2 * np.pi * 330 * t)], axis=1) \
        + 0.01 * rng.standard_normal((len(t), 2))


@unittest.skipIf(demucs_unittest is None, 'separation extras not installed')
class DemucsTest(unittest.TestCase):
    def setUp(self):
        self.net = demucs_unittest()
        self.net.eval()
        self.mix = test_mix()

    def test_run_model_shapes_and_names(self):
        out = demucs.run_model(self.net, self.mix, shifts=1, device='cpu')
        self.assertEqual(tuple(out), tuple(self.net.sources))
        for y in out.values():
            self.assertEqual(y.shape, self.mix.shape)
            self.assertTrue(np.all(np.isfinite(y)))

    def test_seeded_shifts_are_deterministic(self):
        a = demucs.run_model(self.net, self.mix, shifts=2, seed=3, device='cpu')
        b = demucs.run_model(self.net, self.mix, shifts=2, seed=3, device='cpu')
        for k in a:
            np.testing.assert_array_equal(a[k], b[k])

    def test_separate_caches_results(self):
        names = tuple(self.net.sources)
        with tempfile.TemporaryDirectory() as d, \
                mock.patch.dict(demucs.MODELS, {'unittest': names}), \
                mock.patch('demucs.pretrained.get_model', return_value=self.net) as get:
            first = demucs.separate(self.mix, 'unittest', cache_dir=d, device='cpu', shifts=1)
            second = demucs.separate(self.mix, 'unittest', cache_dir=d, device='cpu', shifts=1)
            self.assertEqual(get.call_count, 1)                  # second call came from the cache
            for k in names:
                np.testing.assert_allclose(first[k], second[k], atol=1e-6)   # float32 on disk
            demucs.separate(self.mix * 0.5, 'unittest', cache_dir=d, device='cpu', shifts=1)
            self.assertEqual(get.call_count, 2)                  # new input, new separation

    def test_unknown_model_is_refused(self):
        with self.assertRaises(ValueError):
            demucs.separate(self.mix, 'no-such-model')


if __name__ == '__main__':
    unittest.main()
