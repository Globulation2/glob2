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
            (dll_dir / "SDL3.dll").write_bytes(b"sdl")
            (dll_dir / "libwinpthread-1.dll").write_bytes(b"thread")

            def imports(command, text):
                name = Path(command[-1]).name
                return {
                    "glob2.exe": "DLL Name: KERNEL32.dll\nDLL Name: SDL3.dll\n",
                    "SDL3.dll": "DLL Name: libwinpthread-1.dll\n",
                    "libwinpthread-1.dll": "DLL Name: KERNEL32.dll\n",
                }[name]

            with patch("tools.release.windows_runtime.subprocess.check_output", side_effect=imports):
                release.stage_dlls(exe, dll_dir, destination)
            self.assertEqual({p.name for p in destination.iterdir()}, {"SDL3.dll", "libwinpthread-1.dll"})

    def test_missing_non_system_dll_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch("tools.release.windows_runtime.subprocess.check_output", return_value="DLL Name: missing.dll\n"):
                with self.assertRaisesRegex(RuntimeError, "missing.dll"):
                    release.stage_dlls(root / "glob2.exe", root, root)

    def test_reported_store_license_files_keep_all_bytes_with_safe_extensions(self):
        reported = (
            "binutils/COPYING.LIB", "binutils/COPYING3.LIB",
            "gettext-runtime/intl/COPYING.LIB", "gettext-runtime/libasprintf/COPYING.LIB",
            "libiconv/COPYING.LIB", "libiconv/libcharset/COPYING.LIB",
            "libquadmath/COPYING.LIB", "libunistring/LICENSE.LIB",
        )
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            licenses = destination / "licenses"
            contents = {}
            for index, name in enumerate(reported):
                path = licenses / name
                path.parent.mkdir(parents=True, exist_ok=True)
                contents[name] = ("GNU license notice ©\r\n" + name + "\n\f").encode("utf-8")
                if index == 0:
                    contents[name] = b"\xef\xbb\xbf" + contents[name]
                path.write_bytes(contents[name])
            ordinary = licenses / "COPYING.txt"
            ordinary.write_bytes(b"Unchanged copyright notice\n")
            real_library = destination / "actual.lib"
            real_library.write_bytes(b"!<arch>\nA real library outside license notices")

            release.normalize_store_license_filenames(destination)

            for name, data in contents.items():
                self.assertFalse((licenses / name).exists())
                self.assertEqual((licenses / (name + ".txt")).read_bytes(), data)
            self.assertEqual(ordinary.read_bytes(), b"Unchanged copyright notice\n")
            self.assertTrue(real_library.read_bytes().startswith(b"!<arch>\n"))

    def test_store_license_target_collision_fails_before_any_rename(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            licenses = destination / "licenses"
            first = licenses / "a/COPYING.LIB"
            second = licenses / "z/COPYING.LIB"
            for path in (first, second):
                path.parent.mkdir(parents=True)
                path.write_bytes(b"License notice\n")
            collision = second.with_name("copying.lib.TXT")
            collision.write_bytes(b"Existing different notice\n")

            with self.assertRaisesRegex(RuntimeError, "Case-insensitive.*collision"):
                release.normalize_store_license_filenames(destination)

            self.assertEqual(first.read_bytes(), b"License notice\n")
            self.assertEqual(second.read_bytes(), b"License notice\n")
            self.assertEqual(collision.read_bytes(), b"Existing different notice\n")
            self.assertFalse(first.with_name(first.name + ".txt").exists())

    def test_store_license_binary_or_empty_notice_fails_before_renaming(self):
        for payload in (b"!<arch>\narchive", b"!<thin>\narchive", b"MZbinary", b"\x00", b"\xff", b""):
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as temporary:
                destination = Path(temporary)
                first = destination / "licenses/a/COPYING.LIB"
                first.parent.mkdir(parents=True)
                first.write_bytes(b"Valid license notice\n")
                bad = destination / "licenses/z/COPYING.LIB"
                bad.parent.mkdir(parents=True)
                bad.write_bytes(payload)
                with self.assertRaisesRegex(RuntimeError, "Non-text Store license"):
                    release.normalize_store_license_filenames(destination)
                self.assertEqual(first.read_bytes(), b"Valid license notice\n")
                self.assertEqual(bad.read_bytes(), payload)

    def test_store_license_extension_matching_is_case_insensitive(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            source = destination / "licenses/copying.lIb"
            source.parent.mkdir()
            source.write_bytes(b"Copyright notice\n")
            release.normalize_store_license_filenames(destination)
            self.assertEqual(source.with_name("copying.lIb.txt").read_bytes(), b"Copyright notice\n")

    def test_main_normalizes_notices_after_runtime_staging(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / "build/glob2.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"executable")
            dll_dir = root / "mingw"
            dll_dir.mkdir()
            destination = root / "content"

            def runtime_notices(*args):
                notice = destination / "licenses/binutils/COPYING.LIB"
                notice.parent.mkdir(parents=True)
                notice.write_bytes(b"All license bytes preserved\r\n")

            command = [str(SCRIPT), "--root", str(root), "--exe", str(executable),
                       "--dll-dir", str(dll_dir), "--dest", str(destination),
                       "--identity-name", "Example.Glob2", "--publisher", "CN=Example",
                       "--publisher-display-name", "Example", "--store-id", "9ABCDEFGHIJK",
                       "--version", "1.11.400.0", "--commit", "0" * 40]
            with patch("sys.argv", command), patch.object(release, "stage_assets"), \
                    patch.object(release, "stage_dlls", side_effect=runtime_notices), \
                    patch.object(release, "write_shell_images"):
                release.main()
            renamed = destination / "licenses/binutils/COPYING.LIB.txt"
            self.assertEqual(renamed.read_bytes(), b"All license bytes preserved\r\n")
            self.assertFalse(renamed.with_name("COPYING.LIB").exists())

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
