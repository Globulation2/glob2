# SPDX-License-Identifier: GPL-3.0-or-later
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_glints as glints  # noqa: E402


class TerrainGlintsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frames = glints.synthesize()

    def test_frames_are_centred_cell_overlays(self):
        self.assertEqual(len(self.frames), glints.LAYOUTS * glints.LEVELS)
        for native, hd in self.frames.values():
            self.assertEqual(native.size, (glints.NATIVE, glints.NATIVE))
            self.assertEqual(hd.size, (glints.S, glints.S))
            # Rays fade before the frame edge, so neighbours never show a seam.
            alpha = hd.getchannel("A")
            ring = [alpha.getpixel((x, 0)) for x in range(glints.S)] + \
                   [alpha.getpixel((0, y)) for y in range(glints.S)]
            self.assertEqual(max(ring), 0)

    def test_peak_is_the_brightest_level(self):
        for layout in range(glints.LAYOUTS):
            totals = [sum(self.frames[glints.frame_number(layout, level)][1].getchannel("A").getdata())
                      for level in range(glints.LEVELS)]
            self.assertEqual(totals.index(max(totals)), 1)

    def test_cycles_light_each_layout_once_then_stay_dark(self):
        for layout, cycle in enumerate(glints.cycles()):
            self.assertEqual(len(cycle), glints.CYCLE)
            self.assertEqual(cycle[:glints.LEVELS], [glints.frame_number(layout, k) for k in range(glints.LEVELS)])
            self.assertTrue(all(frame == -1 for frame in cycle[glints.LEVELS:]))

    def test_committed_assets_and_catalog_match(self):
        self.assertEqual(glints.check(self.frames, glints.ROOT), [])
        catalog = json.loads((glints.ROOT / glints.TILESET).read_text())
        with_overlay = sorted(m["key"] for m in catalog["materials"] if "overlay" in m)
        self.assertEqual(with_overlay, sorted(glints.FERTILE))


if __name__ == "__main__":
    unittest.main()
