# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import tempfile
import unittest
from terrain_tileset import ROOT, validate, compile_tileset


class TerrainTileset(unittest.TestCase):
    def setUp(self):
        self.document = json.loads((ROOT / "data/terrain/tileset.json").read_text())

    def test_registered_materials_and_scalability(self):
        self.assertEqual(len(validate(self.document)), 80)
        for i in range(59):
            m = copy.deepcopy(self.document["materials"][2])
            m["key"] = f"fixture-{i}"
            self.document["materials"].append(m)
        self.assertEqual(len(self.document["materials"]), 64)
        self.assertEqual(len(validate(self.document)), 80)

    def test_invalid_catalogs(self):
        mutations = [
            lambda d: d.update(version=9),
            lambda d: d["materials"][0]["variants"][0].update(weight=0),
            lambda d: d["materials"][0]["variants"][0].update(frame=9999),
            lambda d: d["profiles"][0]["contours_q12"][0].__setitem__(0, 1),
            lambda d: d["materials"][0].update(profile="missing"),
            lambda d: d["materials"][0].update(key="sand"),
        ]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                d = copy.deepcopy(self.document)
                mutate(d)
                with self.assertRaises((ValueError, FileNotFoundError)):
                    validate(d)

    def test_pages_and_mips_are_reproducible(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            first = compile_tileset(self.document, a, page_size=128)
            self.assertGreater(len(first["pages"]), 1)
            second = compile_tileset(self.document, b, page_size=128)
            self.assertEqual(first, second)
            for p in a.iterdir():
                self.assertEqual(p.read_bytes(), (b / p.name).read_bytes())
            for material, scales in first["masks"].items():
                for native, hd in zip(scales["1"], scales["4"]):
                    self.assertEqual(native, hd[::4])


if __name__ == "__main__":
    unittest.main()
