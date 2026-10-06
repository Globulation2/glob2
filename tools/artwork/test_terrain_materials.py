"""Constraints that protect original color structure and connected terrain joins."""
import unittest

import numpy as np
from PIL import Image

from tools.artwork import terrain_materials as terrain


class TerrainMaterialTests(unittest.TestCase):
    def test_soft_color_target_keeps_grain_without_exact_block_sums(self):
        settings = dict(seed=23, smoothing=.8, connected_grain=11, fine_grain=5,
                        original_pull=.65, max_block_color_drift=2)
        native = Image.new('RGB', (8, 8), (80, 120, 80))
        image = terrain.refine(native, settings)
        self.assertEqual(image.tobytes(), terrain.refine(native, settings).tobytes())
        pixels = np.asarray(image, dtype=float)
        means = pixels.reshape(8, 4, 8, 4, 3).mean(axis=(1, 3))
        self.assertLessEqual(np.abs(means - (80, 120, 80)).max(), 2.5)
        self.assertTrue(np.any(means != (80, 120, 80)))
        self.assertGreater(pixels[:, :, 1].std(), 2)

    def test_zero_detail_preserves_constant_colors(self):
        settings = dict(seed=23, smoothing=.8, connected_grain=0, fine_grain=0,
                        original_pull=.65, max_block_color_drift=2)
        native = Image.new('RGB', (8, 8), (60, 100, 20))
        expected = native.resize((32, 32), Image.Resampling.NEAREST)
        self.assertEqual(terrain.refine(native, settings).tobytes(), expected.tobytes())

    def test_edges_match_every_legal_neighbor_and_preserve_interiors(self):
        corners = {i: c for i, c in terrain.topology(terrain.ROOT).items() if i < 256}
        rng = np.random.default_rng(23)
        for size in (16, 8, 4, 2):
            with self.subTest(size=size):
                tiles = {i: rng.integers(0, 256, (size, size, 4), dtype=np.uint8)
                         for i in corners}
                water = np.full((size, size, 4), (50, 70, 90, 0), dtype=np.uint8)
                before = {i: tile.copy() for i, tile in tiles.items()}
                terrain.compatible_edges(tiles, corners, water)
                self.assertTrue(np.all(water == (50, 70, 90, 0)))
                edges = {}
                for i, tile in tiles.items():
                    self.assertTrue(np.array_equal(tile[1:-1, 1:-1], before[i][1:-1, 1:-1]))
                    c = corners[i]
                    for key, edge in ((('vertical', c[0], c[2]), tile[:, 0]),
                                      (('vertical', c[1], c[3]), tile[:, -1]),
                                      (('horizontal', c[0], c[1]), tile[0]),
                                      (('horizontal', c[2], c[3]), tile[-1])):
                        if key in edges:
                            self.assertTrue(np.array_equal(edge, edges[key]), key)
                        else:
                            edges[key] = edge.copy()
                self.assertTrue(np.all(edges['vertical', 'E', 'E'] == water[:, 0]))
                self.assertTrue(np.all(edges['horizontal', 'E', 'E'] == water[0]))


if __name__ == '__main__':
    unittest.main()
