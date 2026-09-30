"""Checks for the Steam Windows staging helper."""

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.package_steam_windows import ASSET_DIRS, stage


class SteamWindowsPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.runtime = self.root / "runtime"
        self.source.mkdir()
        self.runtime.mkdir()
        for name in ASSET_DIRS:
            folder = self.source / name
            folder.mkdir()
            (folder / "asset.txt").write_text(name)
        (self.source / "COPYING").write_text("GPL")
        attribution = self.source / "docs/assets/source-attribution.md"
        attribution.parent.mkdir(parents=True)
        attribution.write_text("attribution")
        self.exe = self.root / "glob2.exe"
        self.exe.write_bytes(b"exe")
        (self.runtime / "SDL2.dll").write_bytes(b"sdl")
        (self.runtime / "libwinpthread-1.dll").write_bytes(b"thread")

    def test_stages_transitive_dlls_and_assets(self):
        def fake_imports(binary):
            return {
                "glob2.exe": {"sdl2.dll", "kernel32.dll"},
                "SDL2.dll": {"libwinpthread-1.dll"},
                "libwinpthread-1.dll": {"kernel32.dll"},
            }[binary.name]

        output = self.root / "depot"
        with patch("tools.package_steam_windows.imports", side_effect=fake_imports):
            stage(self.source, self.exe, self.runtime, output)
        self.assertTrue((output / "SDL2.dll").is_file())
        self.assertTrue((output / "libwinpthread-1.dll").is_file())
        self.assertTrue((output / "data/asset.txt").is_file())
        self.assertTrue((output / "COPYING").is_file())
        self.assertIn("glob2.exe", (output / "SHA256SUMS.txt").read_text())

    def test_missing_runtime_dll_fails(self):
        with patch("tools.package_steam_windows.imports", return_value={"missing.dll"}):
            with self.assertRaisesRegex(FileNotFoundError, "missing.dll"):
                stage(self.source, self.exe, self.runtime, self.root / "depot")

    def test_windows_system_dll_is_not_bundled(self):
        system32 = self.root / "Windows/System32"
        system32.mkdir(parents=True)
        (system32 / "dwrite.dll").write_bytes(b"system")
        with patch.dict(os.environ, {"WINDIR": str(system32.parent)}):
            with patch("tools.package_steam_windows.imports", return_value={"dwrite.dll"}):
                stage(self.source, self.exe, self.runtime, self.root / "depot")
        self.assertFalse((self.root / "depot/dwrite.dll").exists())


if __name__ == "__main__":
    unittest.main()
