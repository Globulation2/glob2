"""Check that the Epic directory and normal Windows ZIP contain the same game."""

import importlib.util
import os
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[2] / "tools/release/package_windows.py"
SPEC = importlib.util.spec_from_file_location("package_windows", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WindowsPackageTests(unittest.TestCase):
    def test_epic_stage_and_zip_share_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "glob2-build.exe"
            binary.write_bytes(b"game")
            dll = root / "SDL2.dll"
            dll.write_bytes(b"runtime")
            for name in ("data", "maps", "campaigns", "scripts"):
                folder = root / name
                folder.mkdir()
                (folder / "asset.txt").write_text(name)
            (root / "COPYING").write_text("GPL")
            stage = root / "epic"
            archive = root / "windows.zip"
            old = Path.cwd()
            try:
                os.chdir(root)
                with patch.object(MODULE.subprocess, "run", return_value=subprocess.CompletedProcess(
                        [], 0, stdout="SDL2.dll => /mingw64/bin/SDL2.dll (0x0)")), \
                     patch.object(MODULE.subprocess, "check_output", return_value=str(dll)):
                    MODULE.stage(binary, stage)
                    MODULE.package(binary, archive)
            finally:
                os.chdir(old)
            manifest = root / "SHA256SUMS.txt"
            MODULE.write_manifest(stage, manifest)
            self.assertIn("glob2.exe", manifest.read_text())
            with zipfile.ZipFile(archive) as packed:
                for path in stage.rglob("*"):
                    if path.is_file():
                        relative = path.relative_to(stage).as_posix()
                        self.assertEqual(path.read_bytes(), packed.read("Globulation2/" + relative))


if __name__ == "__main__":
    unittest.main()
