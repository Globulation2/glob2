"""Size evidence rejects incomparable builds and accounts for shipped bytes once."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools.release.package_sizes import inventory, compare
from scons.runtime_assets import optimized_install_enabled


class PackageSizeTests(unittest.TestCase):
    def test_complete_inventory_tracks_categories_archives_and_aliases(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "stage"
            (stage / "usr/share/glob2/data/highres").mkdir(parents=True)
            (stage / "usr/share/glob2/data/highres/frame.webp").write_bytes(b"pixels")
            (stage / "libSDL3_image.so.0").write_bytes(b"library")
            (stage / "libSDL3_image.so").symlink_to("libSDL3_image.so.0")
            archive = root / "candidate.tar.gz"
            archive.write_bytes(b"packed")
            with patch(
                "tools.release.package_sizes.subprocess.check_output",
                side_effect=["revision\n", "compiler 1\n"],
            ):
                report = inventory(stage, [archive])
            self.assertEqual(report["payload_bytes"], 13)
            self.assertEqual(
                report["categories"], {"hd-artwork": 6, "runtime-libraries": 7}
            )
            self.assertEqual(report["archives"][0]["bytes"], 6)
            self.assertEqual(len(report["files"]), 3)
            alias = next(item for item in report["files"] if item["kind"] == "symlink")
            self.assertEqual(alias["bytes"], 0)
            self.assertEqual(alias["target"], "libSDL3_image.so.0")

    def test_comparison_rejects_unrelated_source_toolchains_and_scopes(self):
        baseline = dict(
            schema=1,
            revision="abc",
            scope="standalone-artifact",
            platform="Linux",
            architecture="x86_64",
            compiler="gcc 13",
            payload_bytes=100,
            label="baseline",
            categories={},
            archives=[],
        )
        candidate = dict(baseline, payload_bytes=80, label="candidate")
        self.assertEqual(compare(baseline, candidate)["saved_percent"], 20)
        for field in (
            "schema",
            "revision",
            "scope",
            "platform",
            "architecture",
            "compiler",
        ):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                compare(baseline, dict(candidate, **{field: "different"}))

    def test_unrecorded_toolchain_cannot_be_compared(self):
        with self.assertRaisesRegex(ValueError, "compiler"):
            compare({"compiler": None}, {"compiler": None})

    def test_distro_optimization_preserves_debug_build_selection(self):
        self.assertFalse(optimized_install_enabled(False))
        self.assertTrue(optimized_install_enabled(True))
        self.assertTrue(optimized_install_enabled(False, "1"))
        self.assertFalse(optimized_install_enabled(True, "0"))
        with self.assertRaises(ValueError):
            optimized_install_enabled(False, "typo")


if __name__ == "__main__":
    unittest.main()
