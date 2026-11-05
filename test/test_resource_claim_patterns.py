#!/usr/bin/env python3
"""Geometry and reproducibility contracts for temporary claim-policy experiments."""
from dataclasses import replace
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.resource_claim_patterns import (DIRECTIONS, FAMILIES, ORIGINS, PROBE,
    Pattern, coordinates, encode, header, pilot_header, specifications, spiral, write_catalog)


class Patterns(unittest.TestCase):
    def test_spiral_visits_exact_disk_once(self):
        for radius in (0, 1, 2, 3, 8, 24):
            expected = {(x, y) for x in range(-radius, radius+1)
                        for y in range(-radius, radius+1) if x*x+y*y <= radius*radius}
            result = list(spiral(radius))
            self.assertEqual(result[0], (0, 0))
            self.assertEqual(len(result), len(set(result)))
            self.assertEqual(set(result), expected)

    def test_all_variants_are_bounded_unique_probes_with_unit_first(self):
        for spec in specifications(radii=(0, 1, 3), strides=(1, 2), directions=(0, 1)):
            probes = list(PROBE.iter_unpack(encode(spec)))
            self.assertEqual(probes[0], (0, 0, 0))
            self.assertEqual(len(probes), len(set(probes)))
            self.assertTrue(all(x*x+y*y <= spec.radius**2 and 0 <= origin < 3
                                for x, y, origin in probes))

    def test_cones_obey_direction_and_rays_stay_on_axes(self):
        for direction, (dx, dy) in enumerate(DIRECTIONS):
            for family in ('cone_toward', 'cone_away', 'cone_perpendicular'):
                points = coordinates(Pattern(family, 8, direction=direction))
                self.assertGreater(len(points), 1)
                for x, y in points:
                    along, across = x*dx+y*dy, y*dx-x*dy
                    self.assertTrue(along >= abs(across) if family == 'cone_toward' else
                                    -along >= abs(across) if family == 'cone_away' else
                                    abs(across) >= abs(along))
        for family in ('rays', 'rays_grouped', 'rays_toward'):
            self.assertTrue(all(x == 0 or y == 0 or abs(x) == abs(y)
                                for x, y in coordinates(Pattern(family, 8))))

    def test_sparse_random_variants_reuse_dense_ordering(self):
        for family in FAMILIES:
            for order in ('forward', 'reverse', 'shuffle'):
                spec = Pattern(family, 8, order=order)
                dense = coordinates(spec)[1:]
                self.assertEqual(coordinates(replace(spec, stride=3))[1:], dense[::3])
                sparse = coordinates(replace(spec, stride=3, subset='random_block'))[1:]
                self.assertEqual(len(sparse), (len(dense)+2)//3)
                for i, point in enumerate(sparse):
                    self.assertIn(point, dense[i*3:(i+1)*3])
                self.assertEqual(encode(spec), encode(spec))

    def test_catalog_deduplicates_and_failure_is_explicit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_catalog(root/'ok', [Pattern('spiral', 0), Pattern('random', 0)], headers=True)
            summary = json.loads((root/'ok'/'summary.json').read_text())
            self.assertTrue(summary['complete'])
            self.assertEqual((summary['variants'], summary['unique_patterns']), (2, 1))
            self.assertEqual(len(list((root/'ok').glob('*.h'))), 1)
            with self.assertRaisesRegex(ValueError, 'byte limit'):
                write_catalog(root/'limited', [Pattern('spiral', 8)], max_bytes=1)
            self.assertFalse(json.loads((root/'limited'/'summary.json').read_text())['complete'])
            with self.assertRaisesRegex(ValueError, 'variant limit'):
                write_catalog(root/'variants', [Pattern('spiral', 0)]*2, max_variants=1)
            self.assertFalse(json.loads((root/'variants'/'summary.json').read_text())['complete'])
            with self.assertRaises(FileExistsError):
                write_catalog(root/'ok', [])

    def test_header_uses_separate_signed_coordinates_and_origin_arrays(self):
        text = header('example', encode(Pattern('spiral', 1))).decode()
        self.assertIn('std::int16_t example_x[]', text)
        self.assertIn('std::int16_t example_y[]', text)
        self.assertIn('std::uint8_t example_origin[]', text)

    def test_checked_in_pilot_is_reproducible_and_explicitly_limited(self):
        path = Path(__file__).resolve().parents[1]/'src/map/claims/ResourceClaimPilotPatterns.h'
        self.assertEqual(path.read_bytes(), pilot_header())
        self.assertEqual(pilot_header().count(b':seed1"'), 240)


if __name__ == '__main__':
    unittest.main()
