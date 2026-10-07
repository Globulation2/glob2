# SPDX-License-Identifier: GPL-3.0-or-later
"""Image-generator export and validation on a synthetic material in a temporary root.

Run with the pinned asset encoder interpreter:

    "$(python3 tools/package_assets.py --encoder-python)" -m unittest discover -s tools/artwork -p test_export_material.py
"""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

import PIL
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_material  # noqa: E402
import terrain_synth  # noqa: E402
import validate_material  # noqa: E402
from material_tiles import ROOT, pixel_sha256  # noqa: E402


def synthetic_material(name, size=512):
    """A square stand-in for generated art: a procedural render, upscaled."""
    image = terrain_synth.render_phase(name, 0)[1][3]
    return image.resize((size, size), Image.Resampling.BICUBIC)


@unittest.skipUnless(PIL.__version__ == terrain_synth.PILLOW_VERSION, "needs the pinned Pillow")
class ExportMaterial(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for reference in export_material.REFERENCES:
            target = self.root / reference
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(ROOT / reference, target)

    def tearDown(self):
        self.tmp.cleanup()

    def prepare(self, name):
        source_dir = self.root / "datasrc/gfx" / name
        source_dir.mkdir(parents=True)
        synthetic_material(name).save(source_dir / "material.png")
        shutil.copy(ROOT / "datasrc/gfx" / name / "provenance.json", source_dir / "provenance.json")

    def test_export_check_and_validate(self):
        self.prepare("boulders")
        frames, document = export_material.export("boulders", self.root)
        self.assertEqual(len(frames), 16)
        self.assertEqual(document["method"], "image-generator")
        self.assertEqual(document["replaces"]["method"], "procedural")
        self.assertEqual(document["prompt"], export_material.MATERIAL_PROMPTS["boulders"])
        self.assertEqual(len(document["reference_sha256"]), 4)
        for i in range(16):
            path = self.root / f"data/gfx/terrain-boulders{i}.png"
            with Image.open(path) as image:
                self.assertEqual(image.size, (32, 32))
                self.assertEqual(pixel_sha256(image), document["runtime_sha256"][f"data/gfx/terrain-boulders{i}.png"])
        self.assertEqual(export_material.check("boulders", self.root), 16)
        result = validate_material.validate("boulders", self.root)
        self.assertEqual(result["method"], "image-generator")
        # Any edit to a committed frame or the source is caught.
        with Image.open(self.root / "data/gfx/terrain-boulders5.png") as image:
            edited = image.copy()
        edited.putpixel((10, 10), (255, 0, 255, 255))
        edited.save(self.root / "data/gfx/terrain-boulders5.png")
        with self.assertRaises(ValueError):
            validate_material.validate("boulders", self.root)
        with self.assertRaises(ValueError):
            export_material.check("boulders", self.root)

    def test_rejects_small_or_non_square_sources(self):
        source_dir = self.root / "datasrc/gfx/hedge"
        source_dir.mkdir(parents=True)
        Image.new("RGBA", (256, 256), (40, 80, 40, 255)).save(source_dir / "material.png")
        with self.assertRaises(ValueError):
            export_material.export("hedge", self.root)
        Image.new("RGBA", (512, 600), (40, 80, 40, 255)).save(source_dir / "material.png")
        with self.assertRaises(ValueError):
            export_material.export("hedge", self.root)

    def test_animated_glow_is_hybrid_with_four_phases(self):
        self.prepare("lava")
        frames, document = export_material.export("lava", self.root, animate=True)
        self.assertEqual(len(frames), 64)
        self.assertEqual(document["method"], "hybrid")
        self.assertEqual(document["phases"], 4)
        animated = 0
        for variant in range(16):
            phases = [frames[variant + 16 * p] for p in range(4)]
            distinct = len({p.tobytes() for p in phases})
            self.assertIn(distinct, (1, 4))  # a tile without warm pixels is static
            animated += distinct == 4
        self.assertGreaterEqual(animated, 8)
        self.assertEqual(export_material.check("lava", self.root), 64)
        self.assertEqual(validate_material.validate("lava", self.root)["frames"], 64)

    def test_prompts_share_the_header(self):
        for name, prompt in export_material.MATERIAL_PROMPTS.items():
            self.assertTrue(prompt.startswith(export_material.PROMPT_HEADER), name)
            self.assertIn(name.replace("_", " ").split()[0], prompt.lower(), name)
        self.assertEqual(
            set(export_material.MATERIAL_PROMPTS),
            {"boulders", "hedge", "thicket", "lava", "ember_field", "flower_meadow", "outcrop"},
        )

    def test_committed_materials_validate(self):
        for name in terrain_synth.BUILTIN_ORDER:
            with self.subTest(material=name):
                method = validate_material.validate(name)["method"]
                if terrain_synth.RECIPES[name].placeholder_only:
                    self.assertIn(method, ("image-generator", "hybrid"))
                else:
                    self.assertEqual(method, "procedural")

    def test_animated_recipes_require_animate_glow(self):
        self.prepare("lava")
        with self.assertRaises(ValueError):
            export_material.export("lava", self.root)
        self.prepare("boulders")
        with self.assertRaises(ValueError):
            export_material.export("boulders", self.root, animate=True)


if __name__ == "__main__":
    unittest.main()
