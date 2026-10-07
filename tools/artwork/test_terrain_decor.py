# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for the raised obstacle decor sprites.

    "$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tools/artwork -p test_terrain_decor.py
"""
import json
import sys
import unittest
from pathlib import Path

import PIL

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_decor as decor  # noqa: E402
from material_tiles import ROOT  # noqa: E402

PILLOW = "12.2.0"


@unittest.skipUnless(PIL.__version__ == PILLOW, "needs the pinned Pillow for deterministic kernels")
class TerrainDecor(unittest.TestCase):
    results = None

    @classmethod
    def setUpClass(cls):
        cls.results = decor.synthesize(decor.ORDER)

    def test_committed_frames_match_a_fresh_synthesis(self):
        self.assertEqual(decor.check(self.results, ROOT), [])

    def test_catalog_blocks_name_the_shared_sprite_and_frames(self):
        catalog = json.loads((ROOT / "data/terrain/tileset.json").read_text())
        blocks = {m["key"]: m["decor"] for m in catalog["materials"] if "decor" in m}
        self.assertEqual(sorted(blocks), sorted(decor.ORDER))
        for name, block in blocks.items():
            first = decor.first_frame(name)
            self.assertEqual(block["sprite"], decor.PREFIX)
            self.assertEqual(block["full"], list(range(first, first + decor.FULL)))
            self.assertEqual(block["edge"], list(range(first + decor.FULL, first + decor.FRAMES)))

    def test_frames_stay_inside_the_sprite_and_edges_are_smaller(self):
        for name, frames in self.results.items():
            coverage = []
            for native, hd in frames:
                self.assertEqual(native.size, (decor.NATIVE, decor.NATIVE))
                self.assertEqual(hd.size, (decor.S, decor.S))
                alpha = hd.getchannel("A")
                # Nothing is cut by the frame: the outer ring is faded out.
                ring = [alpha.getpixel((x, y)) for x in range(decor.S) for y in (0, decor.S - 1)]
                ring += [alpha.getpixel((x, y)) for y in range(decor.S) for x in (0, decor.S - 1)]
                self.assertLessEqual(max(ring), 8, name)
                coverage.append(sum(alpha.getdata()) / 255)
            full = sum(coverage[: decor.FULL]) / decor.FULL
            edge = sum(coverage[decor.FULL :]) / decor.EDGE
            with self.subTest(decor=name):
                self.assertLess(edge, full)

    def test_objects_rise_above_their_shadow(self):
        """Three-quarter view: opaque body mass sits higher in the frame than
        the soft ground shadow, which falls to the lower right."""
        for name, frames in self.results.items():
            native, hd = frames[0]
            pixels = hd.load()
            body = [(x, y) for y in range(decor.S) for x in range(decor.S) if pixels[x, y][3] > 240]
            soft = [(x, y) for y in range(decor.S) for x in range(decor.S) if 20 < pixels[x, y][3] < 160]
            with self.subTest(decor=name):
                self.assertTrue(body and soft)
                self.assertLess(sum(y for _, y in body) / len(body), sum(y for _, y in soft) / len(soft))


if __name__ == "__main__":
    unittest.main()
