"""Release identity, ABI packaging, and F-Droid publication guards."""

import datetime as dt
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "mobile"))
import android_release
import fdroid_monitor


class AndroidReleaseTests(unittest.TestCase):
    def test_version_codes_and_desktop_version(self):
        self.assertEqual(android_release.release_identity()["versionName"], "0.9.5.4")
        self.assertEqual({arch: android_release.version_code(arch) for arch in android_release.ABI_CODES},
                         {"armeabi-v7a": 905041, "arm64-v8a": 905042, "x86_64": 905043})
        self.assertEqual(android_release.check_recipe(), [905041, 905042, 905043])

    def test_recipe_rejects_missing_abi(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "scons/build_layout.py",
                         "mobile/toolchain.json",
                         "fdroid/metadata/org.globulation2.glob2.yml"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            recipe = root / "fdroid/metadata/org.globulation2.glob2.yml"
            recipe.write_text(recipe.read_text().replace("versionCode: 905043", "versionCode: 905044"))
            with self.assertRaisesRegex(ValueError, "every ABI"):
                android_release.check_recipe(root)

    def test_recipe_rejects_sdk_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "mobile/toolchain.json",
                         "scons/build_layout.py", "fdroid/metadata/org.globulation2.glob2.yml"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            recipe = root / "fdroid/metadata/org.globulation2.glob2.yml"
            recipe.write_text(recipe.read_text().replace("platforms;android-36", "platforms;android-35"))
            with self.assertRaisesRegex(ValueError, "every ABI"):
                android_release.check_recipe(root)

    def test_recipe_template_supports_later_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "scons/build_layout.py",
                         "mobile/toolchain.json",
                         "fdroid/metadata/org.globulation2.glob2.yml"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            (root / "mobile/android-release.json").write_text(
                json.dumps({"versionName": "0.9.5.5", "versionCodeBase": 90505}))
            (root / "scons/build_layout.py").write_text('PACKAGE_VERSION = "0.9.5.5"\n')
            self.assertEqual(android_release.check_recipe(root), [905041, 905042, 905043])

    def test_version_manifest_rejects_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "mobile").mkdir()
            (root / "scons").mkdir()
            (root / "scons/build_layout.py").write_text('PACKAGE_VERSION = "0.9.5.4"\n')
            (root / "mobile/android-release.json").write_text(json.dumps({"versionName": "0.9.5.4", "versionCodeBase": 90501}))
            with self.assertRaisesRegex(ValueError, "does not encode"):
                android_release.release_identity(root)

    def test_preflight_rejects_reused_tag_at_other_commit(self):
        with mock.patch.object(android_release.subprocess, "check_output",
                               side_effect=["v0.9.5.4\n", "new-commit\n", "old-commit\n"]):
            with self.assertRaisesRegex(ValueError, "already points to another commit"):
                android_release.check_prior_tags()

    def test_apk_rejects_second_abi_and_wrong_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apk = root / "test.apk"
            with zipfile.ZipFile(apk, "w") as package:
                package.writestr("lib/arm64-v8a/libmain.so", b"native")
                package.writestr("lib/x86_64/libmain.so", b"wrong ABI")
                package.writestr("assets/glob2-bundle/index.list", hashlib.sha256(b"").hexdigest() + "\n")
            with self.assertRaisesRegex(ValueError, "native libraries"):
                android_release.verify_apk(apk, "arm64-v8a", root, require_dependency_manifest=False)
            with zipfile.ZipFile(apk, "w") as package:
                package.writestr("lib/arm64-v8a/libmain.so", b"native")
                package.writestr("assets/glob2-bundle/index.list", hashlib.sha256(b"").hexdigest() + "\n")
            with mock.patch.object(android_release.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
                with self.assertRaisesRegex(ValueError, "unsigned"):
                    android_release.verify_apk(apk, "arm64-v8a", root, require_dependency_manifest=False)
            badging = "package: name='org.globulation2.glob2' versionCode='905041' versionName='0.9.5.4'\n"
            with mock.patch.object(android_release.subprocess, "run", return_value=subprocess.CompletedProcess([], 1)), mock.patch.object(android_release.subprocess, "check_output", return_value=badging):
                with self.assertRaisesRegex(ValueError, "version code"):
                    android_release.verify_apk(apk, "arm64-v8a", root, require_dependency_manifest=False)

    def test_apk_rejects_unexpected_same_abi_library(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "mobile/toolchain.json", "scons/build_layout.py"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            manifest = (root / "build/android/device/arm64-v8a/24/client/release/vcpkg-installed"
                        / "glob2-arm64-android/manifest.json")
            manifest.parent.mkdir(parents=True)
            manifest.write_text(json.dumps({"archives": {"lib/libSDL2.so": "digest"}}))
            apk = root / "unexpected.apk"
            with zipfile.ZipFile(apk, "w") as package:
                for name in ("libmain.so", "libc++_shared.so", "libSDL2.so", "libextra.so"):
                    package.writestr("lib/arm64-v8a/" + name, b"native")
            with mock.patch.dict("os.environ", {"GLOB2_DEV_MODE":"isolated"}), self.assertRaisesRegex(ValueError, "pinned dependency manifest"):
                android_release.verify_apk(apk, "arm64-v8a", root, root=root)

    def test_shared_apk_verification_preserves_release_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "mobile/toolchain.json", "scons/build_layout.py"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            prefix = root / "shared-dependencies"
            prefix.mkdir()
            (prefix / "manifest.json").write_text(json.dumps({"archives": {"lib/libSDL2.a": "digest"}}))
            apk = root / "release.apk"
            with zipfile.ZipFile(apk, "w") as package:
                for name in ("libmain.so", "libc++_shared.so"):
                    package.writestr("lib/arm64-v8a/" + name, b"native")
                package.writestr("assets/glob2-bundle/index.list", hashlib.sha256(b"").hexdigest() + "\n")
            badging = "package: name='org.globulation2.glob2' versionCode='905042' versionName='0.9.5.4'\n"
            with (
                mock.patch.object(android_release, "isolated", return_value=False),
                mock.patch.object(android_release, "dependency_prefix", return_value=prefix),
                mock.patch("mobile_toolchain.discover", return_value={"fingerprint": "pinned"}),
                mock.patch.object(android_release.subprocess, "run", side_effect=[subprocess.CompletedProcess([], 0), subprocess.CompletedProcess([], 1)]),
                mock.patch.object(android_release.subprocess, "check_output", return_value=badging),
            ):
                self.assertEqual(android_release.verify_apk(apk, "arm64-v8a", root, root=root), hashlib.sha256(apk.read_bytes()).hexdigest())

    def test_monitor_state_transitions(self):
        now = dt.datetime(2026, 10, 4, tzinfo=dt.timezone.utc)
        release = {"tag_name": "v0.9.5.4", "published_at": "2026-09-30T00:00:00Z"}
        package = {"packages": [{"versionName": "0.9.5.4", "versionCode": 905041},
                                {"versionName": "0.9.5.4", "versionCode": 905042}]}
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "overdue")
        self.assertEqual(fdroid_monitor.status(release, package, now)["missing_codes"], [905043])
        package["packages"].append({"versionName": "0.9.5.4", "versionCode": 905043})
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "complete")



if __name__ == "__main__":
    unittest.main()
