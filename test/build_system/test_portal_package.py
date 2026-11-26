from dataclasses import replace
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


module = load("package_portal", "platforms/web/package-portal.py")
gamepix = load("package_gamepix", "platforms/web/package-gamepix.py")

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

POLICIES = {
    "gamepix": (False, False), "crazygames": (False, False), "newgrounds": (True, True),
    "gamejolt": (True, True), "gamedistribution": (False, False), "yandex": (False, False),
    "poki": (False, False),
}


def write(root, name, data):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def fixture(directory, shell=SHELL, extra=None):
    source, design = Path(directory) / "static", Path(directory) / "design"
    files = {"index.html": shell.encode(), "i18n.js": b"i18n", "loader-0123abcd.js": b"loader",
             "index-0123abcd.js": b"js", "index-0123abcd.wasm": b"wasm", "assets/core.1.data": b"data",
             "index.html.gz": b"sidecar", **(extra or {})}
    for name, data in files.items():
        write(source, name, data)
    (source / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(files.items())))
    for name in ("dist/bootstrap.js", "assets/favicon-32.png", "assets/nunito.woff2", "assets/glob-64.png",
                 "assets/LICENSE-Nunito.txt", "LICENSE"):
        write(design, name, name.encode())
    return source, design


def package(directory, profile, identifier=None, **options):
    source, design = fixture(directory, **options)
    output = Path(directory) / f"glob2-{profile.name}.zip"
    module.write_zip(module.collect(source, design, profile, identifier), output)
    return output


def scripts(html):
    return [(re.search(r'src="([^"]+)"', attributes) or [None, None])[1]
            for attributes in re.findall(r"<script\b([^>]*)>", html)]


class PortalProfileTests(unittest.TestCase):
    def test_every_portal_is_profiled(self):
        self.assertEqual(set(module.PORTALS), set(POLICIES))

    def test_profiles_cite_sources_and_bundle_existing_adapters(self):
        for profile in module.PORTALS.values():
            with self.subTest(portal=profile.name):
                self.assertTrue(profile.sources)
                for source in profile.sources:
                    self.assertTrue(source.startswith("https://"), source)
                self.assertEqual(profile.adapters[0], module.CORE)
                for adapter in profile.adapters:
                    self.assertTrue((module.WEB / adapter).is_file(), adapter)
                self.assertEqual(profile.sdk_url is None, "SDK" not in profile.scripts)
                self.assertIn(profile.size_basis, ("zip", "unpacked"))

    def test_policies_follow_the_portal_rules(self):
        for name, (links, sign_in) in POLICIES.items():
            with self.subTest(portal=name):
                self.assertEqual(module.portal(name).policy,
                                 {"name": name, "externalLinks": links, "onlineSignIn": sign_in})

    def test_unknown_portal_fails(self):
        with self.assertRaisesRegex(ValueError, "Unknown portal"):
            module.portal("example")


