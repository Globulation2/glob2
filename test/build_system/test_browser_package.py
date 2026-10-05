import importlib.util
import os
import stat
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "package_static", Path(__file__).resolve().parents[2] / "browser/package-static.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BrowserPackageTests(unittest.TestCase):
    @unittest.skipIf(os.name == "nt", "POSIX web-server permissions")
    def test_threaded_package_is_public_readable_under_private_build_umask(self):
        self.threaded_source()
        previous = os.umask(0o077)
        try:
            module.package(self.source, self.output)
        finally:
            os.umask(previous)
        for path in [self.output, *self.output.rglob("*")]:
            expected = 0o755 if path.is_dir() else 0o644
            self.assertEqual(path.stat().st_mode & 0o777, expected, str(path))
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "build"
        self.source.mkdir()
        self.output = self.root / "static"
        for ext, data in {
            "html": b'<script src="index.js"></script>',
            "js": b'let w="index.wasm",d="assets/core.0123456789abcdef.data";',
            "wasm": b"wasm" * 1000,
        }.items():
            (self.source / ("index." + ext)).write_bytes(data)
        # Content-addressed game data (scons/web_assets.py) keeps its name.
        (self.source / "assets").mkdir()
        self.package = self.source / "assets/core.0123456789abcdef.data"
        self.package.write_bytes(b"assets" * 1000)

    def test_references_sidecars_and_repeatable_identity(self):
        version = module.package(self.source, self.output)
        module.verify(self.output)
        self.assertEqual((self.output / "studio.html").read_bytes(), (self.output / "index.html").read_bytes())
        js = (self.output / f"index-{version}.js").read_text()
        self.assertIn(f"index-{version}.wasm", js)
        self.assertIn("assets/core.0123456789abcdef.data", js)
        self.assertEqual((self.output / "assets/core.0123456789abcdef.data").read_bytes(), self.package.read_bytes())
        self.assertEqual(version, module.package(self.source, self.output))
        module.verify(self.output)

    def threaded_source(self):
        (self.source / "index.html").write_text('<script src="loader.js"></script>')
        (self.source / "loader.js").write_text('/* capability loader */')
        thread = self.source / "threaded"
        thread.mkdir()
        for ext in ("js", "wasm"):
            (thread / ("index." + ext)).write_bytes((self.source / ("index." + ext)).read_bytes())
        return thread

    @unittest.skipIf(os.name == "nt", "POSIX web service permissions")
    def test_public_package_is_readable_with_restrictive_umask(self):
        self.threaded_source()
        previous = os.umask(0o077)
        try:
            module.package(self.source, self.output)
        finally:
            os.umask(previous)
        for path in [self.output, *self.output.rglob("*")]:
            self.assertEqual(stat.S_IMODE(path.stat().st_mode),
                             0o755 if path.is_dir() else 0o644, str(path))
        module.verify(self.output)

    def test_dual_runtime_references_and_version_include_threaded_binary(self):
        thread = self.threaded_source()
        version = module.package(self.source, self.output)
        module.verify(self.output)
        html = (self.output / "index.html").read_text()
        self.assertIn(f'threaded/index-{version}.js', html)
        self.assertIn(f'loader-{version}.js', html)
        js = (self.output / f'threaded/index-{version}.js').read_text()
        self.assertIn(f'index-{version}.wasm', js)
        self.assertEqual(version, module.package(self.source, self.output))
        (thread / "index.wasm").write_bytes(b"changed worker binary")
        self.assertNotEqual(version, module.package(self.source, self.output))
        module.verify(self.output)

    def test_dual_runtime_rejects_missing_worker_sidecar(self):
        self.threaded_source()
        version = module.package(self.source, self.output)
        missing = f'threaded/index-{version}.wasm.gz'
        (self.output / missing).unlink()
        sums = self.output / "SHA256SUMS"
        sums.write_text(''.join(line+'\n' for line in sums.read_text().splitlines()
                                if not line.endswith('  '+missing)))
        with self.assertRaises(ValueError):
            module.verify(self.output)

    def test_recording_assets_are_versioned_and_references_follow_them(self):
        self.threaded_source()
        assets = {"recording-worker.js": b"importScripts('recording-runtime.js','recording-storage.js','recording-video.js');",
                  "recording-runtime.js": b"const wasm='recording-runtime.wasm';",
                  "recording-storage.js": b"/* storage */", "recording-video.js": b"/* video */",
                  "recording-runtime.wasm": b"codec wasm"}
        for name, data in assets.items():
            (self.source / name).write_bytes(data)
        for script in (self.source / "index.js", self.source / "threaded/index.js"):
            script.write_text(script.read_text() + "new Worker('recording-worker.js');")
        version = module.package(self.source, self.output)
        module.verify(self.output)
        worker = f"recording-{version}-worker.js"
        self.assertTrue((self.output / worker).is_file())
        self.assertIn(f"recording-{version}-runtime.wasm", (self.output / f"recording-{version}-runtime.js").read_text())
        for script in (self.output / f"index-{version}.js", self.output / f"threaded/index-{version}.js"):
            self.assertIn(worker, script.read_text())
        self.assertFalse((self.output / "recording-runtime.wasm").exists())
        (self.source / "recording-runtime.wasm").write_bytes(b"new codec wasm")
        self.assertNotEqual(version, module.package(self.source, self.output))

    def music_source(self):
        self.threaded_source()
        assets = {
            "music-worker.js": b"importScripts('music-runtime.js');",
            "music-output.js": b"registerProcessor('glob2-music', Processor);",
            "music-runtime.js": b"const wasm='music-runtime.wasm';",
            "music-runtime.wasm": b"music decoder wasm",
        }
        for name, data in assets.items():
            (self.source / name).write_bytes(data)
        for script in (self.source / "index.js", self.source / "threaded/index.js"):
            script.write_text(script.read_text() + "new Worker('music-worker.js');addModule('music-output.js');")
        notices = self.source / "licenses/opus"
        notices.mkdir(parents=True)
        (notices / "COPYING").write_text("Opus notices")

    def test_music_assets_and_notices_follow_release_identity(self):
        self.music_source()
        version = module.package(self.source, self.output)
        module.verify(self.output)
        for name in module.MUSIC_FILES:
            target = name.replace("music-", f"music-{version}-", 1)
            self.assertTrue((self.output / target).is_file())
            self.assertTrue((self.output / (target + ".gz")).is_file())
            self.assertFalse((self.output / name).exists())
        for script in (self.output / f"index-{version}.js", self.output / f"threaded/index-{version}.js"):
            self.assertIn(f"music-{version}-worker.js", script.read_text())
            self.assertIn(f"music-{version}-output.js", script.read_text())
        self.assertIn(f"music-{version}-runtime.js", (self.output / f"music-{version}-worker.js").read_text())
        self.assertIn(f"music-{version}-runtime.wasm", (self.output / f"music-{version}-runtime.js").read_text())
        self.assertEqual((self.output / "licenses/opus/COPYING").read_text(), "Opus notices")
        self.assertEqual(version, module.package(self.source, self.output))
        (self.source / "music-output.js").write_text("changed worklet")
        self.assertNotEqual(version, module.package(self.source, self.output))

    def test_missing_music_input_keeps_previous_package(self):
        self.music_source()
        module.package(self.source, self.output)
        previous = (self.output / "SHA256SUMS").read_bytes()
        for name in module.MUSIC_FILES:
            (self.source / name).unlink()
            with self.assertRaisesRegex(ValueError, "Incomplete music runtime"):
                module.package(self.source, self.output)
            self.assertEqual((self.output / "SHA256SUMS").read_bytes(), previous)

    def test_music_sidecars_required_even_without_checksum_entry(self):
        self.music_source()
        version = module.package(self.source, self.output)
        missing = f"music-{version}-output.js.gz"
        (self.output / missing).unlink()
        sums = self.output / "SHA256SUMS"
        sums.write_text("".join(line + "\n" for line in sums.read_text().splitlines()
                                if not line.endswith("  " + missing)))
        with self.assertRaises(ValueError):
            module.verify(self.output)

    def test_package_requires_game_data(self):
        self.package.unlink()
        with self.assertRaises(ValueError):
            module.package(self.source, self.output)

    @unittest.skipUnless(os.name == "posix", "POSIX web-container access modes")
    def test_public_package_is_readable_under_private_umask(self):
        for threaded in (False, True):
            with self.subTest(threaded=threaded):
                if threaded:
                    self.threaded_source()
                previous = os.umask(0o077)
                try:
                    module.package(self.source, self.output)
                finally:
                    os.umask(previous)
                self.assertEqual(stat.S_IMODE(self.output.stat().st_mode), 0o755)
                for entry in self.output.rglob("*"):
                    self.assertEqual(stat.S_IMODE(entry.stat().st_mode),
                                     0o755 if entry.is_dir() else 0o644, str(entry))
                module.verify(self.output)

    def test_stale_assets_removed(self):
        old = module.package(self.source, self.output)
        self.package.unlink()
        (self.source / "assets/core.fedcba9876543210.data").write_bytes(b"changed")
        new = module.package(self.source, self.output)
        self.assertNotEqual(old, new)
        self.assertFalse((self.output / f"index-{old}.wasm").exists())
        self.assertFalse((self.output / "assets/core.0123456789abcdef.data").exists())
        self.assertTrue((self.output / "assets/core.fedcba9876543210.data").exists())

    def test_corrupt_or_missing_sidecar_rejected(self):
        module.package(self.source, self.output)
        (self.output / "index.html.gz").write_bytes(b"bad")
        with self.assertRaises(ValueError):
            module.verify(self.output)

    def test_bad_html_keeps_previous_package(self):
        module.package(self.source, self.output)
        old = (self.output / "index.html").read_bytes()
        (self.source / "index.html").write_bytes(b"no script")
        with self.assertRaises(ValueError):
            module.package(self.source, self.output)
        self.assertEqual(old, (self.output / "index.html").read_bytes())

    def test_existing_website_is_not_an_owned_package(self):
        self.output.mkdir()
        index = self.output / "index.html"
        index.write_bytes(b"existing website")
        with self.assertRaises(ValueError):
            module.package(self.source, self.output)
        self.assertEqual(index.read_bytes(), b"existing website")

    def test_non_html_sidecars_are_mandatory_even_if_checksums_are_removed(self):
        for ext in ("js", "wasm", "data"):
            with self.subTest(extension=ext):
                version = module.package(self.source, self.output)
                missing = (f"index-{version}.{ext}.gz" if ext != "data"
                           else "assets/core.0123456789abcdef.data.gz")
                (self.output / missing).unlink()
                sums = self.output / "SHA256SUMS"
                sums.write_text(
                    "\n".join(
                        line
                        for line in sums.read_text().splitlines()
                        if not line.endswith("  " + missing)
                    )
                    + "\n"
                )
                with self.assertRaises(ValueError):
                    module.verify(self.output)

    def test_failed_directory_swap_keeps_previous_package(self):
        module.package(self.source, self.output)
        previous = {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}
        rename = Path.rename

        def fail_swap(path, destination):
            if path.name.startswith("browser-static-"):
                raise OSError("Simulated directory swap failure")
            return rename(path, destination)

        with patch.object(Path, "rename", fail_swap):
            with self.assertRaises(OSError):
                module.package(self.source, self.output)
        self.assertEqual(
            {p.relative_to(self.output).as_posix(): p.read_bytes() for p in self.output.rglob("*") if p.is_file()}, previous
        )
        module.verify(self.output)


if __name__ == "__main__":
    unittest.main()
