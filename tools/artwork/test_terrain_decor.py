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
            if name in decor.CONNECTED:
                material = next(m for m in catalog["materials"] if m["key"] == name)
                self.assertEqual(material["profile"], "canopy")
                for pair in catalog["pair_treatments"]:
                    if name in (pair["a"], pair["b"]):
                        self.assertEqual(pair["profile"], "canopy")
                start = decor.connected_first(name)
                self.assertEqual(block["connected"], list(range(start, start + 16 * decor.CANOPY_VARIANTS)))

    def test_frames_stay_inside_the_sprite_and_edges_are_smaller(self):
        for name, frames in self.results.items():
            coverage = []
            for native, hd in frames[:decor.FRAMES]:
                self.assertEqual(native.size, (decor.NATIVE, decor.NATIVE))
                self.assertEqual(hd.size, (decor.S, decor.S))
                alpha = hd.getchannel("A")
                # Nothing is cut by the frame: the outer ring is faded out.
                ring = [alpha.getpixel((x, y)) for x in range(decor.S) for y in (0, decor.S - 1)]
                ring += [alpha.getpixel((x, y)) for y in range(decor.S) for x in (0, decor.S - 1)]
                self.assertLessEqual(max(ring), 8, name)
                coverage.append(sum(alpha.getdata()) / 255)
            full = sum(coverage[: decor.FULL]) / decor.FULL
            edge = sum(coverage[decor.FULL :decor.FRAMES]) / decor.EDGE
            with self.subTest(decor=name):
                self.assertLess(edge, full)

    def test_connected_interiors_have_no_tile_rim_or_shadow(self):
        for name in decor.CONNECTED:
            frames = self.results[name]
            for variant in range(decor.CANOPY_VARIANTS):
                hd = frames[decor.FRAMES + 15 * decor.CANOPY_VARIANTS + variant][1]
                lift = 24
                core = hd.crop((0, decor.PAD // 2 - lift,
                                decor.CELL, decor.PAD // 2 - lift + decor.CELL))
                self.assertEqual(core.getchannel("A").getextrema(), (255, 255))
                # Full canopy is exactly a continuous surface. No shadow or
                # repeated front face extends below each row of interior cells.
                self.assertEqual(sum(hd.getchannel("A").getdata()), decor.CELL ** 2 * 255)
            empty = frames[decor.FRAMES][1]
            self.assertIsNone(empty.getbbox())

    def test_connected_masks_follow_the_painted_corners(self):
        for name in decor.CONNECTED:
            lift = 24
            for mask in range(1, 16):
                alpha = self.results[name][decor.FRAMES + mask * decor.CANOPY_VARIANTS][1].getchannel("A")
                for k, (x, y) in enumerate(((2, 2), (decor.CELL - 3, 2),
                                            (2, decor.CELL - 3), (decor.CELL - 3, decor.CELL - 3))):
                    self.assertEqual(alpha.getpixel((x, decor.PAD // 2 - lift + y)),
                                     255 if mask & (1 << k) else 0)
                if mask in (6, 9):
                    self.assertGreater(alpha.getpixel((decor.CELL // 2, decor.PAD // 2 - lift + decor.CELL // 2)), 240)

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
