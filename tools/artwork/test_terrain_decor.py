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

    def test_vertex_clusters_stay_out_of_neighbouring_halos(self):
        # Frames sharing an edge must compute the same heights there.
        for name in decor.CONNECTED:
            for colour in (0, 1):
                heights, _ = decor.canopy_cluster(name, colour)
                box = heights.getbbox()
                self.assertGreaterEqual(min(box[:2]), 0)
                self.assertLessEqual(max(box[2:]), 2 * decor.REACH)
            for interior in range(decor.CANOPY_INTERIORS):
                box = decor.canopy_interior(name, interior)[0].getbbox()
                self.assertGreaterEqual(min(box[:2]), 8)
                self.assertLessEqual(max(box[2:]), decor.CELL - 8)

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
                self.assertEqual(block["connected_interior"],
                                 list(range(start + 16 * decor.CANOPY_VARIANTS, start + decor.CONNECTED_FRAMES)))
                self.assertIs(block["connected_corners"], True)

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
            filled = [decor.FRAMES + 15 * decor.CANOPY_VARIANTS + v for v in range(decor.CANOPY_VARIANTS)]
            filled += range(decor.FRAMES + 16 * decor.CANOPY_VARIANTS, decor.FRAMES + decor.CONNECTED_FRAMES)
            for index in filled:
                hd = frames[index][1]
                top = decor.CANOPY_TOP - decor.RAISE
                # A filled cell is one opaque block: the raised top and the wall
                # band the next row covers. Nothing spills above, below or
                # sideways, so no rim or shadow repeats from cell to cell.
                self.assertEqual(hd.crop((0, top, decor.CELL, decor.CANOPY_TOP + decor.CELL))
                                 .getchannel("A").getextrema(), (255, 255))
                self.assertEqual(sum(hd.getchannel("A").getdata()),
                                 decor.CELL * (decor.CELL + decor.RAISE) * 255)
            empty = frames[decor.FRAMES][1]
            self.assertIsNone(empty.getbbox())

    def test_connected_masks_follow_the_painted_corners(self):
        for name in decor.CONNECTED:
            lift = 24
            for mask in range(1, 16):
                alpha = self.results[name][decor.FRAMES + mask * decor.CANOPY_VARIANTS][1].getchannel("A")
                for k, (x, y) in enumerate(((2, 2), (decor.CELL - 3, 2),
                                            (2, decor.CELL - 3), (decor.CELL - 3, decor.CELL - 3))):
                    if mask & (1 << k):
                        self.assertEqual(alpha.getpixel((x, decor.PAD // 2 - lift + y)), 255)
                    else:
                        # An open corner shows at most the soft ground shadow. Probe
                        # top corners where their raised top would be, and bottom
                        # corners on the ground, clear of nearby crowns' walls.
                        row = decor.PAD // 2 - lift + y if k < 2 else decor.CANOPY_TOP + y
                        self.assertLess(alpha.getpixel((x, row)), 128)
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