class PortalPackageTests(unittest.TestCase):
    def test_every_upload_is_relative_and_loads_policy_sdk_and_adapters_first(self):
        for profile in module.PORTALS.values():
            with self.subTest(portal=profile.name), tempfile.TemporaryDirectory() as directory:
                output = package(directory, profile)
                module.verify(output, profile)
                with zipfile.ZipFile(output) as archive:
                    names = set(archive.namelist())
                    html = archive.read("index.html").decode()
                    for adapter in profile.adapters:
                        self.assertEqual(archive.read(adapter), (module.WEB / adapter).read_bytes())
                self.assertNotIn("index.html.gz", names)
                self.assertNotIn("SHA256SUMS", names)
                self.assertTrue({"theme.js", "brand/favicon-32.png", "brand/nunito.woff2",
                                 "licenses/brand/LICENSE-Nunito.txt"} <= names)
                expected = [None] + [profile.sdk_url if s == "SDK" else s for s in profile.scripts]
                self.assertEqual(scripts(html)[:len(expected) + 1], expected + ["theme.js"])
                policy = json.loads(re.search(r"window\.GLOB2_PORTAL=(\{[^}]*\});", html).group(1))
                self.assertEqual(policy, profile.policy)
                self.assertIn("url('brand/nunito.woff2')", html)

    def test_policy_runs_before_any_other_script(self):
        for profile in module.PORTALS.values():
            with self.subTest(portal=profile.name):
                html, _ = module.rewrite_index(SHELL, profile, module.game_id(profile))
                self.assertTrue(html[html.index("<script"):].startswith("<script>window.GLOB2_PORTAL="))

    def test_sdk_configuration_precedes_the_sdk(self):
        html, _ = module.rewrite_index(SHELL, module.portal("gamepix"))
        self.assertLess(html.index("lifecycle:'manual'"), html.index(module.portal("gamepix").sdk_url))
        profile = module.portal("gamedistribution")
        html, _ = module.rewrite_index(SHELL, profile, "abc123")
        self.assertLess(html.index('window.GD_OPTIONS={gameId:"abc123"}'), html.index('src="gamedistribution.js"'))
        self.assertLess(html.index('src="gamedistribution.js"'), html.index(profile.sdk_url))

    def test_gamedistribution_game_id_comes_from_flag_environment_or_placeholder(self):
        profile = module.portal("gamedistribution")
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertEqual(module.game_id(profile), module.GAME_ID_PLACEHOLDER)
            self.assertEqual(module.game_id(profile, "flag-id"), "flag-id")
        with mock.patch.dict(os.environ, {"GLOB2_GAMEDISTRIBUTION_GAME_ID": "env-id"}):
            self.assertEqual(module.game_id(profile), "env-id")
            self.assertEqual(module.game_id(profile, "flag-id"), "flag-id")
        with self.assertRaises(ValueError):
            module.game_id(profile, 'x";alert(1)//')
        self.assertIsNone(module.game_id(module.portal("crazygames"), "ignored"))

    def test_output_is_reproducible(self):
        for profile in module.PORTALS.values():
            with self.subTest(portal=profile.name), tempfile.TemporaryDirectory() as directory:
                source, design = fixture(directory)
                first, second = Path(directory) / "a.zip", Path(directory) / "b.zip"
                module.write_zip(module.collect(source, design, profile), first)
                module.write_zip(module.collect(source, design, profile), second)
                self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_gamepix_wrapper_matches_the_portal_packager(self):
        profile = module.portal("gamepix")
        with tempfile.TemporaryDirectory() as directory:
            source, design = fixture(directory)
            first, second = Path(directory) / "a.zip", Path(directory) / "b.zip"
            module.write_zip(module.collect(source, design, profile), first)
            gamepix.write_zip(gamepix.collect(source, design), second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            gamepix.verify(second)

    def test_unknown_root_or_external_references_fail(self):
        for extra in ('<script src="/play/extra.js"></script>', '<script src="https://cdn.example/x.js"></script>'):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                source, design = fixture(directory, SHELL.replace("</body>", extra + "</body>"))
                with self.assertRaises(ValueError):
                    module.collect(source, design, module.portal("crazygames"))

    def test_adapter_colliding_with_a_release_file_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            source, design = fixture(directory, extra={"portal-core.js": b"release"})
            with self.assertRaisesRegex(ValueError, "collides"):
                module.collect(source, design, module.portal("newgrounds"))

    def test_verify_rejects_a_reordered_upload(self):
        profile = module.portal("crazygames")
        with tempfile.TemporaryDirectory() as directory:
            source, design = fixture(directory)
            files = module.collect(source, design, profile)
            html = files["index.html"].decode()
            files["index.html"] = html.replace(f'<script src="{profile.sdk_url}"></script>\n', "").encode()
            output = Path(directory) / "bad.zip"
            module.write_zip(files, output)
            with self.assertRaisesRegex(ValueError, "must load before the game"):
                module.verify(output, profile)


class PortalLimitTests(unittest.TestCase):
    def test_upload_above_the_size_limit_fails(self):
        for name in ("gamepix", "crazygames", "newgrounds", "yandex"):
            profile = module.portal(name)
            with self.subTest(portal=name), tempfile.TemporaryDirectory() as directory:
                output = package(directory, profile)
                size = module.measure(output)[profile.size_basis]
                module.check_limits(output, replace(profile, max_bytes=size))
                with self.assertRaisesRegex(ValueError, "accepts at most"):
                    module.check_limits(output, replace(profile, max_bytes=size - 1))

    def test_upload_above_the_file_limit_fails(self):
        profile = module.portal("crazygames")
        self.assertEqual(profile.max_files, 1500)
        with tempfile.TemporaryDirectory() as directory:
            extra = {f"assets/extra-{index}.data": b"x" for index in range(4)}
            output = package(directory, replace(profile, max_files=10), extra=extra)
            with self.assertRaisesRegex(ValueError, "files"):
                module.verify(output, replace(profile, max_files=10))

    def test_documented_limits(self):
        limits = {name: (profile.max_bytes, profile.max_files, profile.size_basis, profile.first_download_bytes)
                  for name, profile in module.PORTALS.items()}
        self.assertEqual(limits["crazygames"], (250 * module.MB, 1500, "unpacked", 50 * module.MB))
        self.assertEqual(limits["gamepix"][:2], (200 * module.MB, None))
        self.assertEqual(limits["yandex"][:3], (100 * module.MB, None, "unpacked"))

    def test_yandex_rejects_spaces_in_file_names(self):
        profile = module.portal("yandex")
        with tempfile.TemporaryDirectory() as directory:
            output = package(directory, profile, extra={"assets/bad name.data": b"x"})
            with self.assertRaisesRegex(ValueError, "file name"):
                module.verify(output, profile)

    def test_summary_reports_size_files_and_limits(self):
        profile = module.portal("crazygames")
        with tempfile.TemporaryDirectory() as directory:
            line = module.summary(package(directory, profile), profile)
        self.assertRegex(line, r"^CrazyGames upload: `glob2-crazygames.zip` [\d.]+ MB zip, [\d.]+ MB unpacked, \d+ files "
                               r"\(limit: 250 MB unpacked, 1500 files\)$")


class ShellTests(unittest.TestCase):
    def test_current_shell_rewrites_cleanly_for_every_portal(self):
        shell = (ROOT / "platforms/web/shell.html").read_text()
        for profile in module.PORTALS.values():
            with self.subTest(portal=profile.name):
                html, needed = module.rewrite_index(shell, profile, module.game_id(profile))
                self.assertIn("theme.js", needed)
                self.assertIn("brand/glob-64.png", needed)
                without_sdk = html.replace(f'src="{profile.sdk_url}"', "") if profile.sdk_url else html
                self.assertNotRegex(without_sdk, module.ROOT_REFERENCE)


if __name__ == "__main__":
    unittest.main()
