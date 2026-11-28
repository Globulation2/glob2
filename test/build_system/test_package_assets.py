"""Runtime export selection, identity, cleanup and source preservation."""

import json
import os
import re
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch
from PIL import Image
from tools import package_assets
from tools.package_assets import export_assets, include_asset
from tools import terrain_tileset


class EncodingExecutorTests(unittest.TestCase):
    def test_spawned_workers_import_tools_after_the_repository_leaves_sys_path(self):
        # A caller may drop the repository root from sys.path after importing
        # tools.package_assets; spawned workers must still find it.
        root = package_assets.ROOT.resolve()
        saved = list(sys.path)
        sys.path[:] = [p for p in sys.path if Path(p or os.getcwd()).resolve() != root]
        try:
            with package_assets.encoding_executor(2) as executor:
                self.assertEqual(executor.submit(package_assets.packaging_jobs, 3).result(timeout=120), 3)
        finally:
            sys.path[:] = saved


class TerrainRuntimeFingerprintTests(unittest.TestCase):
    def test_fingerprints_use_decoded_lossy_exports_and_sheet_regions(self):
        from tools import terrain_tileset
        import random

        with tempfile.TemporaryDirectory() as tmp:
            root, stage = Path(tmp) / "source", Path(tmp) / "stage"
            (root / "content/data/gfx").mkdir(parents=True)
            (stage / "data/gfx").mkdir(parents=True)
            names = [f"data/gfx/fixture{i}.png" for i in range(2)]
            images = [Image.frombytes("RGB", (32, 32), random.Random(i).randbytes(32 * 32 * 3))
                      for i in range(2)]
            for name, image in zip(names, images):
                image.save(root / "content" / name)
            images[0].save(stage / "data/gfx/fixture0.webp", quality=25)
            sheet = Image.new("RGB", (32 * package_assets.SHEET_COLUMNS, 32))
            sheet.paste(images[1], (32, 0))
            sheet.save(stage / "data/gfx/test-sheet-0.webp", quality=25)
            plan = {"data/gfx/test": [{"name": "test-sheet-0.png", "size": (32, 32),
                                      "frames": [root / "content/unused.png", root / "content" / names[1]]}]}
            fingerprints = package_assets.terrain_runtime_fingerprints(
                terrain_tileset, names, root / "content", stage, plan
            )
            with Image.open(stage / "data/gfx/fixture0.webp") as decoded:
                self.assertEqual(fingerprints[names[0]], terrain_tileset.pixel_fingerprint(decoded))
            with Image.open(stage / "data/gfx/test-sheet-0.webp") as decoded:
                self.assertEqual(fingerprints[names[1]], terrain_tileset.pixel_fingerprint(
                    decoded.crop((32, 0, 64, 32))))
            for name, original in zip(names, images):
                self.assertNotEqual(fingerprints[name], terrain_tileset.pixel_fingerprint(original))


class TerrainEncoderHandoffTests(unittest.TestCase):
    def test_catalog_exports_delegate_before_importing_pillow(self):
        import json
        import os
        import shutil
        import subprocess
        import sys

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "source"
            (root / "content/data/gfx").mkdir(parents=True)
            (root / "content/data/terrain").mkdir()
            (root / "tools").mkdir()
            for tool in ("terrain_tileset.py", "terrain_catalog.py"):
                shutil.copyfile(Path(package_assets.__file__).with_name(tool), root / "tools" / tool)
            Image.new("RGBA", (32, 32), (20, 80, 130, 255)).save(root / "content/data/gfx/terrain-test0.png")
            document = {
                "version": 1, "compiled_pack": "data/terrain/compiled/atlas.json",
                "profiles": [{"key": "flat", "roughness_q8": 0,
                              "contours_q12": [[0] * 5 for _ in range(4)]}],
                "materials": [{"key": "water", "profile": "flat",
                               "sprite": "data/gfx/terrain-test", "preview": [20, 80, 130],
                               "variants": [{"frame": 0, "weight": 1}]}],
                "bindings": {key: "water" for key in terrain_tileset.BINDINGS},
            }
            (root / "content/data/terrain/tileset.json").write_text(json.dumps(document))
            script = (
                "import importlib.util,sys; "
                "assert importlib.util.find_spec('PIL') is None; "
                "from tools.package_assets import export_assets; "
                "export_assets(sys.argv[1],sys.argv[2],optimized=sys.argv[3]=='1',terrain_sheets=True)"
            )
            env = dict(os.environ, GLOB2_ASSET_ENCODER_PYTHON=package_assets.encoder_python())
            for optimized in (True, False):
                with self.subTest(optimized=optimized):
                    output = Path(tmp) / str(optimized)
                    result = subprocess.run(
                        [sys.executable, "-S", "-c", script, str(root), str(output), str(int(optimized))],
                        cwd=package_assets.ROOT, env=env, capture_output=True, text=True, timeout=60,
                    )
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    audit = json.loads(output.with_suffix(".json").read_text())
                    self.assertEqual(audit["optimized"], optimized)
                    self.assertTrue((output / document["compiled_pack"]).is_file())
                    if optimized:
                        family = "data/gfx/terrain-test"
                        selection = audit["terrain_sheet_selections"][family]
                        candidates = selection["candidates"]
                        self.assertEqual(set(candidates), {"lossless", "q75"})
                        self.assertEqual(candidates[selection["winner"]]["stored_bytes"],
                                         min(c["stored_bytes"] for c in candidates.values()))
                        self.assertTrue((output / (family + ".sheet")).is_file())
                        self.assertFalse((output / (family + "0.webp")).exists())
                        # Terrain families pack as sprite groups in every packed export.
                        packs = sorted((output / "data/gfx").glob("pack-terrain-test-base-*.webp"))
                        self.assertTrue(packs)
                        self.assertEqual(audit["native_sprite_selections"]["terrain-test-base"]["curve"],
                                         {"fallback": "no curve"})
                        with Image.open(packs[0]) as sheet:
                            self.assertEqual(sheet.convert("RGBA").crop((0, 0, 32, 32))
                                             .getchannel("A").getextrema(), (255, 255))
                    else:
                        self.assertEqual((output / "data/gfx/terrain-test0.png").read_bytes(),
                                         (root / "content/data/gfx/terrain-test0.png").read_bytes())


