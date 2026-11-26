import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location(
    "package_gamepix", Path(__file__).resolve().parents[2] / "platforms/web/package-gamepix.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

SHELL = """<!doctype html>
<html><head>
<link rel="icon" href="/favicon-32.png">
<script src="/theme.js"></script>
<script src="i18n.js"></script>
<style>@font-face{src:url('/signin/assets/nunito.woff2')}</style>
</head><body>
<img src="/signin/assets/glob-64.png">
<script src="loader-0123abcd.js"></script>
</body></html>
"""


def write(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


class GamePixPackageTests(unittest.TestCase):
    def fixture(self, directory, shell=SHELL):
        source, design = Path(directory) / "static", Path(directory) / "design"
        files = {"index.html": shell.encode(), "i18n.js": b"i18n", "loader-0123abcd.js": b"loader",
                 "index-0123abcd.js": b"js", "index-0123abcd.wasm": b"wasm", "assets/core.1.data": b"data"}
        files["index.html.gz"] = b"sidecar"
        for name, data in files.items():
            write(source, name, data)
        (source / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())))
        for name in ("dist/bootstrap.js", "assets/favicon-32.png", "assets/nunito.woff2", "assets/glob-64.png",
                     "assets/LICENSE-Nunito.txt", "LICENSE"):
            write(design, name, name.encode())
        return source, design

    def test_upload_is_relative_and_loads_the_sdk_first(self):
        with tempfile.TemporaryDirectory() as directory:
            source, design = self.fixture(directory)
            output = Path(directory) / "glob2-gamepix.zip"
            module.write_zip(module.collect(source, design), output)
            module.verify(output)
            with zipfile.ZipFile(output) as archive:
                names = set(archive.namelist())
                html = archive.read("index.html").decode()
                self.assertEqual(archive.read("theme.js"), b"dist/bootstrap.js")
            self.assertNotIn("index.html.gz", names)
            self.assertNotIn("SHA256SUMS", names)
            self.assertTrue({"gamepix.js", "brand/favicon-32.png", "brand/nunito.woff2", "brand/glob-64.png",
                             "licenses/brand/LICENSE-Nunito.txt"} <= names)
            self.assertLess(html.index("lifecycle:'manual'"), html.index(module.SDK_URL))
            self.assertLess(html.index(module.SDK_URL), html.index('src="gamepix.js"'))
            self.assertLess(html.index('src="gamepix.js"'), html.index('src="theme.js"'))
            self.assertIn("url('brand/nunito.woff2')", html)

    def test_output_is_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            source, design = self.fixture(directory)
            first, second = Path(directory) / "a.zip", Path(directory) / "b.zip"
            module.write_zip(module.collect(source, design), first)
            module.write_zip(module.collect(source, design), second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_unknown_root_or_external_references_fail(self):
        for extra in ('<script src="/play/extra.js"></script>', '<script src="https://cdn.example/x.js"></script>'):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                source, design = self.fixture(directory, SHELL.replace("</body>", extra + "</body>"))
                with self.assertRaises(ValueError):
                    module.collect(source, design)

    def test_missing_design_asset_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            source, design = self.fixture(directory)
            (design / "assets/glob-64.png").unlink()
            with self.assertRaises(ValueError):
                module.collect(source, design)

    def test_current_shell_rewrites_cleanly(self):
        shell = (module.ROOT / "platforms/web/shell.html").read_text()
        html, needed = module.rewrite_index(shell)
        self.assertIn("theme.js", needed)
        self.assertIn("brand/glob-64.png", needed)
        self.assertNotRegex(html, module.ROOT_REFERENCE)


if __name__ == "__main__":
    unittest.main()
