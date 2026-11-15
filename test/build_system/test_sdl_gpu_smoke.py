# SPDX-License-Identifier: GPL-3.0-or-later
"""Frame-time summaries of the SDL GPU device smoke run."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'buildsys'))
sys.path.insert(0, str(ROOT / 'platforms/mobile'))
import sdl_gpu_smoke  # noqa: E402


class FrameSummaryTests(unittest.TestCase):
    def test_percentiles_and_hitches(self):
        intervals = [16.7] * 97 + [33.4, 60.0, 120.0]
        summary = sdl_gpu_smoke.summarize(intervals, 50)
        self.assertEqual(summary['frames'], 101)
        self.assertEqual(summary['p50_ms'], 16.7)
        self.assertEqual(summary['max_ms'], 120.0)
        self.assertEqual(summary['hitches_over_50ms'], 2)
        self.assertEqual(summary['worst_ms'][:2], [120.0, 60.0])
        self.assertEqual(sdl_gpu_smoke.summarize([], 50), {'frames': 0})

    def test_surfaceflinger_history_ignores_pending_fences(self):
        class FakeAdb:
            def __call__(self, *command, **kwargs):
                if '--list' in command:
                    return 'SurfaceView[org.globulation2.glob2/org.globulation2.glob2.Glob2Activity](BLAST)#7\n'
                pending = str((1 << 63) - 1)
                return '16666666\n1 1000000 1\n2 17666666 2\n3 ' + pending + ' 3\n0 0 0\n'
        recorder = sdl_gpu_smoke.FrameRecorder(FakeAdb())
        recorder.sample()
        self.assertEqual(sorted(recorder.presents), [1000000, 17666666])
        self.assertIn('BLAST', recorder.layer)


if __name__ == '__main__':
    unittest.main()
