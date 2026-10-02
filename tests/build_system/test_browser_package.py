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
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "build"
        self.source.mkdir()
        self.output = self.root / "static"
        for ext, data in {
            "html": b'<script src="index.js"></script>',
            "js": b'let w="index.wasm",d="index.data";',
            "wasm": b"wasm" * 1000,
            "data": b"assets" * 1000,
        }.items():
            (self.source / ("index." + ext)).write_bytes(data)

    def test_references_sidecars_and_repeatable_identity(self):
        version = module.package(self.source, self.output)
        module.verify(self.output)
        js = (self.output / f"index-{version}.js").read_text()
        self.assertIn(f"index-{version}.data", js)
        self.assertEqual(version, module.package(self.source, self.output))
        module.verify(self.output)

    def threaded_source(self):
        (self.source / "index.html").write_text('<script src="loader.js"></script>')
        (self.source / "loader.js").write_text('/* capability loader */')
        thread = self.source / "threaded"
        thread.mkdir()
        for ext in ("js", "wasm", "data"):
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
        self.assertIn(f'index-{version}.data', js)
        self.assertEqual(version, module.package(self.source, self.output))
        (thread / "index.wasm").write_bytes(b"changed worker binary")
        self.assertNotEqual(version, module.package(self.source, self.output))
        module.verify(self.output)

    def test_dual_runtime_rejects_different_assets_and_missing_worker_sidecar(self):
        thread = self.threaded_source()
        version = module.package(self.source, self.output)
        missing = f'threaded/index-{version}.wasm.gz'
        (self.output / missing).unlink()
        sums = self.output / "SHA256SUMS"
        sums.write_text(''.join(line+'\n' for line in sums.read_text().splitlines()
                                if not line.endswith('  '+missing)))
        with self.assertRaises(ValueError):
            module.verify(self.output)
        (thread / "index.data").write_bytes(b"different assets")
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
        (self.source / "index.data").write_bytes(b"changed")
        new = module.package(self.source, self.output)
        self.assertNotEqual(old, new)
        self.assertFalse((self.output / f"index-{old}.data").exists())

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
                missing = f"index-{version}.{ext}.gz"
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
        previous = {p.name: p.read_bytes() for p in self.output.iterdir()}
        rename = Path.rename

        def fail_swap(path, destination):
            if path.name.startswith("browser-static-"):
                raise OSError("Simulated directory swap failure")
            return rename(path, destination)

        with patch.object(Path, "rename", fail_swap):
            with self.assertRaises(OSError):
                module.package(self.source, self.output)
        self.assertEqual(
            {p.name: p.read_bytes() for p in self.output.iterdir()}, previous
        )
        module.verify(self.output)


if __name__ == "__main__":
    unittest.main()
