import importlib.util
from pathlib import Path
import tempfile
import unittest

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


if __name__ == "__main__":
    unittest.main()
