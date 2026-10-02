"""Windows staging rejects missing imports and handles private DLLs and spaces."""

import shutil
import tempfile
import unittest
from pathlib import Path

from tools.release.package_nsis import INVENTORY_BEGIN, INVENTORY_END, file_lists, package, quote
from tools.release.windows_runtime import stage_dlls


class WindowsRuntimeTests(unittest.TestCase):
    def test_recursive_case_insensitive_imports_prefer_private_runtime(self):
        with tempfile.TemporaryDirectory(prefix="runtime with spaces ") as temporary:
            root = Path(temporary)
            stock, lean, output = (root / name for name in ("stock", "lean", "output"))
            for directory in (stock, lean, output):
                directory.mkdir()
            executable = output / "glob2.exe"
            executable.write_bytes(b"game")
            (stock / "SDL3_image.dll").write_bytes(b"stock")
            (lean / "SDL3_image.dll").write_bytes(b"lean")
            (stock / "libwebp.dll").write_bytes(b"webp")
            (stock / "unused.dll").write_bytes(b"unused")
            imports = {
                "glob2.exe": {"SDL3_IMAGE.DLL", "kernel32.dll"},
                "SDL3_image.dll": {"LIBWEBP.DLL"},
                "libwebp.dll": {"sdl3_image.dll"},
            }
            stage_dlls(
                executable,
                [lean, stock],
                output,
                inspect=lambda path: imports[path.name],
            )
            self.assertEqual((output / "SDL3_image.dll").read_bytes(), b"lean")
            self.assertTrue((output / "libwebp.dll").is_file())
            self.assertFalse((output / "unused.dll").exists())

    def test_private_sdl3_runtime_stages_notices_and_rejects_sdl2(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime, output = root / "sdk/bin", root / "output"
            runtime.mkdir(parents=True); output.mkdir()
            (runtime / "SDL3.dll").write_bytes(b"sdl3")
            notices = root / "sdk/share/licenses/SDL3"
            notices.mkdir(parents=True)
            (notices / "LICENSE.txt").write_text("SDL license")
            executable = output / "glob2.exe"
            executable.write_bytes(b"game")
            stage_dlls(executable, [runtime], output,
                       inspect=lambda path: {"SDL3.dll"} if path == executable else {"kernel32.dll"})
            self.assertEqual((output / "licenses/SDL3/LICENSE.txt").read_text(), "SDL license")
            with self.assertRaisesRegex(ValueError, "SDL2 runtime import"):
                stage_dlls(executable, [runtime], output, inspect=lambda path: {"SDL2.dll"})

    def test_missing_import_fails_with_importer(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(FileNotFoundError, "missing.dll.*glob2.exe"):
                stage_dlls(
                    root / "glob2.exe",
                    [root],
                    root,
                    inspect=lambda path: {"missing.dll"},
                )

    def test_installer_lists_preserve_relative_paths_and_only_owned_files(self):
        with tempfile.TemporaryDirectory(prefix="stage $ with spaces ") as temporary:
            root = Path(temporary)
            stage, output = root / "stage", root / "lists"
            (stage / "data/gfx").mkdir(parents=True)
            (stage / "glob2.exe").write_bytes(b"game")
            (stage / "data/gfx/image.webp").write_bytes(b"image")
            file_lists(stage, output)
            install = (output / "install.nsh").read_text()
            self.assertIn("$ with spaces", install)
            self.assertIn('Delete "$INSTDIR\\data\\gfx\\image.png"', install)
            self.assertEqual(
                (output / "owned.txt").read_text(encoding="utf-16-le").splitlines(),
                [INVENTORY_BEGIN, "data\\gfx\\image.webp", "glob2.exe", INVENTORY_END],
            )
            self.assertNotIn("/r", (output / "directories.nsh").read_text())

    @unittest.skipUnless(shutil.which('makensis'), 'NSIS compiler unavailable')
    def test_actual_compiler_accepts_unicode_paths_spaces_and_literal_dollars(self):
        with tempfile.TemporaryDirectory(prefix='nsis $ with spaces ') as temporary:
            root = Path(temporary)
            stage = root / 'stage'
            stage.mkdir()
            (stage / 'glob2.exe').write_bytes(b'game')
            (stage / 'COPYING').write_text('license')
            (stage / 'é.txt').write_text('asset')
            output = root / 'glob2-fixture-setup.exe'
            package(stage, output, '0.9.5.4')
            self.assertTrue(output.is_file())

    def test_rejects_symlinks_and_invalid_nsis_strings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "glob2.exe").write_bytes(b"game")
            (root / "alias").symlink_to("glob2.exe")
            with self.assertRaisesRegex(ValueError, "symlinks"):
                file_lists(root, root / "lists")
        with self.assertRaises(ValueError):
            quote("path\ncommand")


if __name__ == "__main__":
    unittest.main()
