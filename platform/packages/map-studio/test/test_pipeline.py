# SPDX-License-Identifier: GPL-3.0-or-later
import sys
import tempfile
import unittest
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from studio_pipeline import carrier, crop, geometry
from map_images.crop import COLORS, choose_crop, marker_components


def tile(width, height, players):
    image = Image.new("RGB", (width, height), COLORS[0])
    for index in range(players):
        x = 8 + index * (width - 24) // players
        y = 8 + index * (height - 24) // players
        for dx in range(2):
            for dy in range(2):
                image.putpixel((x + dx, y + dy), COLORS[11])
    return image


class EnvelopeTests(unittest.TestCase):
    def test_all_63_shape_and_player_combinations(self):
        for width in (128, 256, 512):
            for height in (128, 256, 512):
                for players in range(2, 9):
                    with self.subTest(width=width, height=height, players=players):
                        source = tile(width, height, players)
                        grid = []
                        indices = {c: i for i, c in enumerate(COLORS)}
                        row = list(source.getdata())
                        for y in range(height * 2):
                            grid.extend(
                                indices[c]
                                for c in row[
                                    (y % height) * width : (y % height + 1) * width
                                ]
                                * 2
                            )
                        result = choose_crop(grid, width, height, players, 1)
                        self.assertEqual(result["score"], 0)
                        self.assertEqual(
                            result["global_marker_components"], players * 4
                        )
                        x, y = result["pixel_origin"]
                        markers = marker_components(grid, 2 * width, 2 * height)
                        self.assertEqual(
                            sum(
                                x <= a and c < x + width and y <= b and d < y + height
                                for a, b, c, d in markers
                            ),
                            players,
                        )

    def test_extreme_rectangular_carriers_extract_native_dimensions(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root)
            for width, height in ((128, 512), (512, 128)):
                source = output / "carrier.png"
                carrier(tile(width, height, 8)).save(source)
                report = crop(source, output, width, height, 8)
                with Image.open(output / "candidate.png") as actual:
                    self.assertEqual(actual.size, (width, height))
                self.assertEqual(report["global_marker_components"], 32)
                self.assertEqual(
                    report["carrier_box"], list(geometry(width, height)[1])
                )

    def test_missing_extra_and_transparent_markers_are_rejected(self):
        with tempfile.TemporaryDirectory() as root:
            output = Path(root)
            source = output / "carrier.png"
            carrier(tile(128, 128, 3)).save(source)
            with self.assertRaisesRegex(ValueError, "Expected 16"):
                crop(source, output, 128, 128, 4)
            carrier(tile(128, 128, 5)).save(source)
            with self.assertRaisesRegex(ValueError, "Expected 16"):
                crop(source, output, 128, 128, 4)
            Image.new("RGBA", (128, 128), (0, 128, 0, 0)).save(source)
            with self.assertRaisesRegex(ValueError, "opaque"):
                crop(source, output, 128, 128, 4)


import os
import json
import subprocess


@unittest.skipUnless(
    os.environ.get("GLOB2_BINARY"), "Set GLOB2_BINARY for native import coverage"
)
class NativeImportTests(unittest.TestCase):
    def test_all_63_native_import_and_reload_combinations(self):
        binary = str(Path(os.environ["GLOB2_BINARY"]).resolve())
        root = Path(os.environ["GLOB2_NATIVE_EVIDENCE"]).resolve()
        root.mkdir(parents=True, exist_ok=True)
        outcomes = []
        for width in (128, 256, 512):
            for height in (128, 256, 512):
                for players in range(2, 9):
                    with self.subTest(width=width, height=height, players=players):
                        folder = root / f"{width}x{height}-{players}"
                        folder.mkdir(exist_ok=True)
                        image = folder / "candidate.png"
                        tile(width, height, players).save(image)
                        saved = folder / "map.map"
                        report = folder / "import.json"
                        env = dict(
                            os.environ,
                            GLOB2_USER_DIR=str(folder / "profile"),
                            SDL_VIDEODRIVER="dummy",
                        )
                        subprocess.run(
                            [
                                binary,
                                "--import-map-image",
                                str(image),
                                "--width",
                                str(width),
                                "--height",
                                str(height),
                                "--teams",
                                str(players),
                                "--seed",
                                "19",
                                "--output",
                                str(saved),
                                "--json",
                                str(report),
                            ],
                            env=env,
                            capture_output=True,
                            check=True,
                            timeout=60,
                        )
                        imported = json.loads(report.read_text())
                        self.assertEqual(imported["map"]["player_slots"], players)
                        self.assertEqual(
                            (imported["map"]["width"], imported["map"]["height"]),
                            (width, height),
                        )
                        self.assertEqual(
                            sum(c["alive"] for c in imported["map"]["colonies"]),
                            players,
                        )
                        reloaded = folder / "reload.json"
                        subprocess.run(
                            [
                                binary,
                                "--preview-map",
                                str(saved) + ".gz",
                                "--output",
                                str(folder / "preview.png"),
                                "--json",
                                str(reloaded),
                            ],
                            env=env,
                            capture_output=True,
                            check=True,
                            timeout=60,
                        )
                        inspected = json.loads(reloaded.read_text())
                        self.assertEqual(inspected["map"]["player_slots"], players)
                        self.assertEqual(
                            (inspected["map"]["width"], inspected["map"]["height"]),
                            (width, height),
                        )
                        outcomes.append(
                            {
                                "width": width,
                                "height": height,
                                "players": players,
                                "imported": True,
                                "reloaded": True,
                            }
                        )
        (root / "summary.json").write_text(json.dumps(outcomes, indent=2))


if __name__ == "__main__":
    unittest.main()