class AssetExportTests(unittest.TestCase):
    def test_script_diagnostics_are_not_runtime_assets(self):
        self.assertFalse(include_asset('data/fonts/script-probes.json'))
        self.assertFalse(include_asset('data/fonts/check_script_support.py'))
        self.assertTrue(include_asset('data/fonts/scripts/NotoSansDevanagari-Regular.ttf'))
        self.assertTrue(include_asset('data/fonts/LICENSE-Noto.txt'))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.root.mkdir()
        self.output = Path(self.temp.name) / "output"
        self.cache = Path(self.temp.name) / "cache"
        self.image = self.root / "content/data/gfx/frame0.png"
        self.image.parent.mkdir(parents=True)
        im = Image.new("RGBA", (64, 64), (20, 80, 130, 0))
        im.putpixel((0, 0), (200, 40, 30, 255))
        im.save(self.image)
        (self.root / "content/data/fonts").mkdir()
        (self.root / "content/data/fonts/LICENSE.txt").write_text("notice")

    def export(self, **kwargs):
        kwargs.setdefault("jobs", 1)
        return export_assets(self.root, self.output, cache=self.cache, **kwargs)

    def test_serial_and_parallel_exports_are_identical_with_cold_caches(self):
        import random
        for index in range(8):
            Image.frombytes("RGBA", (48, 48), random.Random(index).randbytes(48 * 48 * 4)).save(
                self.image.parent / f"fixture{index}.png")
        # Identical inputs exercise simultaneous writes to a shared cache key.
        (self.image.parent / "duplicate.png").write_bytes(self.image.read_bytes())
        for lossy in (True, False):
            with self.subTest(lossy=lossy):
                serial = export_assets(self.root, self.output, cache=self.cache / f"serial-{lossy}",
                                       jobs=1, lossy=lossy)
                before = {p.relative_to(self.output): p.read_bytes()
                          for p in self.output.rglob("*") if p.is_file()}
                parallel = export_assets(self.root, self.output, cache=self.cache / f"parallel-{lossy}",
                                         jobs=4, lossy=lossy)
                self.assertEqual(serial, parallel)
                self.assertEqual(before, {p.relative_to(self.output): p.read_bytes()
                                          for p in self.output.rglob("*") if p.is_file()})

    def test_worker_budget_survives_pinned_encoder_handoff(self):
        import json
        with patch.object(package_assets, "encoder_ready", return_value=False), \
                patch.object(package_assets, "encoder_python", return_value="pinned-python"), \
                patch.object(package_assets.subprocess, "run") as run:
            self.output.with_suffix(".json").write_text(json.dumps({"files": []}))
            self.export(jobs=3)
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--jobs") + 1], "3")

    def test_worker_in_configured_encoder_needs_no_quality_gate(self):
        # Windows release jobs configure a Pillow-only encoder; video sprites
        # fall back to SSIM there instead of failing the export.
        # The export then builds the embedded sprite encoder (FFmpeg, which needs
        # NASM); reaching that build means the readiness check let it through.
        def ready(require_brotli=False, require_quality=False):
            return not require_quality
        class Reached(Exception):
            pass
        def build(*args, **kwargs):
            raise Reached
        encoder = types.ModuleType("recording_dependencies")
        encoder.build = build
        with patch.object(package_assets, "encoder_ready", side_effect=ready), \
                patch.dict(sys.modules, {"recording_dependencies": encoder}), self.assertRaises(Reached):
            self.export(worker=True, video_sprites=True)

    def test_parallel_encoder_failure_preserves_existing_export(self):
        self.export(jobs=1)
        original_audit = self.output.with_suffix(".json").read_bytes()
        before = {p.relative_to(self.output): p.read_bytes()
                  for p in self.output.rglob("*") if p.is_file()}
        Image.new("I;16", (8, 8)).save(self.image)
        with self.assertRaisesRegex(ValueError, "Unsupported higher-depth"):
            self.export(jobs=4)
        self.assertEqual(original_audit, self.output.with_suffix(".json").read_bytes())
        self.assertEqual(before, {p.relative_to(self.output): p.read_bytes()
                                  for p in self.output.rglob("*") if p.is_file()})

    def test_invalid_worker_counts_are_rejected(self):
        for jobs in (0, -1, 1.5):
            with self.subTest(jobs=jobs), self.assertRaisesRegex(ValueError, "positive integer"):
                self.export(jobs=jobs)

    def test_exact_pixels_including_invisible_rgb_and_sources_unchanged(self):
        original = self.image.read_bytes()
        r = self.export(lossy=False)
        record = next(x for x in r["files"] if x["source"].endswith(".png"))
        self.assertTrue(record["output"].endswith(".webp"))
        self.assertEqual(
            Image.open(self.output / record["output"]).convert("RGBA").tobytes(),
            Image.open(self.image).convert("RGBA").tobytes(),
        )
        self.assertEqual(self.image.read_bytes(), original)
        self.assertTrue((self.output / "data/fonts/LICENSE.txt").is_file())
        self.assertFalse((self.output / "export.json").exists())

    def test_cached_export_is_repeatable_and_stale_files_disappear(self):
        first = self.export()
        self.assertEqual(first, self.export())
        self.image.unlink()
        self.export()
        self.assertFalse(any((self.output / "data/gfx").glob("*")))

    def test_cache_blob_corruption_is_repaired(self):
        first = self.export()
        for blob in self.cache.glob("*/image.*"):
            blob.write_bytes(b"corrupt")
        self.assertEqual(first, self.export())

    def test_cache_metadata_corruption_is_repaired(self):
        first = self.export()
        for metadata in self.cache.glob("*/selection.json"):
            metadata.write_text("{bad json")
        self.assertEqual(first, self.export())

    def test_shared_cache_publication_accepts_only_identical_concurrent_writers(self):
        import hashlib
        import json
        import os

        replace = os.replace
        for blocked in ("image.webp", "selection.json"):
            for identical in (True, False):
                with self.subTest(blocked=blocked, identical=identical):
                    cache = self.cache / blocked / str(identical)

                    def reader_blocks_replacement(source, destination):
                        destination = Path(destination)
                        if destination.name == blocked:
                            # Another exporter published before this worker,
                            # and a Windows reader now prevents replacement.
                            destination.write_bytes(Path(source).read_bytes() if identical else b"conflicting")
                            raise PermissionError("Concurrent cache reader")
                        return replace(source, destination)

                    with patch("tools.package_assets.os.replace", side_effect=reader_blocks_replacement), \
                            patch("tools.package_assets.time.sleep"):
                        if identical:
                            blob, record = package_assets.encode_image(self.image, "data/gfx/frame0.png", cache, False)
                            self.assertEqual(hashlib.sha256(blob.read_bytes()).hexdigest(), record["sha256"])
                            self.assertEqual(json.loads(blob.with_name("selection.json").read_text()), record)
                        else:
                            with self.assertRaises(PermissionError):
                                package_assets.encode_image(self.image, "data/gfx/frame0.png", cache, False)
                    entry, = cache.iterdir()
                    self.assertTrue(all(p.name in ("image.webp", "selection.json") for p in entry.iterdir()))
                    # A conflicting publication remains an error and is
                    # repaired by an ordinary subsequent cache validation.
                    blob, record = package_assets.encode_image(self.image, "data/gfx/frame0.png", cache, False)
                    self.assertEqual(hashlib.sha256(blob.read_bytes()).hexdigest(), record["sha256"])

    def test_shared_cache_publication_retries_while_a_racing_writer_holds_the_entry(self):
        import hashlib
        import os

        replace = os.replace
        denials = {"image.webp": 3}

        def writer_still_replacing(source, destination):
            destination = Path(destination)
            if denials.get(destination.name, 0) > 0:
                # Another exporter is mid-replacement: the entry is neither
                # replaceable nor readable yet.
                denials[destination.name] -= 1
                raise PermissionError("Concurrent cache writer")
            return replace(source, destination)

        with patch("tools.package_assets.os.replace", side_effect=writer_still_replacing), \
                patch("tools.package_assets.time.sleep") as sleep:
            blob, record = package_assets.encode_image(self.image, "data/gfx/frame0.png", self.cache, False)
        self.assertEqual(denials["image.webp"], 0)
        self.assertEqual(sleep.call_count, 3)
        self.assertEqual(hashlib.sha256(blob.read_bytes()).hexdigest(), record["sha256"])
        self.assertTrue(all(p.name in ("image.webp", "selection.json") for p in blob.parent.iterdir()))

    def test_source_change_invalidates_cache(self):
        first = self.export()
        Image.new("RGBA", (64, 64), (60, 80, 130, 255)).save(self.image)
        second = self.export()
        self.assertNotEqual(first["files"], second["files"])

    def test_filters_metadata_but_preserves_linux_store_screenshot(self):
        self.assertFalse(include_asset("data/highres/v1/manifest.json"))
        self.assertFalse(include_asset("data/fonts/build_chinese_font.py"))
        self.assertTrue(include_asset("data/highres/v1/frames.txt"))
        self.assertFalse(include_asset("data/screenshots/game.png", "macos"))
        self.assertTrue(include_asset("data/screenshots/game.png", "linux"))
        self.assertTrue(include_asset("data/fonts/README.md"))

    def test_export_omits_baked_unit_clips_and_keeps_fitted_and_static_meshes(self):
        skins = self.root / "content/data/skins/colony-v1"
        skins.mkdir(parents=True)
        baked = {"worker-prone-harvest.gsk", "warrior-fight.gsk", "explorer-fly.gsk"}
        shipped = {"worker-prone-harvest.gsb", "warrior-fight.gsb", "explorer-fly.gsr", "swarm.gsk"}
        for name in baked | shipped:
            (skins / name).write_bytes(name.encode())

        self.export(lossy=False)
        self.assertEqual({p.name for p in (self.output / "data/skins/colony-v1").iterdir()}, shipped)
        self.assertEqual({p.name for p in skins.iterdir()}, baked | shipped)

    def test_refuses_source_and_unowned_output(self):
        with self.assertRaises(ValueError):
            export_assets(self.root, self.root / "content/data")
        self.output.mkdir()
        (self.output / "precious").write_text("retain")
        with self.assertRaises(ValueError):
            self.export()
        self.assertEqual((self.output / "precious").read_text(), "retain")

    def test_old_export_upgrade_format_changes_and_unrelated_content(self):
        import json, random
        # Old ownership format, with no global image recipe, remains upgradeable.
        first = self.export(optimized=False)
        first.pop("image_recipe")
        first["lossy_background"] = first.pop("lossy_images")
        self.output.with_suffix(".json").write_text(json.dumps(first))
        (self.output / "user.txt").write_text("retain")
        Image.frombytes("RGB", (64, 64), random.Random(99).randbytes(64*64*3)).save(self.image)
        second = self.export()
        record = next(x for x in second["files"] if x["source"].endswith(".png"))
        self.assertEqual(record["output"], "data/gfx/frame0.webp")
        self.assertFalse((self.output / "data/gfx/frame0.png").exists())
        self.assertEqual((self.output / "user.txt").read_text(), "retain")
        self.export(optimized=False)
        self.assertFalse((self.output / "data/gfx/frame0.webp").exists())
        self.assertTrue((self.output / "data/gfx/frame0.png").exists())

    def test_corrupt_source_keeps_previous_export(self):
        self.export()
        old = (self.output.with_suffix(".json")).read_bytes()
        self.image.write_bytes(b"corrupt image")
        with self.assertRaises(Exception):
            self.export()
        self.assertEqual((self.output.with_suffix(".json")).read_bytes(), old)

    def test_failed_audit_replacement_rolls_back_export(self):
        self.export()
        previous = {
            p.relative_to(self.output): p.read_bytes()
            for p in self.output.rglob("*")
            if p.is_file()
        }
        marker = self.output.with_suffix(".json")
        old_audit = marker.read_bytes()
        self.image.rename(self.image.with_name("renamed.png"))
        import os

        replace = os.replace

        def fail_audit(source, destination):
            if Path(destination).resolve() == marker.resolve():
                raise OSError("Simulated audit replacement failure")
            return replace(source, destination)

        with patch("tools.package_assets.os.replace", side_effect=fail_audit):
            with self.assertRaises(OSError):
                self.export()
        self.assertEqual(marker.read_bytes(), old_audit)
        self.assertEqual(
            {
                p.relative_to(self.output): p.read_bytes()
                for p in self.output.rglob("*")
                if p.is_file()
            },
            previous,
        )
        self.assertFalse(self.output.with_name(self.output.name + "-previous").exists())

    def test_original_profile_needs_no_conversion(self):
        self.export(optimized=False)
        self.assertEqual(
            (self.output / "data/gfx/frame0.png").read_bytes(), self.image.read_bytes()
        )

    def test_high_depth_png_is_not_quantized_to_webp(self):
        im = Image.new("I;16", (32, 32))
        im.putpixel((0, 0), 257)
        im.save(self.image)
        original = self.image.read_bytes()
        with self.assertRaisesRegex(ValueError, "Unsupported higher-depth"):
            self.export()
        self.assertEqual(self.image.read_bytes(), original)

    def test_linux_screenshots_keep_their_png_representation(self):
        folder = self.root / "content/data/screenshots"
        folder.mkdir()
        screenshot = folder / "game.png"
        screenshot.write_bytes(self.image.read_bytes())
        self.export(platform="linux")
        self.assertEqual(
            (self.output / "data/screenshots/game.png").read_bytes(),
            screenshot.read_bytes(),
        )

    def test_global_q90_policy_preserves_alpha_and_sources(self):
        import random
        for name, mode in (("frame0", "RGBA"), ("menu-colony", "RGB"),
                           ("loading-wordmark", "RGBA"), ("gamegui0", "RGBA")):
            path = self.image.parent / (name + ".png")
            Image.frombytes(mode, (64, 64), random.Random(99).randbytes(64 * 64 * len(mode))).save(path)
        originals = {p: p.read_bytes() for p in self.image.parent.glob("*.png")}
        audit = self.export()
        self.assertTrue(audit["lossy_images"])
        self.assertEqual(audit["image_recipe"], package_assets.image_recipe())
        for record in audit["files"]:
            if not record["source"].endswith(".png"):
                continue
            self.assertTrue(record["lossy"])
            self.assertEqual(record["recipe"]["lossy_quality"], 90)
            self.assertEqual(record["recipe"]["lossy_method"], 6)
            original = Image.open(self.root / "content" / record["source"]).convert("RGBA")
            decoded = Image.open(self.output / record["output"]).convert("RGBA")
            self.assertEqual(original.size, decoded.size)
            self.assertEqual(original.getchannel("A").tobytes(), decoded.getchannel("A").tobytes())
        for p, data in originals.items():
            self.assertEqual(p.read_bytes(), data)

    def test_edition_quality_applies_to_its_own_export_only(self):
        import random
        Image.frombytes("RGB", (64, 64), random.Random(5).randbytes(64 * 64 * 3)).save(
            self.image.parent / "menu-colony.png")
        lite = self.export(lossy_quality=70)
        self.assertEqual(lite["image_recipe"]["lossy_quality"], 70)
        record = next(r for r in lite["files"] if r["source"] == "data/gfx/menu-colony.png")
        self.assertTrue(record["lossy"])
        self.assertEqual(record["recipe"]["lossy_quality"], 70)
        # Nothing process-wide changes: browser copy provenance (platforms/web/
        # derive_assets.py) and a later standard export still see the standard quality.
        self.assertEqual(package_assets.image_recipe()["lossy_quality"], package_assets.DEFAULT_LOSSY_QUALITY)
        self.assertNotIn("GLOB2_WEBP_LOSSY_QUALITY", os.environ)
        standard = self.export()
        record = next(r for r in standard["files"] if r["source"] == "data/gfx/menu-colony.png")
        self.assertEqual(record["recipe"]["lossy_quality"], package_assets.DEFAULT_LOSSY_QUALITY)

    def test_group_quality_never_exceeds_its_ceiling(self):
        self.assertEqual(package_assets.group_lossy_quality(), 75)
        self.assertEqual(package_assets.group_lossy_quality(70), 70)
        self.assertEqual(package_assets.group_lossy_quality(90), 75)

    def test_terrain_stone_maps_stay_exact_under_lossy_policy(self):
        import random
        stones = self.root / "content/data/terrain/stones"
        stones.mkdir(parents=True)
        # Noise makes lossy WebP smaller, so only the data-image rule keeps it exact.
        source = Image.frombytes("RGBA", (64, 64), random.Random(7).randbytes(64 * 64 * 4))
        source.save(stones / "brick_path.png")
        audit = self.export()
        self.assertTrue(audit["lossy_images"])
        record = next(r for r in audit["files"] if r["source"] == "data/terrain/stones/brick_path.png")
        self.assertFalse(record["lossy"])
        decoded = Image.open(self.output / record["output"]).convert("RGBA")
        self.assertEqual(decoded.tobytes(), source.tobytes())

    def test_png_cannot_win_even_when_smaller_and_hd_index_uses_webp(self):
        # This artwork's optimized PNG is smaller than either WebP encoding.
        source = self.root / "content/data/gfx/gamegui47.png"
        actual = package_assets.ROOT / "content/data/gfx/gamegui47.png"
        source.write_bytes(actual.read_bytes())
        hd = self.root / "content/data/highres/v1"
        hd.mkdir(parents=True)
        text = "glob2-highres 1\nframe0 64 64 4 base.png team.png\n"
        (hd / "frames.txt").write_text(text)
        audit = self.export()
        self.assertTrue(all(r["output"].endswith(".webp") for r in audit["files"]
                            if r["source"].endswith(".png")))
        self.assertEqual((hd / "frames.txt").read_text(), text)
        self.assertEqual((self.output / "data/highres/v1/frames.txt").read_text(),
                         text.replace(".png", ".webp"))

    def test_source_hd_atlas_placement_is_verified_before_encoding(self):
        directory = self.root / "content/data/highres/v1"
        directory.mkdir(parents=True)
        frame = Image.new("RGBA", (8, 8), (37, 53, 83, 177))
        frame.save(directory / "terrain0.png")
        (directory / "frames.txt").write_text("GLOB2_HIGHRES 1\nterrain0 2 2 4 terrain0.png -\n")
        atlas = Image.new("RGBA", (256, 256))
        atlas.paste(frame, (64, 64))
        atlas.save(directory / "terrain-atlas-mip0.png")
        package_assets.verify_highres_atlases(self.root)
        atlas.putpixel((64, 64), (99, 99, 99, 177))
        atlas.save(directory / "terrain-atlas-mip0.png")
        with self.assertRaisesRegex(ValueError, "Source atlas frame placement"):
            self.export()

    def test_smaller_lossless_candidate_wins(self):
        record = next(x for x in self.export()["files"] if x["source"].endswith(".png"))
        self.assertFalse(record["lossy"])

    def test_q90_encoder_parameters(self):
        calls = []
        save = Image.Image.save
        def capture(image, fp, format=None, **kwargs):
            if format == "WEBP":
                calls.append(kwargs)
            return save(image, fp, format, **kwargs)
        with patch.object(Image.Image, "save", capture):
            self.export()
        self.assertTrue(any(c.get("lossless") is False and c.get("quality") == 90
                            and c.get("method") == 6 and c.get("exact") is True for c in calls))

    def test_recipe_change_invalidates_cache(self):
        self.export()
        count = len(list(self.cache.glob("*/selection.json")))
        with patch.object(package_assets, "IMAGE_RECIPE", "future-recipe"):
            audit = self.export()
        self.assertGreater(len(list(self.cache.glob("*/selection.json"))), count)
        self.assertEqual(audit["image_recipe"]["image_recipe"], "future-recipe")

    def test_rgba16_png_filters_and_adam7_reference(self):
        import random, struct, zlib
        width, height = 13, 11
        samples = [random.Random(91 + i).randrange(65536) for i in range(width * height * 4)]
        expected = bytes((v + 128) // 257 for v in samples)
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
        for interlace in (0, 1):
            passes = ((0, 0, 1, 1),) if not interlace else (
                (0,0,8,8), (4,0,8,8), (0,4,4,8), (2,0,4,4), (0,2,2,4), (1,0,2,2), (0,1,1,2))
            scanlines = bytearray()
            for sx, sy, dx, dy in passes:
                previous = bytes(len(range(sx, width, dx)) * 8)
                for row_index, y in enumerate(range(sy, height, dy)):
                    values = [v for x in range(sx, width, dx) for v in samples[(y*width+x)*4:(y*width+x+1)*4]]
                    row = struct.pack(">" + "H" * len(values), *values)
                    filter_type = row_index % 5
                    scanlines.append(filter_type)
                    for i, value in enumerate(row):
                        a, b, c = row[i-8] if i >= 8 else 0, previous[i], previous[i-8] if i >= 8 else 0
                        p = a + b - c
                        paeth = min((a, b, c), key=lambda v: abs(p-v))
                        prediction = (0, a, b, (a+b)//2, paeth)[filter_type]
                        scanlines.append((value - prediction) % 256)
                    previous = row
            raw = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 6, 0, 0, interlace))
            raw += chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b"")
            self.assertEqual(package_assets.decoded_rgba(raw).tobytes(), expected)

    def test_high_depth_rgba_uses_renderer_reference(self):
        import random, struct, zlib
        width = height = 64
        # Construct 16-bit RGBA without relying on Pillow's 8-bit-only writer.
        rgba = random.Random(42).randbytes(width * height * 4)
        rows = b"".join(b"\0" + b"".join(bytes((v, 123)) for v in rgba[y*width*4:(y+1)*width*4])
                        for y in range(height))
        def chunk(tag, data):
            return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
        self.image.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 6, 0, 0, 0))
                              + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
        original = self.image.read_bytes()
        record = next(x for x in self.export()["files"] if x["source"].endswith(".png"))
        self.assertTrue(record["lossy"])
        self.assertEqual(record["recipe"]["depth_action"], "renderer-rgba8-round-nearest-div257")
        decoded = Image.open(self.output / record["output"]).convert("RGBA")
        expected = bytes(((v * 256 + 123 + 128) // 257) for v in rgba)
        self.assertEqual(decoded.getchannel("A").tobytes(), expected[3::4])
        self.assertNotEqual(expected[3::4], rgba[3::4])
        self.assertEqual(self.image.read_bytes(), original)
        audit = self.export(lossy=False)
        record = next(x for x in audit["files"] if x["source"].endswith(".png"))
        self.assertFalse(record["lossy"])
        self.assertTrue(record["output"].endswith(".webp"))
        self.assertEqual(Image.open(self.output / record["output"]).convert("RGBA").tobytes(), expected)
        self.assertEqual(self.image.read_bytes(), original)



class ShadowLayerSheetTests(unittest.TestCase):
    """A frame's shadow layer (<frame>s.png) packs into its family's sheets as
    `shadow` runs, with exact alpha, and never adds or fills frames."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.gfx = self.root / "content/data/gfx"
        self.gfx.mkdir(parents=True)
        self.output = Path(self.temp.name) / "output"
        self.cache = Path(self.temp.name) / "cache"
        self.frames = {}
        # Three 3x3 frames; frames 0 and 1 cast 5x4 shadows, frame 2 none.
        for index in range(3):
            pixels = bytes((index * 29 + p) % 256 for p in range(3 * 3 * 4))
            Image.frombytes("RGBA", (3, 3), pixels).save(self.gfx / f"tree{index}.png")
            self.frames[f"tree{index}.png"] = pixels
        for index in range(2):
            pixels = b"".join(bytes((0, 0, 0, (index * 53 + p * 17) % 256)) for p in range(5 * 4))
            Image.frombytes("RGBA", (5, 4), pixels).save(self.gfx / f"tree{index}s.png")
            self.frames[f"tree{index}s.png"] = pixels

    def unpack(self):
        layers = {"image": "", "rotated": "r", "shadow": "s"}
        frames, kinds = {}, []
        for line in (self.output / "data/gfx/tree.sheet").read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            name, layer, first, count, width, height = line.split()
            kinds.append(layer)
            first, count, width, height = int(first), int(count), int(width), int(height)
            with Image.open(next((self.output / "data/gfx").glob(Path(name).stem + ".*"))) as image:
                image = image.convert("RGBA")
                columns = image.width // width
                for i in range(count):
                    left, top = (i % columns) * width, (i // columns) * height
                    frames[f"tree{first + i}{layers[layer]}.png"] = image.crop(
                        (left, top, left + width, top + height)).tobytes()
        return frames, kinds

    def test_shadows_pack_as_shadow_runs_exactly(self):
        export_assets(self.root, self.output, cache=self.cache, jobs=1, lossy=False)
        frames, kinds = self.unpack()
        self.assertEqual(frames, self.frames)
        self.assertEqual(kinds, ["image", "shadow"])
        self.assertFalse(any(p.name.startswith("tree") and p.suffix == ".webp" and "sheet" not in p.name
                             for p in (self.output / "data/gfx").iterdir()))

    def test_lossy_sheets_keep_shadow_alpha_exact(self):
        export_assets(self.root, self.output, cache=self.cache, jobs=1, lossy=True)
        frames, _ = self.unpack()
        for name in ("tree0s.png", "tree1s.png"):
            self.assertEqual(frames[name][3::4], self.frames[name][3::4])

    def test_shadows_neither_make_nor_fill_a_family(self):
        files = sorted(self.gfx.iterdir())
        self.assertIn("data/gfx/tree", package_assets.sprite_families(files, self.root / "content"))
        # A shadow of a frame the family does not have is an authoring error.
        Image.new("RGBA", (2, 2)).save(self.gfx / "tree3s.png")
        with self.assertRaises(ValueError):
            package_assets.sheet_plan(sorted(self.gfx.iterdir()), self.root / "content")
        (self.gfx / "tree3s.png").unlink()
        # Two frames and their shadows are still too few files for a sheet.
        (self.gfx / "tree2.png").unlink()
        self.assertNotIn("data/gfx/tree", package_assets.sprite_families(sorted(self.gfx.iterdir()),
                                                                          self.root / "content"))


class SpriteSheetExportTests(unittest.TestCase):
    """Optimized exports pack data/gfx/unit's frames into sheets for Sprite::load."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.gfx = self.root / "content/data/gfx"
        self.gfx.mkdir(parents=True)
        self.output = Path(self.temp.name) / "output"
        self.cache = Path(self.temp.name) / "cache"
        self.frames = {}
        # Frames 0-4 recolorable at 3x3 and 5-6 at 4x4; shadows on 2-3 only.
        for index in range(7):
            for suffix in ["r"] + ([""] if index in (2, 3) else []):
                size = 3 if index < 5 else 4
                pixels = bytes((index * 37 + len(suffix) * 11 + p) % 256 for p in range(size * size * 4))
                name = "unit%d%s.png" % (index, suffix)
                Image.frombytes("RGBA", (size, size), pixels).save(self.gfx / name)
                self.frames[name] = pixels
        Image.new("RGBA", (5, 5)).save(self.gfx / "unitmini0.png")

    def export(self, **kwargs):
        kwargs.setdefault("jobs", 1)
        return export_assets(self.root, self.output, cache=self.cache, **kwargs)

    def unpack(self):
        """{frame name: RGBA bytes} cut out of the exported sheets like Sprite::load."""
        frames = {}
        index = (self.output / "data/gfx/unit.sheet").read_text()
        for line in index.splitlines():
            if not line or line.startswith("#"):
                continue
            name, layer, first, count, width, height = line.split()
            first, count, width, height = int(first), int(count), int(width), int(height)
            sheet = next((self.output / "data/gfx").glob(Path(name).stem + ".*"))
            with Image.open(sheet) as image:
                image = image.convert("RGBA")
                self.assertEqual(image.width % width, 0)
                columns = image.width // width
                for i in range(count):
                    left, top = (i % columns) * width, (i // columns) * height
                    tile = image.crop((left, top, left + width, top + height))
                    key = "unit%d%s.png" % (first + i, "r" if layer == "rotated" else "")
                    self.assertNotIn(key, frames)
                    frames[key] = tile.tobytes()
        return frames

    def test_parallel_sheets_match_serial_bytes_and_audit(self):
        for lossy in (True, False):
            with self.subTest(lossy=lossy):
                serial = export_assets(self.root, self.output, cache=self.cache / f"serial-{lossy}",
                                       jobs=1, lossy=lossy)
                before = {p.relative_to(self.output): p.read_bytes()
                          for p in self.output.rglob("*") if p.is_file()}
                parallel = export_assets(self.root, self.output, cache=self.cache / f"parallel-{lossy}",
                                         jobs=4, lossy=lossy)
                self.assertEqual(serial, parallel)
                self.assertEqual(before, {p.relative_to(self.output): p.read_bytes()
                                          for p in self.output.rglob("*") if p.is_file()})

    def test_sheets_hold_every_frame_exactly_and_replace_the_frames(self):
        audit = self.export(lossy=False)
        self.assertEqual(self.unpack(), self.frames)
        shipped = {p.name for p in (self.output / "data/gfx").iterdir()}
        self.assertFalse(any(name.startswith("unit") and name[4].isdigit() for name in shipped))
        self.assertTrue(any(name.startswith("unitmini0.") for name in shipped))
        packed = [item for item in audit["files"] if "packed_from" in item]
        self.assertEqual(
            sorted(f["source"] for item in packed for f in item["packed_from"]),
            sorted("data/gfx/" + name for name in self.frames),
        )
        self.assertEqual(audit["source_bytes"], sum(p.stat().st_size for p in self.gfx.iterdir()))

    def test_version_two_webp_groups_keep_variable_sizes_and_frame_layers(self):
        audit = self.export(packed_hd_sprites=True, lossy=False)
        header, *lines = (self.output / "data/gfx/unit.sheet").read_text().splitlines()
        self.assertEqual(header, "GLOB2_SPRITES 2")
        unpacked = {}
        for line in lines:
            token, layer, index = line.split()
            page, *geometry = token[1:].split(",")
            x, y, width, height = map(int, geometry)
            key = f"unit{index}{'r' if layer == 'rotated' else ''}.png"
            with Image.open(self.output / "data/gfx" / page) as image:
                unpacked[key] = image.convert("RGBA").crop((x, y, x + width, y + height)).tobytes()
        self.assertEqual(unpacked, self.frames)
        self.assertEqual(set(audit["native_sprite_selections"]),
                         {"unit-movement-0-team", "unit-movement-0-base"})
        self.assertTrue(all(entry["winner"] == "lossless"
                            for entry in audit["native_sprite_selections"].values()))
        warm = self.export(packed_hd_sprites=True, lossy=False)
        self.assertTrue(all(entry["cache_hit"] for entry in warm["native_sprite_selections"].values()))
        self.assertEqual(audit["files"], warm["files"])

    def test_runs_split_by_layer_size_and_sheet_capacity(self):
        with patch.object(package_assets, "SHEET_FRAMES", 2):
            self.export(lossy=False)
            self.assertEqual(self.unpack(), self.frames)
        lines = [
            line.split()[1:]
            for line in (self.output / "data/gfx/unit.sheet").read_text().splitlines()
            if line and not line.startswith("#")
        ]
        self.assertEqual(
            lines,
            [
                ["image", "2", "2", "3", "3"],
                ["rotated", "0", "2", "3", "3"],
                ["rotated", "2", "2", "3", "3"],
                ["rotated", "4", "1", "3", "3"],
                ["rotated", "5", "2", "4", "4"],
            ],
        )

    def test_cached_export_is_repeatable(self):
        first = self.export(lossy=False)
        self.assertEqual(first, self.export(lossy=False))
        Image.new("RGBA", (3, 3), (1, 2, 3, 4)).save(self.gfx / "unit1r.png")
        self.frames["unit1r.png"] = bytes((1, 2, 3, 4)) * 9
        self.assertNotEqual(first, self.export(lossy=False))
        self.assertEqual(self.unpack(), self.frames)

    def test_default_sheet_alpha_and_corrupt_packed_cache(self):
        first = self.export()
        for name, pixels in self.unpack().items():
            self.assertEqual(pixels[3::4], self.frames[name][3::4])
        for packed in self.cache.glob("sheet-*/*.png"):
            packed.write_bytes(b"corrupt")
        self.assertEqual(first, self.export())

    def test_original_profile_keeps_the_frames(self):
        self.export(optimized=False)
        self.assertFalse((self.output / "data/gfx/unit.sheet").exists())
        self.assertEqual((self.output / "data/gfx/unit5r.png").read_bytes(), (self.gfx / "unit5r.png").read_bytes())

    def test_a_family_with_a_gap_stays_as_frames(self):
        # Sprite::load stops at the gap; a sheet would make unit8r appear.
        Image.new("RGBA", (4, 4)).save(self.gfx / "unit8r.png")
        self.export(lossy=False)
        self.assertFalse((self.output / "data/gfx/unit.sheet").exists())
        self.assertTrue((self.output / "data/gfx/unit8r.webp").exists())
        with self.assertRaises(ValueError):
            package_assets.sheet_plan(sorted(self.gfx.iterdir()), self.root / "content",
                                      extra_families={"data/gfx/unit"})

    def test_every_sprite_family_packs_and_short_runs_stay_narrow(self):
        cursor = self.gfx / "cursor"
        cursor.mkdir()
        frames = {}
        for index in range(3):
            pixels = bytes((index * 41 + p) % 256 for p in range(5 * 6 * 4))
            Image.frombytes("RGBA", (5, 6), pixels).save(cursor / f"click{index}r.png")
            frames[index] = pixels
        # Too few frame files for a sheet: a family of two, and one with a single frame.
        Image.new("RGBA", (2, 2)).save(self.gfx / "cloud0.png")
        Image.new("RGBA", (2, 2)).save(self.gfx / "pair0.png")
        Image.new("RGBA", (2, 2)).save(self.gfx / "pair1.png")
        self.export(lossy=False)
        (line,) = [line.split() for line in (self.output / "data/gfx/cursor/click.sheet")
                   .read_text().splitlines() if line and not line.startswith("#")]
        self.assertEqual(line[1:], ["rotated", "0", "3", "5", "6"])
        with Image.open(self.output / "data/gfx/cursor" / Path(line[0]).name) as image:
            self.assertEqual(image.size, (15, 6))  # three columns, not SHEET_COLUMNS
            image = image.convert("RGBA")
            self.assertEqual({i: image.crop((5 * i, 0, 5 * i + 5, 6)).tobytes() for i in frames}, frames)
        shipped = {p.name for p in (self.output / "data/gfx").iterdir()}
        self.assertTrue({"cloud0.webp", "pair0.webp", "pair1.webp"} <= shipped)
        self.assertFalse(any(name.startswith("click") for name in shipped))

    def test_rgba64_frames_round_like_standalone_images(self):
        import struct, zlib
        def png(samples, width, height):
            def chunk(tag, data):
                return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
            rows = b"".join(b"\0" + struct.pack(">%dH" % (width * 4), *samples[y * width * 4:(y + 1) * width * 4])
                            for y in range(height))
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, 6, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
        expected = {}
        for index in range(3):
            samples = [(index * 9001 + p * 257 + 128) % 65536 for p in range(2 * 2 * 4)]
            (self.gfx / f"pool0b{index}.png").write_bytes(png(samples, 2, 2))
            expected[index] = bytes((v + 128) // 257 for v in samples)
        self.export(lossy=False)
        (line,) = [line.split() for line in (self.output / "data/gfx/pool0b.sheet").read_text().splitlines()
                   if line and not line.startswith("#")]
        with Image.open(self.output / "data/gfx" / line[0]) as image:
            image = image.convert("RGBA")
            self.assertEqual({i: image.crop((2 * i, 0, 2 * i + 2, 2)).tobytes() for i in expected}, expected)

    def test_every_optimized_export_packs_terrain_decor(self):
        # Native and standard web exports pass no edition sheet families; decor still packs.
        decor = {}
        for index in range(5):
            pixels = bytes((index * 53 + p) % 256 for p in range(2 * 2 * 4))
            Image.frombytes("RGBA", (2, 2), pixels).save(self.gfx / f"terrain-decor{index}.png")
            decor[index] = pixels
        for options in (dict(lossy=False), dict(lossy=False, platform="macos", packed_hd_sprites=True)):
            with self.subTest(**options):
                audit = self.export(**options)
                shipped = {p.name for p in (self.output / "data/gfx").iterdir()}
                self.assertFalse(any(re.fullmatch(r"terrain-decor\d+\.\w+", name) for name in shipped))
                lines = [line.split() for line in (self.output / "data/gfx/terrain-decor.sheet").read_text().splitlines()
                         if line and not line.startswith("#")]
                self.assertEqual([line[1:] for line in lines], [["image", "0", "5", "2", "2"]])
                with Image.open(next((self.output / "data/gfx").glob(Path(lines[0][0]).stem + ".*"))) as image:
                    image = image.convert("RGBA")
                    self.assertEqual({i: image.crop((2 * i, 0, 2 * i + 2, 2)).tobytes() for i in decor}, decor)
                packed = {f["source"] for item in audit["files"] for f in item.get("packed_from", ())}
                self.assertTrue({f"data/gfx/terrain-decor{i}.png" for i in decor} <= packed)

    def test_exports_keep_only_the_legacy_terrain_the_menus_draw(self):
        hd = self.root / "content/data/highres/v1"
        hd.mkdir(parents=True)
        rows = ["GLOB2_HIGHRES 1"]
        for index in (0, 143, 144, 300):
            Image.new("RGBA", (2, 2), (index % 256, 1, 2, 255)).save(self.gfx / f"terrain{index}.png")
            Image.new("RGBA", (8, 8), (index % 256, 1, 2, 255)).save(hd / f"terrain{index}.png")
            rows.append(f"terrain{index} 2 2 4 terrain{index}.png -")
        self.assertFalse(include_asset("data/highres/v1/terrain-atlas-mip0.png"))
        self.assertTrue(include_asset("data/highres/v1/terrain-lava0.png"))
        (hd / "frames.txt").write_text("\n".join(rows) + "\n")
        for options in (dict(lossy=False), dict(lossy=False, platform="macos", packed_hd_sprites=True)):
            with self.subTest(**options):
                audit = self.export(**options)
                sources = {item["source"] for item in audit["files"]} | {
                    f["source"] for item in audit["files"] for f in item.get("packed_from", ())}
                native = {s for s in sources if s.startswith("data/gfx/terrain")}
                self.assertEqual(native, {"data/gfx/terrain0.png", "data/gfx/terrain143.png"})
                if not options.get("packed_hd_sprites"):  # packed HD frames are recorded by group
                    self.assertTrue({"data/highres/v1/terrain0.png", "data/highres/v1/terrain143.png"} <= sources)
                self.assertFalse(any(re.search(r"terrain(144|300)\b", s) for s in sources))
                self.assertFalse(any("terrain144" in p.name or "terrain300" in p.name
                                     for p in self.output.rglob("*")))
                index = (self.output / "data/highres/v1/frames.txt").read_text()
                self.assertEqual([line.split()[0] for line in index.splitlines()[1:]], ["terrain0", "terrain143"])

    def test_hd_terrain_decor_packs_into_webp_page_groups(self):
        from tools import packed_sprite_assets
        hd = self.root / "content/data/highres/v1"
        hd.mkdir(parents=True)
        decor, rows = {}, ["GLOB2_HIGHRES 1"]
        for index in range(5):
            size = 8 if index < 3 else 4  # a size change starts a new page run
            pixels = bytes((index * 29 + p) % 256 for p in range(size * size * 4))
            Image.frombytes("RGBA", (size, size), pixels).save(hd / f"terrain-decor{index}.png")
            decor[f"terrain-decor{index}"] = pixels
            rows.append(f"terrain-decor{index} {size // 4} {size // 4} 4 terrain-decor{index}.png -")
        (hd / "frames.txt").write_text("\n".join(rows) + "\n")
        # 8x8 frames are 256 decoded bytes: frames 0-1 fill the first group, 2-4 the second.
        with patch.object(packed_sprite_assets, "GROUP_PIXEL_BYTES", 512):
            audit = self.export(lossy=False, platform="macos", packed_hd_sprites=True)
        self.assertEqual(set(audit["hd_sprite_selections"]), {"terrain-decor-0-base", "terrain-decor-1-base"})
        shipped = {p.name for p in (self.output / "data/highres/v1").iterdir()}
        self.assertFalse(any(re.fullmatch(r"terrain-decor\d+\.\w+", name) for name in shipped))
        header, *lines = (self.output / "data/highres/v1/frames.txt").read_text().splitlines()
        self.assertEqual(header, "GLOB2_HIGHRES 2")
        unpacked = {}
        for line in lines:
            ident, width, height, scale, base, team = line.split()
            self.assertEqual(team, "-")
            page, *geometry = base[1:].split(",")
            self.assertTrue(page.startswith("pack-terrain-decor-"))
            x, y, w, h = map(int, geometry)
            with Image.open(self.output / "data/highres/v1" / page) as image:
                unpacked[ident] = image.convert("RGBA").crop((x, y, x + w, y + h)).tobytes()
        self.assertEqual(unpacked, decor)

    def test_other_hd_frames_pool_into_webp_page_groups_by_size(self):
        hd = self.root / "content/data/highres/v1"
        hd.mkdir(parents=True)
        expected, rows = {}, ["GLOB2_HIGHRES 1"]
        for ident, size, team in (("inn0b0", 8, True), ("mapicon0", 4, False), ("inn0b1", 8, False),
                                  ("mapicon1", 4, False), ("cloud", 4, False)):
            layers = []
            for suffix in ("", "r") if team else ("",):
                pixels = bytes((len(ident) * 31 + len(suffix) * 7 + p) % 256 for p in range(size * size * 4))
                Image.frombytes("RGBA", (size, size), pixels).save(hd / f"{ident}{suffix}.png")
                expected[ident, suffix] = pixels
                layers.append(f"{ident}{suffix}.png")
            rows.append(f"{ident} {size // 4} {size // 4} 4 {layers[0]} {layers[1] if team else '-'}")
        (hd / "frames.txt").write_text("\n".join(rows) + "\n")
        audit = self.export(lossy=False, platform="macos", packed_hd_sprites=True)
        self.assertEqual(set(audit["hd_sprite_selections"]), {"sprites-0-base", "sprites-0-team"})
        self.assertFalse(any(p.suffix == ".webp" and not p.name.startswith("pack-")
                             for p in (self.output / "data/highres/v1").iterdir()))
        unpacked = {}
        for line in (self.output / "data/highres/v1/frames.txt").read_text().splitlines()[1:]:
            ident, _, _, _, base, team = line.split()
            for suffix, token in (("", base), ("r", team)):
                if token == "-":
                    continue
                page, *geometry = token[1:].split(",")
                x, y, w, h = map(int, geometry)
                with Image.open(self.output / "data/highres/v1" / page) as image:
                    unpacked[ident, suffix] = image.convert("RGBA").crop((x, y, x + w, y + h)).tobytes()
        self.assertEqual(unpacked, expected)




def _x264_ffmpeg():
    import shutil, subprocess
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return None
    encoders = subprocess.run([ffmpeg, "-hide_banner", "-encoders"], capture_output=True, text=True).stdout
    return ffmpeg if "libx264rgb" in encoders else None


class VerifiedSpritePackTests(unittest.TestCase):
    @unittest.skipUnless(_x264_ffmpeg(), "needs ffmpeg with libx264rgb")
    def test_video_alpha_is_one_block_per_chunk(self):
        import zlib
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            frames = []
            for index in range(8):  # one unit direction: a chunk of eight
                image = Image.new("RGBA", (16, 16), (30 * index, 90, 200, 255))
                image.putpixel((3, 4), (10, 20, 30, 50 + index))  # partial alpha in every frame
                frames.append((index, image))
            result = packed.write_video(frames, (16, 16), "clip", dict(material=None, crf=20),
                                        _x264_ffmpeg(), Path(tmp), None)
            self.assertNotIn("rejected", result)
            alpha = [name for name in result["files"] if not name.endswith(".h264")]
            self.assertEqual(alpha, ["pack-clip-0000.za"])
            planes = zlib.decompress((Path(tmp) / alpha[0]).read_bytes())
            self.assertEqual(len(planes), 8 * 16 * 16)
            for index, image in frames:
                self.assertEqual(planes[index * 256:(index + 1) * 256], image.getchannel("A").tobytes())
                self.assertIn(",pack-clip-0000.za,16,16,8,", result["refs"][index])

    def test_video_group_exceeding_runtime_memory_is_rejected_before_encoding(self):
        from types import SimpleNamespace
        from tools import packed_sprite_assets as packed

        pose = SimpleNamespace(width=2048, height=2048)
        group = {"material": None, "crf": 26}
        with patch.object(packed.subprocess, "run", side_effect=AssertionError("oversized encode")):
            result = packed.write_video([(index, pose) for index in range(8)], (2048, 2048),
                                        "oversized", group, "ffmpeg", None, None)
        self.assertEqual(result, {"rejected": "video group exceeds decoded size limit"})

    @staticmethod
    def frame(colour=(43, 106, 215, 127), size=32):
        """A frame with texture, so lossy WebP and a fake video have something to lose."""
        image = Image.new("RGBA", (size, size), colour)
        for x in range(0, size, 2):
            for y in range(size):
                image.putpixel((x, y), (colour[0] // 2, colour[1], 255 - colour[2], colour[3]))
        return image

    def fake_video(self, decoded):
        def encode(frames, size, name, spec, ffmpeg, directory, fingerprint):
            (directory / "tiny.h264").write_bytes(b"1")
            return dict(files=["tiny.h264"], refs={0: "@tiny.h264,-,32,32,1,0,0,0,32,32"},
                        decoded={0: decoded}, fingerprints={})
        return encode

    def test_identical_frames_score_perfectly_with_either_metric(self):
        from tools import packed_sprite_assets as packed

        frames = {0: self.frame(), 1: self.frame((200, 30, 90, 255))}
        metrics = [m for m in ("ssimulacra2", "ssim") if m == "ssim" or packed.quality_metric() == m]
        try:
            import numpy  # noqa: F401
        except ImportError:
            self.skipTest("perceptual metrics need numpy")
        for metric in metrics:
            with self.subTest(metric=metric):
                self.assertEqual(packed.perceptual_score(frames, dict(frames), metric),
                                 100.0 if metric == "ssimulacra2" else 1.0)

    def test_video_that_looks_worse_than_q75_loses_and_warm_cache_never_encodes(self):
        from tools import packed_sprite_assets as packed

        metric = packed.quality_metric()
        if metric is None:
            self.skipTest("perceptual metrics need numpy")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "frame.png"
            self.frame().save(source)
            group = dict(material=None, crf=26, frames=[(0, "unit0", 5, source)])
            # Same alpha, texture smeared away: what the perceptual gate exists to catch.
            smeared = Image.new("RGBA", (32, 32), (65, 106, 128, 127))

            with patch.object(packed, "write_video", side_effect=self.fake_video(smeared)):
                directory, result, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertFalse(cache_hit)
            self.assertEqual(result["quality_metric"], metric)
            self.assertNotEqual(result["winner"], "video")
            video, webp = result["candidates"]["video"], result["candidates"]["q75"]
            self.assertLess(video[metric], webp[metric])
            self.assertEqual(video["rejected"], "below Q75 " + {"ssimulacra2": "SSIMULACRA2", "ssim": "SSIM"}[metric])
            self.assertNotIn("decoded", video)
            self.assertFalse((directory / "tiny.h264").exists())
            with patch.object(packed, "write_webp", side_effect=AssertionError("warm encode")), \
                 patch.object(packed, "write_video", side_effect=AssertionError("warm encode")):
                _, warm, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertTrue(cache_hit)
            self.assertEqual(result["winner"], warm["winner"])
            self.frame((44, 106, 215, 127)).save(source)
            with patch.object(packed, "write_video", side_effect=self.fake_video(smeared)):
                _, _, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertFalse(cache_hit)

    def test_video_that_looks_as_good_as_q75_wins_when_smaller(self):
        from tools import packed_sprite_assets as packed

        if packed.quality_metric() is None:
            self.skipTest("perceptual metrics need numpy")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "frame.png"
            self.frame().save(source)
            group = dict(material=None, crf=26, frames=[(0, "unit0", 5, source)])
            with patch.object(packed, "write_video", side_effect=self.fake_video(self.frame())):
                _, result, _ = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertEqual(result["winner"], "video")
            self.assertNotIn("rejected", result["candidates"]["video"])

    def test_video_without_any_perceptual_metric_is_rejected(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "frame.png"
            self.frame().save(source)
            group = dict(material=None, crf=26, frames=[(0, "unit0", 5, source)])
            with patch.object(packed, "quality_metric", return_value=None), \
                 patch.object(packed, "write_video", side_effect=self.fake_video(self.frame())):
                _, result, _ = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertNotEqual(result["winner"], "video")
            self.assertEqual(result["candidates"]["video"]["rejected"], "no perceptual quality metric available")
            # A later encoder with a metric must not reuse the metric-less selection.
            try:
                import numpy  # noqa: F401
            except ImportError:
                return
            with patch.object(packed, "quality_metric", return_value="ssim"), \
                 patch.object(packed, "write_video", side_effect=self.fake_video(self.frame())):
                _, _, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "generic", ("encoder", "hash"), True)
            self.assertFalse(cache_hit)


class SpriteQualityCurveTests(unittest.TestCase):
    def group(self, root, video=True):
        source = root / "frame.png"
        if not source.exists():
            VerifiedSpritePackTests.frame().save(source)
        return dict(material=None, crf=26 if video else None, frames=[(0, "unit0", 5, source)])

    def curve(self, group, **overrides):
        from tools import packed_sprite_assets as packed

        entry = dict(group=packed.group_identity(group), webp_encoder=packed.webp_identity(),
                     metric=packed.metric_identity(),
                     h264_encoder=packed.video_identity() if group["crf"] is not None else None,
                     lossless=[9000, 8000],
                     # [stored, transfer, score]: WebP is cheaper stored, video cheaper to download.
                     webp={"60": [1000, 900, 55.0], "80": [1500, 1400, 72.0], "95": [3000, 2900, 90.0]},
                     h264={"30": [800, 500, 50.0], "24": [1700, 1000, 74.0], "18": [2500, 1600, 85.0]})
        entry.update(overrides)
        return entry

    def test_selection_takes_the_cheapest_point_meeting_the_target(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            group = self.group(Path(tmp))
            curve = self.curve(group)
            pick = packed.select_point(curve, group, 70, True, True, "macos")
            self.assertEqual((pick["codec"], pick["setting"], pick["predicted_bytes"]), ("webp", 80, 1500))
            # Browsers count transfer bytes, where video is cheaper.
            pick = packed.select_point(curve, group, 70, True, True, "web")
            self.assertEqual((pick["codec"], pick["setting"]), ("h264", 24.0))
            # A higher target moves up the curve; an unreachable one falls back to lossless.
            self.assertEqual(packed.select_point(curve, group, 80, True, True, "macos")["setting"], 18.0)
            self.assertEqual(packed.select_point(curve, group, 99, True, True, "macos")["codec"], "lossless")
            # Lossless builds have only the lossless point.
            self.assertEqual(packed.select_point(curve, group, 50, False, True, "macos")["codec"], "lossless")

    def test_selection_uses_only_the_codecs_a_build_has(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            group = self.group(Path(tmp))
            curve = self.curve(group)
            # No sprite encoder (or a CRF-less group): video points are never chosen.
            pick = packed.select_point(curve, group, 70, True, False, "web")
            self.assertEqual((pick["codec"], pick["setting"]), ("webp", 80))
            # A video curve measured with another encoder keeps WebP usable.
            pick = packed.select_point(self.curve(group, h264_encoder="other"), group, 70, True, True, "web")
            self.assertEqual(pick["codec"], "webp")
            self.assertIn("another encoder", pick["note"])

    def test_stale_or_missing_curves_fall_back(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            group = self.group(Path(tmp))
            self.assertEqual(packed.select_point(None, group, 70, True, True, "macos"), {"fallback": "no curve"})
            self.assertEqual(packed.select_point(self.curve(group, webp_encoder="old"), group, 70, True, True,
                                                 "macos")["fallback"], "encoder or metric changed")
            curve = self.curve(group)
            VerifiedSpritePackTests.frame((44, 106, 215, 127)).save(group["frames"][0][3])
            self.assertEqual(packed.select_point(curve, group, 70, True, True, "macos")["fallback"],
                             "sources changed")

    def test_ties_prefer_lossless_then_webp_then_video(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            group = self.group(Path(tmp))
            curve = self.curve(group, lossless=[1000, 1000], webp={"80": [1000, 1000, 80.0]},
                               h264={"24": [1000, 1000, 80.0]})
            self.assertEqual(packed.select_point(curve, group, 70, True, True, "macos")["codec"], "lossless")
            curve["lossless"] = [2000, 2000]
            self.assertEqual(packed.select_point(curve, group, 70, True, True, "macos")["codec"], "webp")

    def test_curve_choice_encodes_only_the_chosen_point_at_its_setting(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            group = self.group(root)
            calls = []
            real_webp = packed.write_webp

            def webp(*args, **kwargs):
                calls.append((args[2], kwargs.get("setting")))
                return real_webp(*args, **kwargs)

            with patch.object(packed, "write_webp", side_effect=webp), \
                 patch.object(packed, "write_video", side_effect=AssertionError("video not chosen")):
                _, result, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "macos", ("encoder", "hash"), True,
                    curve=self.curve(group), target=70)
            self.assertFalse(cache_hit)
            self.assertEqual(calls, [(packed.images.GROUP_LOSSY_MODE, 80)])
            self.assertEqual(result["winner"], packed.images.GROUP_LOSSY_MODE)
            self.assertEqual((result["curve"]["codec"], result["curve"]["setting"], result["curve"]["target"]),
                             ("webp", 80, 70))
            self.assertIn("stored_bytes", result["candidates"][result["winner"]])
            # A different target is a different pack; the same one is a warm cache hit.
            with patch.object(packed, "write_webp", side_effect=AssertionError("warm encode")):
                _, _, cache_hit = packed.encode_group(
                    "test", group, root / "cache", "macos", ("encoder", "hash"), True,
                    curve=self.curve(group), target=70)
            self.assertTrue(cache_hit)
            _, other, _ = packed.encode_group("test", group, root / "cache", "macos", ("encoder", "hash"), True,
                                              curve=self.curve(group), target=50)
            self.assertEqual(other["curve"]["setting"], 60)

    def test_rejected_video_point_falls_back_to_the_best_point_without_video(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            group = self.group(root)
            with patch.object(packed, "write_video", return_value={"rejected": "video group exceeds decoded size limit"}):
                _, result, _ = packed.encode_group("test", group, root / "cache", "web", ("encoder", "hash"), True,
                                                   curve=self.curve(group), target=70)
            self.assertEqual(result["winner"], packed.images.GROUP_LOSSY_MODE)
            self.assertEqual((result["curve"]["codec"], result["curve"]["setting"]), ("webp", 80))
            self.assertIn("video point rejected", result["curve"]["note"])

    def test_missing_curve_keeps_the_fixed_candidates_and_says_so(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            group = self.group(root, video=False)
            _, result, _ = packed.encode_group("test", group, root / "cache", "macos", None, True, target=70)
            self.assertEqual(result["curve"], {"fallback": "no curve"})
            self.assertEqual(set(result["candidates"]), {"lossless", packed.images.GROUP_LOSSY_MODE})

    def test_edition_groups_find_their_own_curve(self):
        from tools import packed_sprite_assets as packed

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            standard = self.group(root)
            trimmed_source = root / "trimmed.png"
            VerifiedSpritePackTests.frame((10, 20, 30, 255)).save(trimmed_source)
            trimmed = dict(standard, frames=[(0, "unit0", 5, trimmed_source)])
            curves = {"native/test": self.curve(standard), "native/test@lite": self.curve(trimmed)}
            self.assertIs(packed.find_curve(curves, "native", "test", standard), curves["native/test"])
            self.assertIs(packed.find_curve(curves, "native", "test", trimmed), curves["native/test@lite"])
            # A group no curve describes gets the standard one, which selection reports stale.
            other = dict(standard, frames=[(0, "unit0", 4, trimmed_source)])
            self.assertIs(packed.find_curve(curves, "native", "test", other), curves["native/test"])
            self.assertEqual(packed.select_point(packed.find_curve(curves, "native", "test", other), other, 70,
                                                 True, False, "web")["fallback"], "sources changed")

    def test_platform_targets_default_and_override(self):
        with patch.dict(package_assets.SPRITE_TARGETS, {"default": 70, "web": 62}, clear=True):
            self.assertEqual(package_assets.sprite_target_for("macos"), 70)
            self.assertEqual(package_assets.sprite_target_for("web"), 62)

    def test_curve_file_is_current_for_its_recorded_groups(self):
        """Committed curves must parse, use the current format and cover the grids."""
        from tools import packed_sprite_assets as packed

        document = json.loads(packed.CURVE_FILE.read_text())
        self.assertEqual(document["version"], packed.CURVE_VERSION)
        self.assertEqual(document["webp_grid"], list(packed.WEBP_GRID))
        self.assertEqual(document["crf_grid"], [float(c) for c in packed.CRF_GRID])
        for key, entry in document["groups"].items():
            self.assertRegex(key, r"^(native|hd)/")
            self.assertEqual(sorted(entry["webp"], key=int), [str(q) for q in packed.WEBP_GRID])
            if entry["h264_encoder"]:
                self.assertEqual(sorted(entry["h264"], key=float), [packed.crf_key(c) for c in packed.CRF_GRID])


if __name__ == "__main__":
    unittest.main()
