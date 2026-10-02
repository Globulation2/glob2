"""Checks for the manually triggered Windows Store release staging step."""

import importlib.util
import tempfile
import unittest
import xml.etree.ElementTree as ET
from argparse import Namespace
from pathlib import Path
from unittest.mock import patch

from PIL import Image


SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/windows_store_release.py"
spec = importlib.util.spec_from_file_location("windows_store_release", SCRIPT)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class WindowsStoreReleaseTest(unittest.TestCase):
    def test_transitive_mingw_dependencies_are_staged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dll_dir = root / "mingw"
            dll_dir.mkdir()
            destination = root / "content"
            destination.mkdir()
            exe = root / "glob2.exe"
            exe.write_bytes(b"test")
            (dll_dir / "SDL2.dll").write_bytes(b"sdl")
            (dll_dir / "libwinpthread-1.dll").write_bytes(b"thread")

            def imports(command, text):
                name = Path(command[-1]).name
                return {
                    "glob2.exe": "DLL Name: KERNEL32.dll\nDLL Name: SDL2.dll\n",
                    "SDL2.dll": "DLL Name: libwinpthread-1.dll\n",
                    "libwinpthread-1.dll": "DLL Name: KERNEL32.dll\n",
                }[name]

            with patch("tools.release.windows_runtime.subprocess.check_output", side_effect=imports):
                release.stage_dlls(exe, dll_dir, destination)
            self.assertEqual({p.name for p in destination.iterdir()}, {"SDL2.dll", "libwinpthread-1.dll"})

    def test_missing_non_system_dll_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch("tools.release.windows_runtime.subprocess.check_output", return_value="DLL Name: missing.dll\n"):
                with self.assertRaisesRegex(RuntimeError, "missing.dll"):
                    release.stage_dlls(root / "glob2.exe", root, root)

    def test_game_config_uses_partner_center_identity_without_xbox_services(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            release.write_game_config(destination, Namespace(
                identity_name="Example.Glob2", publisher="CN=Example",
                version="1.0.0.0", store_id="9ABCDEFGHIJK",
                publisher_display_name="Example Publisher",
            ))
            game = ET.parse(destination / "MicrosoftGame.config").getroot()
            self.assertEqual(game.find("Identity").attrib["Publisher"], "CN=Example")
            self.assertEqual(game.find("StoreId").text, "9ABCDEFGHIJK")
            self.assertEqual(game.find("ExecutableList/Executable").attrib["TargetDeviceFamily"], "PC")
            self.assertEqual(game.find("ShellVisuals").attrib["SplashScreenImage"], "SplashScreen.png")
            self.assertIsNone(game.find("TitleId"))
            self.assertIsNone(game.find("MSAAppId"))

    def test_shell_images_include_full_hd_splash(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            icon = destination / "icon.png"
            Image.new("RGBA", (128, 128), (255, 255, 255, 255)).save(icon)
            release.write_shell_images(icon, destination)
            with Image.open(destination / "SplashScreen.png") as splash:
                self.assertEqual(splash.size, (1920, 1080))
                self.assertEqual(splash.mode, "RGB")


if __name__ == "__main__":
    unittest.main()
