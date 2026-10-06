# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for the procedural terrain materials.

Run with the pinned asset encoder interpreter:

    "$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tools/artwork -p test_terrain_synth.py
"""
import json
import statistics
import sys
import unittest
from pathlib import Path

import PIL
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_synth as synth  # noqa: E402
from material_tiles import (  # noqa: E402
    ROOT,
    TILE,
    color_distance,
    interior_grain,
    join_error,
    mean_color,
    pixel_sha256,
    reference_tiles,
    style_stats,
    wrap_seam_error,
)


@unittest.skipUnless(PIL.__version__ == synth.PILLOW_VERSION, "needs the pinned Pillow for deterministic kernels")
class TerrainSynth(unittest.TestCase):
    results = None

    @classmethod
    def setUpClass(cls):
        cls.results = synth.synthesize(synth.BUILTIN_ORDER)

    def tiles(self, name, phase=0):
        return self.results[name][phase]

    def test_every_catalogue_type_has_a_recipe(self):
        self.assertEqual(sorted(synth.RECIPES), sorted(synth.BUILTIN_ORDER))
        self.assertEqual(len(synth.BUILTIN_ORDER), 24)
        for name, recipe in synth.RECIPES.items():
            self.assertEqual(recipe.name, name)
            self.assertIn(recipe.profile, ("rock", "soft", "crisp", "brush", "sand", "fractured"))
            self.assertIn("height", recipe.seam)

    def test_determinism_matches_committed_frames_and_provenance(self):
        for name in synth.BUILTIN_ORDER:
            if synth.RECIPES[name].placeholder_only:
                continue
            with self.subTest(material=name):
                self.assertEqual(synth.check_material(name, self.results[name], ROOT), [])

    def test_second_synthesis_is_identical(self):
        again = synth.synthesize(["dirt", "lava"], jobs=1)
        for name, phases in again.items():
            for phase, tiles in phases.items():
                for fresh, first in zip(tiles, self.tiles(name, phase)):
                    self.assertEqual(pixel_sha256(fresh), pixel_sha256(first))

    def test_frames_are_native_rgba_and_distinct(self):
        for name, recipe in synth.RECIPES.items():
            for phase in range(recipe.phases):
                tiles = self.tiles(name, phase)
                with self.subTest(material=name, phase=phase):
                    self.assertEqual(len(tiles), synth.VARIANTS)
                    for tile in tiles:
                        self.assertEqual(tile.size, (TILE, TILE))
                        self.assertEqual(tile.mode, "RGBA")
                    self.assertEqual(len({tile.tobytes() for tile in tiles}), synth.VARIANTS)

    def test_tileability(self):
        for name in synth.BUILTIN_ORDER:
            tiles = self.tiles(name)
            grain = statistics.fmean(interior_grain(tile) for tile in tiles)
            seam = statistics.fmean(wrap_seam_error(tile) for tile in tiles)
            with self.subTest(material=name):
                self.assertLessEqual(seam, max(1.5 * grain, 1.0))

    def test_perimeter_agreement_for_all_ordered_pairs(self):
        for name, recipe in synth.RECIPES.items():
            for phase in range(recipe.phases):
                tiles = self.tiles(name, phase)
                with self.subTest(material=name, phase=phase):
                    for a in tiles:
                        for b in tiles:
                            self.assertEqual(a.crop((31, 0, 32, 32)).tobytes(), b.crop((0, 0, 1, 32)).tobytes())
                            self.assertEqual(a.crop((0, 31, 32, 32)).tobytes(), b.crop((0, 0, 32, 1)).tobytes())
                            self.assertEqual(join_error(a, b), 0)

    def test_alpha_rules(self):
        for name, recipe in synth.RECIPES.items():
            low, high = recipe.alpha_range
            for tile in self.tiles(name):
                lo, hi = tile.getchannel("A").getextrema()
                with self.subTest(material=name):
                    self.assertGreaterEqual(lo, low)
                    self.assertLessEqual(hi, high)
        # Deep water about 160, dark water about 210: tints of the ocean, not covers.
        self.assertLess(style_stats(self.tiles("deep_water"))["alpha"], 175)
        self.assertGreater(style_stats(self.tiles("dark_water"))["alpha"], 200)

    def test_style_bands(self):
        for name, recipe in synth.RECIPES.items():
            stats = style_stats(self.tiles(name))
            with self.subTest(material=name, stats=stats):
                self.assertLessEqual(abs(stats["luma"] - recipe.style.luma), 6)
                tolerance = 3 + 3 * (1 - recipe.style.match)
                self.assertLessEqual(abs(stats["std"] - recipe.style.std), tolerance)
                self.assertLessEqual(stats["grain"], recipe.style.grain_max)
                self.assertLessEqual(stats["std"], 26)

    def test_ground_materials_stay_low_contrast_like_the_native_tiles(self):
        grass = style_stats(reference_tiles("grass"))
        trail = style_stats(reference_tiles("trail"))
        self.assertLess(grass["std"], 8)
        self.assertLess(trail["std"], 14)
        for name in ("dirt", "clay", "mud", "dirt_track", "loam", "moss", "spring_meadow", "deep_snow"):
            stats = style_stats(self.tiles(name))
            with self.subTest(material=name):
                self.assertLessEqual(stats["std"], 14)
                self.assertLessEqual(stats["grain"], 10)

    def test_preview_colors_follow_the_rendered_mean(self):
        previews = {}
        for name in synth.BUILTIN_ORDER:
            recipe = synth.RECIPES[name]
            preview, minimap, mean = synth.preview_colors(name, self.results[name])
            previews[name] = preview
            with self.subTest(material=name):
                if recipe.preview is None:
                    self.assertEqual(preview, list(mean))
                    for a, b in zip(preview, mean):
                        self.assertLessEqual(abs(a - b), 24)
                else:
                    # Legible overrides stay in the material's hue family.
                    self.assertLess(color_distance(preview, mean), 120)
                self.assertTrue(all(0 <= c <= 255 for c in preview + minimap))

    def test_non_sibling_materials_are_separable(self):
        means = {name: mean_color(self.tiles(name)) for name in synth.BUILTIN_ORDER}
        for legacy in ("grass", "sand", "trail", "ice"):
            means[legacy] = mean_color(reference_tiles(legacy))
        groups = {name: synth.RECIPES[name].group for name in synth.BUILTIN_ORDER}
        groups.update(grass="grass", sand="sand", trail="paths", ice="ice")
        names = sorted(means)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                if groups[a] == groups[b]:
                    continue
                with self.subTest(pair=(a, b)):
                    # A floor against accidental near-duplicates; readability also
                    # comes from texture, so neighbouring earth tones may sit close.
                    self.assertGreaterEqual(color_distance(means[a], means[b]), 14)

    def test_animated_phase_continuity(self):
        for name in ("lava", "ember_field"):
            recipe = synth.RECIPES[name]
            self.assertEqual(recipe.phases, 4)
            frames = synth.frames_of(name, self.results[name])
            self.assertEqual(len(frames), 64)
            for variant in range(synth.VARIANTS):
                phases = [frames[variant + 16 * phase] for phase in range(4)]
                self.assertEqual(len({p.tobytes() for p in phases}), 4)
                for current, following in zip(phases, phases[1:] + phases[:1]):
                    cur, nxt = current.load(), following.load()
                    diffs = [
                        abs(cur[x, y][k] - nxt[x, y][k]) for y in range(TILE) for x in range(TILE) for k in range(3)
                    ]
                    with self.subTest(material=name, variant=variant):
                        # Only the glow moves; the crust layout is shared by all phases.
                        self.assertLess(statistics.fmean(diffs), 12)
                        self.assertLess(sum(1 for d in diffs if d > 40) / len(diffs), 0.2)

    def test_catalog_fragment_is_well_formed(self):
        fragment = synth.catalog_fragment(self.results)
        self.assertEqual([m["key"] for m in fragment["materials"]], synth.BUILTIN_ORDER)
        self.assertEqual(fragment["bindings"], {name: name for name in synth.BUILTIN_ORDER})
        for block in fragment["materials"]:
            self.assertEqual(block["sprite"], f"data/gfx/terrain-{block['key']}")
            self.assertEqual(len(block["variants"]), 16)
            self.assertFalse(block["ocean"])
            if synth.RECIPES[block["key"]].phases > 1:
                self.assertEqual((block["animation_frames"], block["animation_stride"]), (4, 16))
        pairs = {tuple(sorted((p["a"], p["b"]))) for p in fragment["pair_treatments"]}
        self.assertEqual(len(pairs), len(fragment["pair_treatments"]))
        for p in fragment["pair_treatments"]:
            self.assertIn(p["profile"], ("rock", "soft", "crisp", "brush", "fractured"))
        json.dumps(fragment)

    def test_provenance_records_statistics_only_references(self):
        for name in synth.BUILTIN_ORDER:
            document = json.loads((ROOT / "datasrc/gfx" / name / "provenance.json").read_text())
            recipe = synth.RECIPES[name]
            with self.subTest(material=name):
                self.assertEqual(document["pillow"], synth.PILLOW_VERSION)
                self.assertEqual(len(document["runtime_sha256"]), 16 * recipe.phases)
                if recipe.placeholder_only:
                    # Swapped to image-generated art: the record documents the prompt instead.
                    self.assertIn(document["method"], ("image-generator", "hybrid"))
                    self.assertTrue(document.get("prompt"))
                    continue
                self.assertEqual(document["method"], "procedural")
                self.assertIn("platform", document)
                for reference in document["style_references"].values():
                    self.assertEqual(reference["use"], "statistics only")


if __name__ == "__main__":
    unittest.main()
