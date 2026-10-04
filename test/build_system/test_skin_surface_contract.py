"""Regression checks for defects that bounded GSK decoding cannot detect."""

import json
from pathlib import Path
import struct
import sys
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/skins"))
from surface_contract import validate_surface
import install_units


class SkinSurfaceContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = (ROOT / "data/skins/colony-v1/worker-walk.gsk").read_bytes()
        cls.contract = json.loads(
            (ROOT / "data/skins/colony-v1/worker-surface.json").read_text()
        )
        cls.count = struct.unpack_from("<I", cls.data, 4)[0]

    def test_rejects_paint_that_does_not_match_its_counterpart(self):
        data = bytearray(self.data)
        index = next(
            i for i, j in enumerate(self.contract["reflections"]["frontBack"]) if i != j
        )
        struct.pack_into("<f", data, 20 + index * 8, 0.12345)
        with self.assertRaisesRegex(AssertionError, "paint symmetry mismatch"):
            validate_surface(data, self.contract)

    def test_rejects_a_triangle_joining_different_feet(self):
        data = bytearray(self.data)
        regions = self.contract["regions"]
        left, right = regions.index(2), regions.index(3)
        struct.pack_into("<3I", data, 20 + self.count * 8, left, right, left + 1)
        with self.assertRaisesRegex(AssertionError, "bridges different limbs"):
            validate_surface(data, self.contract)

    def test_rejects_a_collapsed_connection(self):
        data = bytearray(self.data)
        offset = 20 + self.count * 8
        a, b, c = struct.unpack_from("<3I", data, offset)
        pose = offset + struct.unpack_from("<I", data, 8)[0] * 4
        for index in (b, c):
            data[pose + index * 24 : pose + index * 24 + 12] = data[
                pose + a * 24 : pose + a * 24 + 12
            ]
        with self.assertRaisesRegex(AssertionError, "collapsed joint triangle"):
            validate_surface(data, self.contract)


class SkinInstallTest(unittest.TestCase):
    def test_incomplete_export_does_not_replace_installed_meshes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            installed = root / "data/skins/colony-v1"
            installed.mkdir(parents=True)
            (installed / "manifest.json").write_text("{}")
            mesh = installed / "worker-walk.gsk"
            mesh.write_bytes(b"keep the installed mesh")
            staged = root / "staged"
            staged.mkdir()
            (staged / "worker-manifest.json").write_text('{"clips":{}}')
            with patch.object(install_units, "ROOT", root):
                with self.assertRaisesRegex(ValueError, "Incomplete worker clips"):
                    install_units.install(staged)
            self.assertEqual(mesh.read_bytes(), b"keep the installed mesh")
            self.assertEqual((installed / "manifest.json").read_text(), "{}")


if __name__ == "__main__":
    unittest.main()
