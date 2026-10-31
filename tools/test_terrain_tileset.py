# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
import tempfile
import unittest
from terrain_tileset import (ROOT, BINDINGS, LEGACY_BINDINGS, validate, compile_tileset, pixel_fingerprint,
                             seamless_sources, data_path, required_bindings, material_provenance)
from PIL import Image


def catalog_frame_count(document):
    """Distinct source frames the catalog references (shared frames count once)."""
    frames = set()
    for m in document["materials"]:
        for v in m["variants"]:
            for phase in range(m.get("animation_frames", 1)):
                frames.add(f"{m['sprite']}{v['frame'] + phase * m.get('animation_stride', 0)}.png")
    return len(frames)


class TerrainTileset(unittest.TestCase):
    def setUp(self):
        self.document = json.loads((ROOT / "data/terrain/tileset.json").read_text())

    def test_boundary_shapes_are_explicit_and_version_gated(self):
        validate(self.document)
        for shape in ("unknown", 3, None, []):
            self.document["profiles"][0]["shape"] = shape
            with self.assertRaises(ValueError):
                validate(self.document)
        self.document["profiles"][0]["shape"] = "contextual"
        self.document["version"] = 2
        with self.assertRaises(ValueError):
            validate(self.document)

    def test_registered_materials_and_scalability(self):
        expected = catalog_frame_count(self.document)
        self.assertGreaterEqual(expected, 80)
        self.assertEqual(len(validate(self.document)), expected)
        for i in range(64 - len(self.document["materials"])):
            m = copy.deepcopy(self.document["materials"][2])
            m["key"] = f"fixture-{i}"
            self.document["materials"].append(m)
        self.assertEqual(len(self.document["materials"]), 64)
        self.assertEqual(len(validate(self.document)), expected)

    def test_interchangeable_blocks_require_complete_periodic_grids(self):
        # Animated materials stay on one layout: blocks multiply by phases.
        blocked = [m for m in self.document["materials"] if "variant_blocks" in m]
        self.assertEqual({m["key"] for m in self.document["materials"] if "variant_blocks" not in m},
                         {"water", "deep_water", "lava", "ember_field"})
        for material in blocked:
            self.assertEqual(material["variant_blocks"], 4)
            self.assertEqual(material["variant_grid"], 8)
            self.assertEqual(len(material["variants"]), 256)
            for value in (0, 2, 17, "4", True):
                invalid = copy.deepcopy(self.document)
                next(m for m in invalid["materials"] if m["key"] == material["key"])["variant_blocks"] = value
                with self.assertRaises(ValueError):
                    validate(invalid)
            invalid = copy.deepcopy(self.document)
            next(m for m in invalid["materials"] if m["key"] == material["key"]).pop("variant_grid")
            with self.assertRaises(ValueError):
                validate(invalid)

    def test_variant_block_groups_use_nonempty_utf8_keys_of_at_most_64_bytes(self):
        # Reduce to one material so each schema boundary checks only its frames.
        document = copy.deepcopy(self.document)
        document["materials"] = [document["materials"][0]]
        key = document["materials"][0]["key"]
        document["bindings"] = {name: key for name in document["bindings"]}
        document["pair_treatments"] = []
        for value in ("waves", "a" * 64, "é" * 32):
            document["materials"][0]["variant_block_group"] = value
            with self.subTest(valid=value):
                validate(document)
        for value in ("", "a" * 65, "é" * 33, True, 4, [], {}):
            document["materials"][0]["variant_block_group"] = value
            with self.subTest(invalid=value):
                with self.assertRaisesRegex(ValueError, "variant_block_group"):
                    validate(document)
        document["materials"][0]["variant_block_group"] = "waves"
        document["materials"][0].pop("variant_grid")
        with self.assertRaisesRegex(ValueError, "variant_block_group"):
            validate(document)

    def test_required_bindings_cover_legacy_and_builtin_names(self):
        names = json.loads((ROOT / "tools/terrain_builtin_names.json").read_text())
        self.assertEqual(BINDINGS[:5], LEGACY_BINDINGS)
        for name in names:
            self.assertIn(name, BINDINGS)
            self.assertIn(name, self.document["bindings"])
        self.assertEqual(len(set(BINDINGS)), len(BINDINGS))
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(required_bindings(Path(tmp)), LEGACY_BINDINGS)
        d = copy.deepcopy(self.document)
        del d["bindings"][names[-1]]
        with self.assertRaises((ValueError, KeyError)):
            validate(d)

    def test_material_provenance_is_per_material(self):
        provenance = material_provenance(self.document)
        self.assertEqual(set(provenance), {m["key"] for m in self.document["materials"]})
        self.assertEqual(provenance["grass"]["method"], "procedural")
        for key, record in provenance.items():
            self.assertIn(record["method"], ("procedural", "image-generator", "hybrid"), key)
            self.assertTrue((ROOT / record["source"]).is_file(), key)

    def test_invalid_catalogs(self):
        mutations = [
            lambda d: d.update(version=9),
            lambda d: d.update(version=True),
            lambda d: d.update(version=1.0),
            lambda d: d.update(version=1),
            lambda d: d.update(boundary_warp_q8={}),
            lambda d: d.update(boundary_warp_q8=[0, 0]),
            lambda d: d.update(boundary_warp_q8=[True, 0, 0]),
            lambda d: d.update(boundary_warp_q8=[0, 1.5, 0]),
            lambda d: d.update(boundary_warp_q8=[-1, 0, 0]),
            lambda d: d.update(boundary_warp_q8=[1025, 0, 0]),
            lambda d: d.update(boundary_warp_q8=[0, 385, 0]),
            lambda d: d.update(boundary_warp_q8=[0, 0, 129]),
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
            lambda d: d["profiles"][0]["contours_q12"].__setitem__(0, [0] * 6),
            lambda d: d["profiles"][0]["contours_q12"][0].__setitem__(1, 1025),
            lambda d: d["profiles"][0]["contours_q12"].__setitem__(slice(None), d["profiles"][0]["contours_q12"][:3]),
            lambda d: d["profiles"][0].update(contours_q12=[[0, 0, 0, 0, 0]] * 65),
            lambda d: d["profiles"][0].update(amplitude_q8=1025),
            lambda d: d["profiles"][0].update(speckle_q8=True),
            lambda d: d["profiles"][0].update(bridge_q8=-1),
            lambda d: d.update(version=2),
            lambda d: d["materials"][3].update(seam=[]),
            lambda d: d["materials"][3]["seam"].update(cast_q8=257),
            lambda d: d["materials"][3]["seam"].update(height=-1),
            lambda d: d["materials"][3]["seam"].update(fringe=[0, 0]),
            lambda d: d["materials"][3]["seam"].update(fringe_width_q8=2049),
            lambda d: d["profiles"][0].update(feather_q8=True),
            lambda d: d["profiles"][0].update(feather_q8=127),
            lambda d: d["profiles"][0].update(feather_q8=513),
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

    def test_contour_resolutions_share_native_and_hd_geometry(self):
        for count in (5, 9, 17, 33):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as tmp:
                d = copy.deepcopy(self.document)
                curve = [0] + [512 if i % 2 else -512 for i in range(count - 2)] + [0]
                d["profiles"][0]["contours_q12"] = [curve] * 4
                packed = compile_tileset(d, Path(tmp))
                masks = packed["masks"][d["profiles"][0]["key"]]
                self.assertEqual(masks["1"][0], masks["4"][0][::4])
                self.assertEqual(masks["4"][0][::64 // (count - 1)], curve)

    def test_version_one_catalog_remains_supported(self):
        self.document["version"] = 1
        self.document.pop("boundary_warp_q8")
        for profile in self.document["profiles"]:
            for field in ("feather_q8", "amplitude_q8", "speckle_q8", "bridge_q8", "shape"):
                profile.pop(field, None)
            profile["contours_q12"] = [[0, 128, -128, 64, 0]] * 4
        for material in self.document["materials"]:
            material.pop("seam", None)
        self.assertEqual(len(validate(self.document)), catalog_frame_count(self.document))
        self.document["profiles"][0]["contours_q12"][0][1] = 257
        with self.assertRaises(ValueError):
            validate(self.document)

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

    def blended(self):
        """The catalog with its first material's shared borders blended, not periodic."""
        document = copy.deepcopy(self.document)
        for field in ("edges", "variant_grid", "variant_blocks", "variant_block_group"):
            document["materials"][0].pop(field, None)
        return document, catalog_frame_count(dict(materials=document["materials"][:1]))

    def test_only_blended_frames_are_packed(self):
        with tempfile.TemporaryDirectory() as tmp:
            periodic = [m for m in self.document["materials"] if m.get("edges") == "periodic"]
            packed = compile_tileset(dict(self.document, materials=periodic), Path(tmp) / "a")
            self.assertEqual((packed["pages"], packed["frames"], packed["sources"]), ([], [], {}))
            document, count = self.blended()
            packed = compile_tileset(document, Path(tmp) / "b")
            prefix = document["materials"][0]["sprite"]
            self.assertEqual(len(packed["frames"]), count)
            self.assertTrue(all(f["source"].startswith(prefix) for f in packed["frames"]))
            self.assertEqual(sorted(packed["sources"]), sorted(f["source"] for f in packed["frames"]))
            self.assertTrue(all(len(page["levels"]) == 1 for page in packed["pages"]))

    def test_source_fingerprints_follow_exported_pixels(self):
        native = Image.new("RGBA", (32, 32), (23, 57, 91, 255))
        # Shared fixture value also asserted by the C++ pack loader regression.
        self.assertEqual(pixel_fingerprint(native), "422c101433645325")
        document, count = self.blended()
        with tempfile.TemporaryDirectory() as tmp:
            fingerprints = {name: pixel_fingerprint(native) for name in validate(document)}
            packed = compile_tileset(document, Path(tmp), runtime_fingerprints=fingerprints)
            self.assertEqual(len(packed["frames"]), count)
            self.assertTrue(all(frame["native_rgba_fnv1a64"] == fingerprints[frame["source"]]
                                for frame in packed["frames"]))

    def test_pages_and_mips_are_reproducible(self):
        document, _ = self.blended()
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            first = compile_tileset(document, a, page_size=128)
            self.assertGreater(len(first["pages"]), 1)
            second = compile_tileset(document, b, page_size=128)
            self.assertEqual(first, second)
            for p in a.iterdir():
                self.assertEqual(p.read_bytes(), (b / p.name).read_bytes())
            for material, scales in first["masks"].items():
                for native, hd in zip(scales["1"], scales["4"]):
                    self.assertEqual(native, hd[::4])


if __name__ == "__main__":
    unittest.main()
