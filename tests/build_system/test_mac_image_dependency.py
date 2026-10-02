"""Mac release dependency identity and canonical bundle aliases."""

import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scons"))
import mac_image_dependency as lean
import addDependentLibsToBundle as bundle
import dmg


class MacImageTests(unittest.TestCase):
    def test_identity_ignores_unrelated_libraries_and_hashes_shared_files_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / "libcodec.dylib"
            library.write_bytes(b"codec")
            (root / "libmain.a").write_bytes(b"static shim")
            unrelated = root / "libunrelated.dylib"
            unrelated.write_bytes(b"other")

            def pkg_config(command, **kwargs):
                return {
                    "--modversion": "1.0",
                    "--variable=libdir": str(root),
                    "--libs-only-l": "-lcodec -lmain",
                    "--libs-only-L": "",
                }[command[1]]

            with patch.object(lean.subprocess, "check_output", side_effect=pkg_config):
                with patch.object(lean, "digest", wraps=lean.digest) as hash_file:
                    first = lean.dependency_identity()
                    self.assertEqual(hash_file.call_count, 2)
                unrelated.write_bytes(b"unrelated update")
                self.assertEqual(first, lean.dependency_identity())
                library.write_bytes(b"codec update")
                self.assertNotEqual(first, lean.dependency_identity())

    def test_only_required_codecs_and_saving_are_enabled(self):
        options = lean.lean_options()
        for name in lean.CODECS:
            self.assertIn(
                "-DSDL2IMAGE_"
                + name
                + "="
                + ("ON" if name in ("PNG", "JPG", "WEBP") else "OFF"),
                options,
            )
        self.assertIn("-DSDL2IMAGE_DEPS_SHARED=OFF", options)
        self.assertIn("-DSDL2IMAGE_PNG_SAVE=ON", options)

    def test_corrupted_dependency_rejected(self):
        import json, hashlib

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "lib").write_bytes(b"original")
            (root / "manifest.json").write_text(
                json.dumps(
                    dict(
                        identity={"v": 1},
                        files={"lib": hashlib.sha256(b"original").hexdigest()},
                    )
                )
            )
            self.assertTrue(lean.verified(root, {"v": 1}))
            (root / "lib").write_bytes(b"changed")
            self.assertFalse(lean.verified(root, {"v": 1}))

    def test_aliases_resolve_to_one_real_file_even_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="library aliases ") as temp:
            root = Path(temp)
            real = root / "libSDL3.0.dylib"
            real.write_bytes(b"library")
            alias = root / "libSDL3.dylib"
            alias.symlink_to(real.name)
            resolved = {}
            visited = []
            with patch.object(
                bundle.subprocess, "check_output", return_value="header\n"
            ) as check:
                bundle.libDependencies(str(alias), resolved, visited, [], [str(root)])
                bundle.libDependencies(str(real), resolved, visited, [], [str(root)])
                self.assertEqual(set(resolved.values()), {str(real.resolve())})
                self.assertEqual(
                    check.call_args.args[0], ["otool", "-L", str(real.resolve())]
                )

    def test_missing_dependency_is_fatal(self):
        with self.assertRaises(RuntimeError):
            bundle.libDependencies("@rpath/absent.dylib", {}, [], [], [])

    def test_disk_image_staging_preserves_library_aliases(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root / "Glob2.app"
            app.mkdir()
            (app / "libSDL3.0.dylib").write_bytes(b"library")
            (app / "libSDL3.dylib").symlink_to("libSDL3.0.dylib")

            def inspect(command, **kwargs):
                stage = Path(command[command.index("-srcfolder") + 1]) / "Glob2.app"
                self.assertTrue((stage / "libSDL3.dylib").is_symlink())
                self.assertEqual((stage / "libSDL3.dylib").read_bytes(), b"library")

            with patch.object(dmg.subprocess, "run", side_effect=inspect):
                dmg.create_dmg([root / "release.dmg"], [app], {})


if __name__ == "__main__":
    unittest.main()
