# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for the raised obstacle decor sprites.

    "$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tools/artwork -p test_terrain_decor.py
"""
import sys
import unittest
from pathlib import Path

import PIL

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_decor as decor  # noqa: E402
from material_tiles import ROOT  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
import terrain_catalog  # noqa: E402

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

    def test_catalog_blocks_name_the_shared_sprite_and_frames(self):
        catalog = terrain_catalog.load(ROOT / "content")
        blocks = {m["key"]: m["decor"] for m in catalog["materials"] if "decor" in m}
        self.assertEqual(sorted(blocks), sorted(decor.ORDER))
        for name, block in blocks.items():
            first = decor.first_frame(name)
            self.assertEqual(block["sprite"], decor.SPRITE)
            self.assertEqual(block["full"], list(range(first, first + decor.FULL)))
            self.assertEqual(block["edge"], list(range(first + decor.FULL, first + decor.FRAMES)))
            if name in decor.CONNECTED:
                material = next(m for m in catalog["materials"] if m["key"] == name)
                # Cover is the strongest class, so canopy draws all its contacts.
                self.assertEqual((material["profile"], material["class"]), ("canopy", "cover"))
                self.assertEqual(catalog["border_classes"][0]["key"], "cover")
                start = decor.connected_first(name)
                self.assertEqual(block["connected"], list(range(start, start + 16 * decor.CANOPY_VARIANTS)))
                self.assertEqual(block["connected_interior"],
                                 list(range(start + 16 * decor.CANOPY_VARIANTS, start + decor.CONNECTED_FRAMES)))
                self.assertIs(block["connected_corners"], True)

    def test_frames_stay_inside_the_sprite_and_edges_are_smaller(self):
        for name, frames in self.results.items():
            coverage = []
            n = decor.native_size(name)
            size = n * decor.SCALE
            for native, hd in frames[:decor.FRAMES]:
                self.assertEqual(native.size, (n, n))
                self.assertEqual(hd.size, (size, size))
                alpha = hd.getchannel("A")
                # Nothing is cut by the frame: the outer ring is faded out.
                ring = [alpha.getpixel((x, y)) for x in range(size) for y in (0, size - 1)]
                ring += [alpha.getpixel((x, y)) for y in range(size) for x in (0, size - 1)]
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
            lift = decor.raise_of(name)
            for index in filled:
                hd = frames[index][1]
                self.assertEqual(hd.size, (decor.CELL, decor.CANOPY_TOP + decor.CELL + decor.CANOPY_BELOW))
                top, bottom = decor.CANOPY_TOP - lift, decor.CANOPY_TOP + decor.CELL
                # A filled cell is one opaque block: the raised top and the wall
                # band the next row covers. Nothing spills above or sideways, so
                # no rim repeats from cell to cell; below it only the soft long
                # shadow falls, which the next row covers where canopy continues.
                self.assertEqual(hd.crop((0, top, decor.CELL, bottom)).getchannel("A").getextrema(), (255, 255))
                self.assertEqual(sum(hd.crop((0, 0, decor.CELL, bottom)).getchannel("A").getdata()),
                                 decor.CELL * (decor.CELL + lift) * 255)
                shadow = hd.crop((0, bottom, decor.CELL, hd.height)).getchannel("A")
                self.assertLess(shadow.getextrema()[1], 200)
                self.assertGreater(shadow.getpixel((decor.CELL // 2, 8)), 100)
                self.assertLess(shadow.getpixel((decor.CELL // 2, decor.CANOPY_BELOW - 1)), 16)
            empty = frames[decor.FRAMES][1]
            self.assertIsNone(empty.getbbox())

    def test_connected_masks_follow_the_painted_corners(self):
        for name in decor.CONNECTED:
            lift = decor.raise_of(name)
            for mask in range(1, 16):
                alpha = self.results[name][decor.FRAMES + mask * decor.CANOPY_VARIANTS][1].getchannel("A")
                for k, (x, y) in enumerate(((2, 2), (decor.CELL - 3, 2),
                                            (2, decor.CELL - 3), (decor.CELL - 3, decor.CELL - 3))):
                    if mask & (1 << k):
                        self.assertEqual(alpha.getpixel((x, decor.CANOPY_TOP - lift + y)), 255)
                    else:
                        # An open corner shows at most the soft ground shadow. Probe
                        # top corners where their raised top would be, and bottom
                        # corners on the ground, clear of nearby crowns' walls.
                        row = decor.CANOPY_TOP - lift + y if k < 2 else decor.CANOPY_TOP + y
                        self.assertLess(alpha.getpixel((x, row)), 128)
                if mask in (6, 9):
                    self.assertGreater(alpha.getpixel((decor.CELL // 2, decor.CANOPY_TOP - lift + decor.CELL // 2)), 240)

    def test_tall_boulders_rise_into_the_rows_above_and_cast_long_shadows(self):
        frames = self.results["boulders"]
        n = decor.native_size("boulders")
        self.assertEqual(n, 128)
        # The cell sits where the runtime anchors a tall frame: 16 native
        # pixels above the bottom edge, centred horizontally.
        cell_left, cell_top = decor.offset_x(n) * decor.SCALE, decor.offset_y(n) * decor.SCALE
        self.assertEqual((cell_left, cell_top), (48 * decor.SCALE, 80 * decor.SCALE))
        tops = []
        for index, (_, hd) in enumerate(frames[:decor.FULL]):
            pixels = hd.getchannel("A").load()
            body_top = min(y for y in range(hd.height) for x in range(0, hd.width, 2) if pixels[x, y] > 240)
            shadow_right = max(x for x in range(hd.width) for y in range(0, hd.height, 2) if 20 < pixels[x, y] < 200)
            tops.append(body_top)
            with self.subTest(frame=index):
                # Every full frame rises above its own cell into the row above...
                self.assertLess(body_top, cell_top)
                # ...and throws a shadow well past the cell's right edge.
                self.assertGreater(shadow_right, cell_left + (32 + 8) * decor.SCALE)
        # The tallest stand most of a cell above their own.
        self.assertLess(min(tops), cell_top - 24 * decor.SCALE)

    def test_objects_rise_above_their_shadow(self):
        """Three-quarter view: opaque body mass sits higher in the frame than
        the soft ground shadow, which falls to the lower right."""
        for name, frames in self.results.items():
            native, hd = frames[0]
            pixels = hd.load()
            body = [(x, y) for y in range(hd.height) for x in range(hd.width) if pixels[x, y][3] > 240]
            soft = [(x, y) for y in range(hd.height) for x in range(hd.width) if 20 < pixels[x, y][3] < 160]
            with self.subTest(decor=name):
                self.assertTrue(body and soft)
                self.assertLess(sum(y for _, y in body) / len(body), sum(y for _, y in soft) / len(soft))


if __name__ == "__main__":
    unittest.main()
