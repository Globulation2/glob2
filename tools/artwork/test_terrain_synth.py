# SPDX-License-Identifier: GPL-3.0-or-later
"""Contracts for the procedural terrain materials.

Run with the pinned asset-tool environment (see docs/assets/terrain-production.md):

    artifacts/asset-tools/venv/bin/python -m unittest discover -s tools/artwork -p test_terrain_synth.py
"""
import json
from dataclasses import replace
import statistics
import sys
import unittest
from unittest import mock
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
    luma,
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
        cls.results, cls.hd = synth.synthesize_both(synth.SYNTH_ORDER, jobs=2)

    def tiles(self, name, phase=0):
        return self.results[name][phase]

    def test_every_catalogue_type_has_a_recipe(self):
        self.assertEqual(sorted(synth.RECIPES), sorted(synth.SYNTH_ORDER))
        self.assertEqual(len(synth.BUILTIN_ORDER), 24)
        for name, recipe in synth.RECIPES.items():
            self.assertEqual(recipe.name, name)
            self.assertIn(recipe.profile, ("rock", "soft", "crisp", "brush", "sand", "fractured", "cliff", "cobblestone", "frozen"))
            self.assertIn("height", recipe.seam)

    def test_determinism_matches_committed_frames_and_provenance(self):
        for name in synth.SYNTH_ORDER:
            if synth.RECIPES[name].placeholder_only:
                continue
            with self.subTest(material=name):
                self.assertEqual(synth.check_material(name, self.results[name], ROOT, self.hd[name]), [])

    def test_every_catalogue_sprite_frame_has_registered_hd(self):
        """Native-only terrain is limited to legacy art without an HD source."""
        catalog = json.loads((ROOT / "data/terrain/tileset.json").read_text())
        rows = {line.split()[0] for line in (ROOT / "data/highres/v1/frames.txt").read_text().splitlines()[1:]}
        native_only = {"data/gfx/terrain", "data/gfx/terrain-cobblestone"}
        for material in catalog["materials"]:
            sprite = material["sprite"]
            if sprite in native_only:
                continue
            decor = material.get("decor")
            if decor:
                for frame in decor["full"] + decor["edge"]:
                    name = f"{decor['sprite'].removeprefix('data/gfx/')}{frame}"
                    with self.subTest(frame=name):
                        self.assertIn(name, rows)
            stem = sprite.removeprefix("data/gfx/")
            phases = material.get("animation_frames", 1)
            stride = material.get("animation_stride", 0)
            for variant in material["variants"]:
                for phase in range(phases):
                    frame = f"{stem}{variant['frame'] + phase * stride}"
                    with self.subTest(frame=frame):
                        self.assertIn(frame, rows)
                        self.assertTrue((ROOT / "data/highres/v1" / f"{frame}.png").is_file())

    def test_hd_frames_match_their_classic_downsample(self):
        """HD frames are the 4x source of the classic tiles, not a separate look."""
        _, hd = synth.synthesize_both(["gravel", "lava"], jobs=1)
        for name, phases in hd.items():
            for phase, tiles in phases.items():
                for tile, native in zip(tiles, self.tiles(name, phase)):
                    with self.subTest(material=name, phase=phase):
                        self.assertEqual(tile.size, (4 * TILE, 4 * TILE))
                        down = tile.convert("RGB").resize((TILE, TILE), Image.Resampling.BOX)
                        self.assertLess(color_distance(mean_color([down]), mean_color([native])), 6)

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
                    self.assertEqual(len(tiles), (recipe.grid ** 2 or synth.VARIANTS) * recipe.blocks)
                    for tile in tiles:
                        self.assertEqual(tile.size, (TILE, TILE))
                        self.assertEqual(tile.mode, "RGBA")
                    self.assertEqual(len({tile.tobytes() for tile in tiles}), (recipe.grid ** 2 or synth.VARIANTS) * recipe.blocks)

    def grid_block(self, name, phase=0, hd=False, layout=0):
        """A grid material's variants assembled into their positional block."""
        grid = synth.RECIPES[name].grid
        tiles = self.hd[name][phase] if hd else self.tiles(name, phase)
        cells = grid * grid
        tiles = tiles[layout * cells:(layout + 1) * cells]
        tile_size = tiles[0].width
        block = Image.new("RGBA", (grid * tile_size, grid * tile_size))
        for variant, tile in enumerate(tiles):
            block.paste(tile, ((variant % grid) * tile_size, (variant // grid) * tile_size))
        return block

    def test_tileability(self):
        for name in synth.SYNTH_ORDER:
            if synth.RECIPES[name].grid:
                continue  # covered by test_grid_materials_continue_across_every_cell_edge
            tiles = self.tiles(name)
            grain = statistics.fmean(interior_grain(tile) for tile in tiles)
            seam = statistics.fmean(wrap_seam_error(tile) for tile in tiles)
            with self.subTest(material=name):
                self.assertLessEqual(seam, max(1.5 * grain, 1.0))

    def test_perimeter_agreement_for_all_ordered_pairs(self):
        for name, recipe in synth.RECIPES.items():
            if recipe.periodic:
                continue  # covered by test_periodic_materials_share_their_outer_ring_and_wrap
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
        # Every terrain material is opaque: nothing is drawn underneath terrain.
        for name in synth.SYNTH_ORDER:
            with self.subTest(material=name):
                self.assertEqual(style_stats(self.tiles(name))["alpha"], 255.0)

    def test_style_bands(self):
        for name, recipe in synth.RECIPES.items():
            stats = style_stats(self.tiles(name))
            with self.subTest(material=name, stats=stats):
                self.assertLessEqual(abs(stats["luma"] - recipe.style.luma), 6)
                tolerance = 3 + 3 * (1 - recipe.style.match)
                self.assertLessEqual(abs(stats["std"] - recipe.style.std), tolerance)
                self.assertLessEqual(stats["grain"], recipe.style.grain_max)
                # Object materials (pebbles, flower heads, tussocks) need contrast
                # between objects and gaps to read at 32 px; ground stays quiet.
                self.assertLessEqual(stats["std"], 36 if recipe.periodic else 26)

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
        for name in synth.SYNTH_ORDER:
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
        means = {name: mean_color(self.tiles(name)) for name in synth.SYNTH_ORDER}
        groups = {name: synth.RECIPES[name].group for name in synth.SYNTH_ORDER}
        groups.update(grass="grass", sand="sand", road="paths", ice="ice")
        # Road retains the original muted stone palette. Its cobblestone
        # contour profile and connected pavers distinguish it from gravel's
        # scattered stones, despite their intentionally similar mean colors.
        profile_distinguished = {frozenset(("road", "gravel"))}
        names = sorted(means)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                if groups[a] == groups[b]:
                    continue
                if frozenset((a, b)) in profile_distinguished:
                    self.assertEqual({synth.RECIPES[a].profile, synth.RECIPES[b].profile},
                                     {"cobblestone", "rock"})
                    continue
                with self.subTest(pair=(a, b)):
                    # A floor against accidental near-duplicates; readability also
                    # comes from texture, so neighbouring earth tones may sit close.
                    self.assertGreaterEqual(color_distance(means[a], means[b]), 14)

    def test_animated_phase_continuity(self):
        for name in ("lava", "ember_field"):
            recipe = synth.RECIPES[name]
            self.assertGreater(recipe.phases, 1)
            frames = synth.frames_of(name, self.results[name])
            stride = (recipe.grid ** 2 or synth.VARIANTS) * recipe.blocks
            self.assertEqual(len(frames), stride * recipe.phases)
            for variant in range(stride):
                phases = [frames[variant + stride * phase] for phase in range(recipe.phases)]
                self.assertEqual(len({p.tobytes() for p in phases}), recipe.phases)
                for current, following in zip(phases, phases[1:] + phases[:1]):
                    cur, nxt = current.load(), following.load()
                    diffs = [
                        abs(cur[x, y][k] - nxt[x, y][k]) for y in range(TILE) for x in range(TILE) for k in range(3)
                    ]
                    with self.subTest(material=name, variant=variant):
                        # Only the glow moves; the crust layout is shared by all phases.
                        self.assertLess(statistics.fmean(diffs), 12)
                        self.assertLess(sum(1 for d in diffs if d > 40) / len(diffs), 0.2)

    def test_runtime_blend_matches_the_compiler_border_preparation(self):
        sys.path.insert(0, str(ROOT / "tools"))
        from terrain_tileset import seamless_sources
        from material_tiles import runtime_blend

        for name in ("gravel", "deep_water"):
            document = {"materials": [{"sprite": f"data/gfx/terrain-{name}",
                                       "variants": [{"frame": i} for i in range(synth.VARIANTS)]}]}
            prepared = seamless_sources(document, ROOT)
            tiles = [Image.open(ROOT / f"data/gfx/terrain-{name}{i}.png").convert("RGBA") for i in range(synth.VARIANTS)]
            for i, blended in enumerate(runtime_blend(tiles)):
                self.assertEqual(blended.tobytes(), prepared[f"data/gfx/terrain-{name}{i}.png"].tobytes(), (name, i))

    def test_periodic_materials_share_their_outer_ring_and_wrap(self):
        """Periodic variants join any variant: identical outer ring, and each
        tile continues itself across its own edges (no hard seam)."""
        periodic = [n for n in synth.SYNTH_ORDER if synth.RECIPES[n].periodic and not synth.RECIPES[n].grid]
        self.assertEqual(periodic, [])  # Every shipped recipe now uses seeded full blocks.
        for name in periodic:
            tiles = [t.convert("RGB") for t in self.tiles(name)]
            ring = lambda t: [t.getpixel((x, y)) for y in range(TILE) for x in range(TILE)
                              if min(x, y, TILE - 1 - x, TILE - 1 - y) == 0]
            for tile in tiles[1:]:
                with self.subTest(material=name):
                    self.assertEqual(ring(tile), ring(tiles[0]))

    def test_grid_materials_continue_across_every_cell_edge(self):
        """Each grid variant joins its block neighbours, and the block wraps:
        cell-edge contrast stays within the interior's upper contrast range.
        Interior samples cover every grain interpolation phase and plank gap."""
        grid = [n for n in synth.SYNTH_ORDER if synth.RECIPES[n].grid]
        self.assertEqual(sorted(grid), sorted(synth.SYNTH_ORDER))
        for name in grid:
            recipe = synth.RECIPES[name]
            self.assertTrue(recipe.periodic)
            self.assertGreater(recipe.grid, 0)
            self.assertEqual(recipe.grid & (recipe.grid - 1), 0)
            for phase in range(recipe.phases):
                for hd in (False, True):
                    for layout in range(recipe.blocks):
                        block = self.grid_block(name, phase, hd=hd, layout=layout).convert("RGB")
                        size, tile_size = block.width, block.width // recipe.grid
                        import numpy as np
                        pixels = np.asarray(block, dtype=np.float64)
                        lum = 0.299 * pixels[:, :, 0] + 0.587 * pixels[:, :, 1] + 0.114 * pixels[:, :, 2]
                        columns = np.abs(lum - np.roll(lum, -1, axis=1)).mean(axis=0)
                        rows = np.abs(lum - np.roll(lum, -1, axis=0)).mean(axis=1)
                        for axis, steps in (("x", columns), ("y", rows)):
                            interior = sorted(step for i, step in enumerate(steps) if (i + 1) % tile_size)
                            upper_decile = interior[int(0.9 * (len(interior) - 1))]
                            for edge in range(tile_size - 1, size, tile_size):
                                with self.subTest(material=name, phase=phase, hd=hd, layout=layout, axis=axis, edge=edge):
                                    # A broad wave can put its steepest slope
                                    # at a tile cut. Compare that slope with
                                    # both adjacent lines as well as grain.
                                    adjacent = (steps[(edge - 1) % size] + steps[(edge + 1) % size]) / 2
                                    baseline = max(upper_decile, adjacent)
                                    self.assertLessEqual(steps[edge], 1.5 * baseline + 0.25)

    def test_interchangeable_layout_perimeters_match_and_interiors_differ(self):
        for name, recipe in synth.RECIPES.items():
            if recipe.blocks == 1:
                continue
            for phase in range(recipe.phases):
                for hd in (False, True):
                    blocks = [self.grid_block(name, phase, hd=hd, layout=layout)
                              for layout in range(recipe.blocks)]
                    size = blocks[0].width
                    strip = (synth.N if hd else TILE) // 8
                    boxes = ((0, 0, strip, size), (size - strip, 0, size, size),
                             (0, 0, size, strip), (0, size - strip, size, size))
                    for layout, block in enumerate(blocks[1:], 1):
                        for edge, box in enumerate(boxes):
                            with self.subTest(material=name, phase=phase, hd=hd, layout=layout, edge=edge):
                                self.assertEqual(block.crop(box).tobytes(), blocks[0].crop(box).tobytes())
                    interiors = {block.crop((strip, strip, size - strip, size - strip)).tobytes()
                                 for block in blocks}
                    self.assertEqual(len(interiors), recipe.blocks, (name, phase, hd))

    def test_water_waves_travel_in_small_steps_around_the_loop(self):
        # No wave term moves more than a quarter wavelength per phase (aliasing
        # would read as flicker), and every term closes the loop exactly.
        for kx, ky, cycles, _ in synth.WAVE_SWELL + synth.WAVE_CHOP:
            self.assertLessEqual(cycles / synth.WAVE_PHASES, 0.25, (kx, ky))
        self.assertEqual(synth.WAVE_SIZE % synth.WAVE_PHASES, 0)
        for name in ("water", "deep_water"):
            recipe = synth.RECIPES[name]
            self.assertEqual(recipe.phases, synth.WAVE_PHASES)
            for layout in range(recipe.blocks):
                blocks = [self.grid_block(name, phase, layout=layout) for phase in range(recipe.phases)]
                self.assertEqual(len({b.tobytes() for b in blocks}), recipe.phases)
                for current, following in zip(blocks, blocks[1:] + blocks[:1]):
                    a, b = current.convert("RGB").getdata(), following.convert("RGB").getdata()
                    diffs = [abs(p - q) for pa, pb in zip(a, b) for p, q in zip(pa, pb)]
                    with self.subTest(material=name, layout=layout):
                        self.assertGreater(statistics.fmean(diffs), 0.3)
                        self.assertLess(statistics.fmean(diffs), 6)

    def test_catalog_fragment_is_well_formed(self):
        fragment = synth.catalog_fragment(self.results)
        self.assertEqual([m["key"] for m in fragment["materials"]], synth.SYNTH_ORDER)
        self.assertEqual(fragment["bindings"], {name: name for name in synth.SYNTH_ORDER})
        for block in fragment["materials"]:
            self.assertEqual(block["sprite"], f"data/gfx/terrain-{block['key']}")
            recipe = synth.RECIPES[block["key"]]
            self.assertEqual(len(block["variants"]), (recipe.grid ** 2 or synth.VARIANTS) * recipe.blocks)
            self.assertNotIn("ocean", block)
            if synth.RECIPES[block["key"]].phases > 1:
                self.assertEqual((block["animation_frames"], block["animation_stride"]),
                                 (recipe.phases, (recipe.grid ** 2 or synth.VARIANTS) * recipe.blocks))
            if synth.RECIPES[block["key"]].grid:
                self.assertEqual((block["edges"], block["variant_grid"]), ("periodic", recipe.grid))
            self.assertEqual(block.get("variant_blocks", 1), recipe.blocks)
        pairs = {tuple(sorted((p["a"], p["b"]))) for p in fragment["pair_treatments"]}
        self.assertEqual(len(pairs), len(fragment["pair_treatments"]))
        for p in fragment["pair_treatments"]:
            self.assertIn(p["profile"], ("rock", "soft", "crisp", "brush", "fractured"))
        json.dumps(fragment)

    def test_provenance_records_statistics_only_references(self):
        for name in synth.SYNTH_ORDER:
            document = json.loads((ROOT / "datasrc/gfx" / name / "provenance.json").read_text())
            recipe = synth.RECIPES[name]
            with self.subTest(material=name):
                self.assertEqual(document["pillow"], synth.PILLOW_VERSION)
                blocks = recipe.blocks if document["method"] == "procedural" else 1
                cells = (recipe.grid ** 2 or synth.VARIANTS) if document["method"] == "procedural" else synth.VARIANTS
                self.assertEqual(len(document["runtime_sha256"]), cells * blocks * recipe.phases)
                if recipe.placeholder_only:
                    # Swapped to image-generated art: the record documents the prompt instead.
                    self.assertIn(document["method"], ("image-generator", "hybrid"))
                    self.assertTrue(document.get("prompt"))
                    continue
                self.assertEqual(document["method"], "procedural")
                self.assertIn("platform", document)
                for reference in document["style_references"].values():
                    self.assertEqual(reference["use"], "statistics only")


class TerrainBlockContracts(unittest.TestCase):
    """Small block-domain regressions without synthesising the material set."""

    def test_indexed_worley_matches_exhaustive_search_and_tie_order(self):
        import math
        import random

        rng = random.Random(71)
        points = [(rng.random() * 32, rng.random() * 32) for _ in range(80)]
        points += [points[0], (0.0, 0.0), (31.0, 31.0)]
        angles = [rng.random() * math.tau for _ in points]
        rotations = [(math.cos(a), math.sin(a)) for a in angles]
        rotations[-3] = rotations[0]  # Coincident geometry must prefer the original ID.
        for metric, stretch in (("euclid", 1.0), ("chebyshev", 1.0), ("angular", 0.35), ("angular", 2.6)):
            with self.subTest(metric=metric, stretch=stretch):
                expected = synth._worley_from_points(points, metric, rotations, stretch, size=32)
                actual = synth._worley_indexed(points, metric, rotations, stretch, size=32)
                self.assertEqual(actual, expected)

    def test_sampling_uses_the_whole_block_and_wraps_at_512(self):
        size = 4 * synth.N
        values = [float(x + size * y) for y in range(size) for x in range(size)]
        self.assertEqual(synth.sample(values, 300.25, 257.5), 300.25 + size * 257.5)
        self.assertEqual(synth.sample(values, size + 300.25, 257.5 - size), 300.25 + size * 257.5)
        self.assertEqual(synth.sample(values, size - 0.5, 0), (size - 1) / 2)
        self.assertEqual(synth.sample(values, 0, size - 0.5), size * (size - 1) / 2)

    def test_domain_warp_keeps_full_block_coordinates(self):
        size = 4 * synth.N
        values = [float(x + size * y) for y in range(size) for x in range(size)]
        shift_x, shift_y = [1.0] * len(values), [0.0] * len(values)
        warped = synth.domain_warp(values, shift_x, shift_y, 3, 2)
        self.assertEqual(len(warped), size * size)
        for x, y in ((300, 257), (size - 1, 0), (0, size - 1)):
            with self.subTest(x=x, y=y):
                self.assertEqual(warped[y * size + x], values[((y - 2) % size) * size + (x + 3) % size])

    def test_stamps_wrap_at_the_block_boundary(self):
        size = 4 * synth.N
        mask = [0.0] * (size * size)
        synth.stamp_disc(mask, size - 0.5, size - 0.5, 2)
        self.assertGreater(mask[(size - 1) * size + size - 1], 0)
        self.assertGreater(mask[0], 0)
        self.assertEqual(mask[synth.N * size + synth.N], 0)

    def test_render_phase_slices_one_block_without_changing_its_borders(self):
        for grid in (4, 8):
            with self.subTest(grid=grid):
                size = grid * synth.N
                # Four-pixel detail averages to integers, avoiding tie rounding
                # differences. Keep tones below clipping at either block size.
                rgb = [
                    [float(3 * min(x // 4, size // 4 - 1 - x // 4, 63) + 4 * (x % 4))
                     for y in range(size) for x in range(size)],
                    [float(3 * min(y // 4, size // 4 - 1 - y // 4, 63) + 4 * (y % 4))
                     for y in range(size) for x in range(size)],
                    [float(4 * ((x + y) % 4)) for y in range(size) for x in range(size)],
                ]
                render = mock.Mock(return_value=(rgb, None))
                original = synth.RECIPES["mud"]
                recipe = replace(original, render=render, grid=grid, blocks=1,
                                 style=replace(original.style, match=0))
                with mock.patch.dict(synth.RECIPES, mud=recipe):
                    native, hd = synth.render_phase("mud", 0)
                render.assert_called_once()
                self.assertEqual(render.call_args.args[0].size, size)
                expected = synth.fields_to_image(rgb, None, size)
                for tiles, whole in ((hd, expected),
                                     (native, expected.resize((size // 4, size // 4), Image.Resampling.BOX))):
                    self.assertEqual(len(tiles), grid * grid)
                    tile_size = tiles[0].width
                    reconstructed = Image.new("RGBA", (tile_size * grid, tile_size * grid))
                    for variant, tile in enumerate(tiles):
                        reconstructed.paste(tile, ((variant % grid) * tile_size, (variant // grid) * tile_size))
                    self.assertEqual(reconstructed.tobytes(), whole.tobytes())

    def test_positional_validator_accepts_recurring_pixel_phase_contrast(self):
        from validate_material import validate_grid_joins

        # The boundary step is twice the adjacent-line average, but the same
        # step recurs inside every tile; it is texture grain, not a grid seam.
        for tile_size in (TILE, synth.N):
            tile = Image.new("RGBA", (tile_size, tile_size))
            tile.putdata([(130, 130, 130, 255) if x % 8 == 7 else (128, 128, 128, 255)
                          for y in range(tile_size) for x in range(tile_size)])
            validate_grid_joins("deep_snow", 0, [tile] * 16, 4)

    def test_positional_validator_accepts_a_broad_wave_slope_at_a_tile_cut(self):
        import math
        import numpy as np
        from validate_material import validate_grid_joins

        for tile_size in (TILE, synth.N):
            size, grid = 4 * tile_size, 4
            # Smooth periodic water with two short regions of steep slope.
            # Center the highest slope between pixels across a tile boundary.
            tones = [round(128 + 40 * math.tanh(8 * math.sin(
                math.tau * (x - tile_size + 0.5) / size))) for x in range(size)]
            for axis in ("x", "y"):
                block = Image.new("RGBA", (size, size))
                block.putdata([(v, v, v, 255)
                               for y in range(size) for x in range(size)
                               for v in (tones[x if axis == "x" else y],)])
                steps = np.abs(np.array(tones, dtype=float) - np.roll(tones, -1))
                interior = sorted(step for i, step in enumerate(steps) if (i + 1) % tile_size)
                upper_decile = interior[int(0.9 * (len(interior) - 1))]
                # This case must exercise the local-slope allowance rather
                # than already passing the global grain threshold.
                self.assertGreater(steps[tile_size - 1], 1.5 * upper_decile + 0.25)
                tiles = [block.crop((x * tile_size, y * tile_size,
                                     (x + 1) * tile_size, (y + 1) * tile_size))
                         for y in range(grid) for x in range(grid)]
                with self.subTest(tile_size=tile_size, axis=axis):
                    validate_grid_joins("water", 0, tiles, grid)

    def test_positional_validator_rejects_native_and_hd_boundary_stripes(self):
        from validate_material import validate_grid_joins

        for tile_size in (TILE, synth.N):
            tiles = [Image.new("RGBA", (tile_size, tile_size), (128, 128, 128, 255)) for _ in range(16)]
            validate_grid_joins("deep_snow", 0, tiles, 4)
            for axis in ("x", "y"):
                broken = [tile.copy() for tile in tiles]
                box = ((tile_size - 1, 0, tile_size, tile_size) if axis == "x"
                       else (0, tile_size - 1, tile_size, tile_size))
                for tile in broken:
                    tile.paste((0, 0, 0, 255), box)
                with self.subTest(tile_size=tile_size, axis=axis):
                    with self.assertRaisesRegex(ValueError, "positional block has a seam"):
                        validate_grid_joins("deep_snow", 0, broken, 4)

    def test_interchangeable_validator_checks_edges_and_distinct_interiors(self):
        from validate_material import validate_grid_joins

        for grid, tile_size in ((4, TILE), (4, synth.N), (8, TILE), (8, synth.N)):
            size, strip = tile_size * grid, tile_size // 8
            cells = grid * grid
            tiles = []
            for layout in range(4):
                block = Image.new("RGBA", (size, size), (128, 128, 128, 255))
                tone = 130 + layout
                block.paste((tone, tone, tone, 255), (strip, strip, size - strip, size - strip))
                tiles.extend(block.crop((x * tile_size, y * tile_size, (x + 1) * tile_size, (y + 1) * tile_size))
                             for y in range(grid) for x in range(grid))
            validate_grid_joins("deep_snow", 0, tiles, grid)
            broken = [tile.copy() for tile in tiles]
            broken[cells].putpixel((0, 0), (0, 0, 0, 255))
            with self.subTest(grid=grid, tile_size=tile_size):
                with self.assertRaisesRegex(ValueError, "layout 1 perimeter strip"):
                    validate_grid_joins("deep_snow", 0, broken, grid)
                with self.assertRaisesRegex(ValueError, "layout interiors must be distinct"):
                    validate_grid_joins("deep_snow", 0, tiles[:cells] * 2, grid)

    def test_committed_catalog_keeps_recipe_edge_contracts(self):
        catalog = json.loads((ROOT / "data/terrain/tileset.json").read_text())
        materials = {material["key"]: material for material in catalog["materials"]}
        for name, recipe in synth.RECIPES.items():
            with self.subTest(material=name):
                self.assertEqual(materials[name].get("edges", "blend"), "periodic" if recipe.periodic else "blend")
                self.assertEqual(materials[name].get("variant_grid", 0), recipe.grid)
                self.assertEqual(materials[name].get("variant_blocks", 1), recipe.blocks)


if __name__ == "__main__":
    unittest.main()
