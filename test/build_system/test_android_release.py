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
    def test_candidate_accepts_existing_version_but_publication_rejects_tag_collision(self):
        with mock.patch.object(android_release.subprocess, "check_output", side_effect=[
                "v0.11.0.1\n", "candidate\n", "published\n"]):
            with self.assertRaisesRegex(ValueError, "already points"):
                android_release.check_prior_tags()
        with mock.patch.object(android_release.subprocess, "check_output", side_effect=AssertionError("Candidate must not inspect release tags")):
            self.assertEqual(android_release.check_candidate()["versionName"], "0.11.0.1")
    def test_version_codes_and_desktop_version(self):
        self.assertEqual(android_release.release_identity()["versionName"], "0.11.0.1")
        self.assertEqual({arch: android_release.version_code(arch) for arch in android_release.ABI_CODES},
                         {"armeabi-v7a": 1100011, "arm64-v8a": 1100012, "x86_64": 1100013})
        self.assertEqual(android_release.check_recipe(), [1100001, 1100002, 1100003])

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
            recipe.write_text(recipe.read_text().replace("versionCode: 1100003", "versionCode: 1100004"))
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
                json.dumps({"versionName": "0.11.0.2", "versionCodeBase": 110002}))
            (root / "scons/build_layout.py").write_text('PACKAGE_VERSION = "0.11.0.2"\n')
            self.assertEqual(android_release.check_recipe(root), [1100001, 1100002, 1100003])

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
                               side_effect=["v0.11.0.1\n", "new-commit\n", "old-commit\n"]):
            with self.assertRaisesRegex(ValueError, "already points to another commit"):
                android_release.check_prior_tags()

    def test_development_check_allows_released_version_but_not_lower_codes(self):
        with mock.patch.object(android_release.subprocess, "check_output",
                               return_value="v0.10.0.0\nv0.11.0.1\n"):
            android_release.check_prior_tags(development=True)
        with mock.patch.object(android_release.subprocess, "check_output",
                               return_value="v0.11.0.1\nv0.11.1.0\n"):
            with self.assertRaisesRegex(ValueError, "would not increase past v0.11.1.0"):
                android_release.check_prior_tags(development=True)

    def test_development_cli_preserves_identity_and_recipe_validation(self):
        for validator in ("release_identity", "check_recipe"):
            with self.subTest(validator=validator), \
                    mock.patch.object(sys, "argv", ["android_release.py", "check", "--development"]), \
                    mock.patch.object(android_release.subprocess, "check_output", return_value="v0.9.5.4\n"), \
                    mock.patch.object(android_release, validator, side_effect=ValueError("invalid contract")):
                with self.assertRaisesRegex(ValueError, "invalid contract"):
                    android_release.main()

    def test_workflows_keep_publication_preflight_strict(self):
        # PR/master Android builds skip the release contract (release-only, #604);
        # the release workflows keep the strict check.
        mobile = (ROOT / ".github/workflows/mobile.yml").read_text()
        self.assertNotIn("android_release.py check", mobile)
        self.assertNotIn("python3 mobile/android_release.py verify-apk", mobile)
        for name in ("release.yml", "fdroid-release-validation.yml"):
            workflow = (ROOT / ".github/workflows" / name).read_text()
            self.assertIn("python3 mobile/android_release.py ${{ inputs.tag && 'check' || 'check-candidate' }}\n", workflow)
            self.assertNotIn("check --development", workflow)

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
            badging = "package: name='org.globulation2.glob2' versionCode='1100011' versionName='0.9.5.4'\n"
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
            manifest.write_text(json.dumps({"archives": {"lib/libSDL3.so": "digest"}}))
            apk = root / "unexpected.apk"
            with zipfile.ZipFile(apk, "w") as package:
                for name in ("libmain.so", "libc++_shared.so", "libSDL3.so", "libextra.so"):
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
            badging = "package: name='org.globulation2.glob2' versionCode='1100012' versionName='0.11.0.1'\n"
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
        release = {"tag_name": "v0.11.0.1", "published_at": "2026-09-30T00:00:00Z"}
        package = {"packages": [{"versionName": "0.11.0.1", "versionCode": 1100011},
                                {"versionName": "0.11.0.1", "versionCode": 1100012}]}
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "overdue")
        self.assertEqual(fdroid_monitor.status(release, package, now)["missing_codes"], [1100013])
        package["packages"].append({"versionName": "0.11.0.1", "versionCode": 1100013})
        self.assertEqual(fdroid_monitor.status(release, package, now)["state"], "complete")



class InviteLinkManifestTests(unittest.TestCase):
    """Editions without online play (Amazon, China) must not claim invite links."""

    def test_offline_editions_drop_both_invite_filters(self):
        import android
        manifest = (ROOT / "mobile/android/app/src/main/AndroidManifest.xml").read_text()
        self.assertIn('android:autoVerify="true"', manifest)
        self.assertIn('android:host="${officialInstanceHost}" android:pathPrefix="/j/"', manifest)
        self.assertIn('android:host="${officialInstanceHost}" android:pathPrefix="/play/"', manifest)
        self.assertIn('android:scheme="glob2" android:host="play"', manifest)
        stripped = android.without_invite_links(manifest)
        for gone in ("android.intent.action.VIEW", 'android:scheme="glob2"', "autoVerify", "officialInstanceHost"):
            self.assertNotIn(gone, stripped)
        for kept in ("android.intent.category.LAUNCHER", "android.permission.INTERNET", "</activity>"):
            self.assertIn(kept, stripped)

    def test_unexpected_layout_is_refused(self):
        import android
        with self.assertRaisesRegex(ValueError, "invite links"):
            android.without_invite_links("<manifest><intent-filter><action android:name=\"android.intent.action.VIEW\"/>")


if __name__ == "__main__":
    unittest.main()
