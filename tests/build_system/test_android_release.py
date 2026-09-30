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
        self.assertEqual(android_release.release_identity()["versionName"], "0.9.5.0")
        self.assertEqual({arch: android_release.version_code(arch) for arch in android_release.ABI_CODES},
                         {"armeabi-v7a": 905001, "arm64-v8a": 905002, "x86_64": 905003})
        self.assertEqual(android_release.check_recipe(), [905001, 905002, 905003])

    def test_recipe_rejects_missing_abi(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for path in ("mobile/android-release.json", "scons/build_layout.py",
                         "fdroid/metadata/org.globulation.glob2.yml"):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / path).read_bytes())
            recipe = root / "fdroid/metadata/org.globulation.glob2.yml"
            recipe.write_text(recipe.read_text().replace("versionCode: 905003", "versionCode: 905004"))
            with self.assertRaisesRegex(ValueError, "every ABI"):
                android_release.check_recipe(root)

    def test_version_manifest_rejects_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "mobile").mkdir()
            (root / "scons").mkdir()
            (root / "scons/build_layout.py").write_text('PACKAGE_VERSION = "0.9.5.0"\n')
            (root / "mobile/android-release.json").write_text(json.dumps({"versionName": "0.9.5.0", "versionCodeBase": 90501}))
            with self.assertRaisesRegex(ValueError, "does not encode"):
                android_release.release_identity(root)

    def test_apk_rejects_second_abi_and_wrong_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apk = root / "test.apk"
            with zipfile.ZipFile(apk, "w") as package:
                package.writestr("lib/arm64-v8a/libmain.so", b"native")
                package.writestr("lib/x86_64/libmain.so", b"wrong ABI")
                package.writestr("assets/glob2-bundle/index.list", hashlib.sha256(b"").hexdigest() + "\n")
            with self.assertRaisesRegex(ValueError, "native libraries"):
                android_release.verify_apk(apk, "arm64-v8a", root)
            with zipfile.ZipFile(apk, "w") as package:
                package.writestr("lib/arm64-v8a/libmain.so", b"native")
                package.writestr("assets/glob2-bundle/index.list", hashlib.sha256(b"").hexdigest() + "\n")
            with mock.patch.object(android_release.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
                with self.assertRaisesRegex(ValueError, "unsigned"):
                    android_release.verify_apk(apk, "arm64-v8a", root)
            badging = "package: name='org.globulation.glob2' versionCode='905001' versionName='0.9.5.0'\n"
            with mock.patch.object(android_release.subprocess, "run", return_value=subprocess.CompletedProcess([], 1)), mock.patch.object(android_release.subprocess, "check_output", return_value=badging):
                with self.assertRaisesRegex(ValueError, "version code"):
                    android_release.verify_apk(apk, "arm64-v8a", root)

    def test_monitor_state_transitions(self):
        now = dt.datetime(2026, 10, 4, tzinfo=dt.timezone.utc)
        release = {"tag_name": "v0.9.5.0", "published_at": "2026-09-30T00:00:00Z"}
        package = {"packages": [{"versionName": "0.9.5.0", "versionCode": 905001},
                                {"versionName": "0.9.5.0", "versionCode": 905002}]}
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "overdue")
        self.assertEqual(fdroid_monitor.status(release, package, now)["missing_codes"], [905003])
        package["packages"].append({"versionName": "0.9.5.0", "versionCode": 905003})
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "complete")



if __name__ == "__main__":
    unittest.main()
