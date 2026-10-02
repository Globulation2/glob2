"""Offline builds reject unverified input and keep distro recipes on shared pins."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools import build_asset_encoder as encoder

ROOT = Path(__file__).resolve().parents[2]


class OfflineEncoderTests(unittest.TestCase):
    def test_sources_match_rpm_and_flatpak_inputs(self):
        rpm = (ROOT / "fedora/glob2.spec").read_text()
        encoded = (ROOT / "flatpak/org.globulation2.Globulation2.yml.in").read_text()
        for artifact in encoder.SOURCES.values():
            self.assertIn(artifact["url"], rpm)
            self.assertIn(artifact["url"], encoded)
            self.assertIn(artifact["sha256"], encoded)

    def test_corrupt_archive_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = next(iter(encoder.SOURCES.values()))
            (root / Path(artifact["url"]).name).write_bytes(b"corrupt")
            with patch.object(encoder.tarfile, "open") as extract:
                with self.assertRaisesRegex(ValueError, "corrupt"):
                    encoder.source_trees(root, root)
                extract.assert_not_called()

    def test_build_refuses_to_overwrite_existing_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "fresh"):
                encoder.build(temporary, temporary)


if __name__ == "__main__":
    unittest.main()
