# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import tempfile
import unittest
from terrain_tileset import ROOT, validate, compile_tileset, pixel_fingerprint, seamless_sources, data_path
from PIL import Image


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
            lambda d: d.update(version=True),
            lambda d: d.update(version=1.0),
            lambda d: d.update(pair_treatments={}),
            lambda d: d.update(bindings=[]),
            lambda d: d.update(compiled_pack="data/terrain/wrong.json"),
            lambda d: d.update(compiled_pack="data/atlas.json"),
            lambda d: d.update(compiled_pack=0),
            lambda d: d.update(profiles=[]),
            lambda d: d.update(materials={}),
            lambda d: d["bindings"].update(fixture="missing"),
            lambda d: d["materials"][0].update(ocean=1),
            lambda d: d["materials"][0].update(sprite=""),
            lambda d: d["materials"][0].update(sprite="data/../secret"),
            lambda d: d["materials"][0].update(sprite="data/terrain\\escape"),
            lambda d: d["materials"][0].update(minimap=[1, 2, True]),
            lambda d: d["materials"][1].update(backdrop={"sprite": "data/gfx/terrain", "ticks": 1.5}),
            lambda d: d["materials"][1].update(backdrop={"sprite": "data/gfx/terrain", "frames": True}),
            lambda d: d["materials"][1].update(backdrop={"sprite": "data/gfx/terrain", "ticks": 2147483648}),
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

    def test_asset_paths_are_canonical_and_portable(self):
        self.assertEqual(data_path("data/gfx/ice..cracked", "Sprite"), Path("data/gfx/ice..cracked"))
        for path in ("data//gfx/ice", "data/gfx/./ice", "data/gfx/../ice", "data/gfx/ice/",
                     "data/gfx/ice\\broken", "data/gfx/ice:broken", "data/gfx/ice\0broken"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                data_path(path, "Sprite")

    def test_variant_borders_preserve_premultiplied_alpha_continuity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "data").mkdir()
            Image.new("RGBA", (32, 32), (40, 80, 120, 128)).save(root / "data/tile0.png")
            Image.new("RGBA", (32, 32), (250, 0, 0, 0)).save(root / "data/tile1.png")
            document = {"materials": [{"sprite": "data/tile", "variants": [{"frame": 0}, {"frame": 1}]}]}
            frames = seamless_sources(document, root)
            first, second = frames["data/tile0.png"], frames["data/tile1.png"]
            for offset in range(32):
                for point in ((0, offset), (31, offset), (offset, 0), (offset, 31)):
                    self.assertEqual(first.getpixel(point), second.getpixel(point))
            self.assertEqual(second.getpixel((1, 16)), (40, 80, 120, 96))
            self.assertEqual(second.getpixel((16, 16)), (250, 0, 0, 0))

    def test_source_fingerprints_follow_exported_pixels(self):
        native = Image.new("RGBA", (32, 32), (23, 57, 91, 255))
        # Shared fixture value also asserted by the C++ pack loader regression.
        self.assertEqual(pixel_fingerprint(native), "422c101433645325")
        with tempfile.TemporaryDirectory() as tmp:
            fingerprints = {name: pixel_fingerprint(native) for name in validate(self.document)}
            packed = compile_tileset(self.document, Path(tmp), runtime_fingerprints=fingerprints)
            self.assertTrue(all(frame["native_rgba_fnv1a64"] == fingerprints[frame["source"]]
                                for frame in packed["frames"]))

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
