"""Release metadata must describe final bytes and explicit reviewed qualification."""

import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools/release/downloads_manifest.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("downloads_manifest", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
STAGE_SPEC = importlib.util.spec_from_file_location("stage_downloads", SCRIPT.parent / "stage_downloads.py")
STAGE = importlib.util.module_from_spec(STAGE_SPEC)
STAGE_SPEC.loader.exec_module(STAGE)
ROOT = SCRIPT.parents[2]


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.sha = "a" * 40
        self.tag = "v1.2.3"
        self.inventory = {"schemaVersion": 1, "sourceCommit": self.sha, "packages": [], "sources": [{"filename": "glob2-1.2.3.tar.gz"}]}
        for platform, arch, kind in sorted(MODULE.REQUIRED):
            filename = f"glob2-1.2.3-{platform}-{arch}.{kind}"
            self.inventory["packages"].append({"platform": platform, "architecture": arch,
                "format": kind, "minimumOs": "Test minimum", "filename": filename})
        digests = {}
        for descriptor in self.inventory["packages"] + self.inventory["sources"]:
            payload = descriptor["filename"].encode()
            (self.directory / descriptor["filename"]).write_bytes(payload)
            digests[descriptor["filename"]] = hashlib.sha256(payload).hexdigest()
        self.evidence = {"schemaVersion": 1, "tag": self.tag, "sourceCommit": self.sha,
            "reviewedBy": "reviewer", "reviewedAt": "2020-01-01T00:00:00Z", "evidenceUrl": "https://example.org/evidence",
            "platforms": {platform: {gate: True for gate in gates} for platform, gates in MODULE.qualification.PLATFORMS.items()},
            "compatibility": {gate: True for gate in MODULE.qualification.COMPATIBILITY},
            "testedTargets": {target: True for target in MODULE.qualification.TESTED_TARGETS},
            "stores": {"googlePlay": {"url": "https://play.google.com/store/apps/details?id=org.globulation2.glob2", "production": True},
                       "appStore": {"url": "https://apps.apple.com/us/app/glob2/id123456", "production": True}},
            "artifacts": digests}

    def generate(self):
        return MODULE.generate(self.directory, self.inventory, self.evidence, self.tag, self.sha)

    def test_final_bytes_and_all_formats(self):
        result = self.generate()
        self.assertEqual(len(result["packages"]), 11)
        self.assertEqual({(p["platform"], p["architecture"], p["format"]) for p in result["packages"]}, MODULE.REQUIRED)
        self.assertTrue(result["qualification"]["qualified"])
        for item in result["packages"] + result["sources"]:
            payload = (self.directory / item["filename"]).read_bytes()
            self.assertEqual(item["sizeBytes"], len(payload))
            self.assertEqual(item["sha256"], hashlib.sha256(payload).hexdigest())
            self.assertEqual(item["url"], f"https://github.com/Globulation2/glob2/releases/download/{self.tag}/{item['filename']}")
        self.assertNotIn("reviewedBy", json.dumps(result))
        self.assertNotIn("example.org", json.dumps(result))

    def test_numeric_tag_component_parity(self):
        self.tag = "v1.2.3.4.5"
        self.evidence["tag"] = self.tag
        self.assertEqual(self.generate()["version"], "1.2.3.4.5")
        for invalid in ("v1", "1.2", "v1.2-beta", "v1..2"):
            self.tag = invalid
            self.evidence["tag"] = invalid
            with self.subTest(tag=invalid), self.assertRaisesRegex(ValueError, "release tag"):
                self.generate()

    def test_missing_each_package_rejected(self):
        original = copy.deepcopy(self.inventory)
        for index in range(len(original["packages"])):
            with self.subTest(index=index):
                self.inventory = copy.deepcopy(original)
                self.inventory["packages"].pop(index)
                with self.assertRaisesRegex(ValueError, "missing required"):
                    self.generate()

    def test_duplicates_and_source_overlap(self):
        self.inventory["packages"].append(copy.deepcopy(self.inventory["packages"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.generate()
        self.inventory["packages"].pop()
        self.inventory["sources"][0]["filename"] = self.inventory["packages"][0]["filename"]
        with self.assertRaisesRegex(ValueError, "distinct"):
            self.generate()

    def test_changed_final_bytes_invalidate_evidence(self):
        path = self.directory / self.inventory["packages"][0]["filename"]
        path.write_bytes(b"signed or notarized bytes differ")
        with self.assertRaisesRegex(ValueError, "checksums"):
            self.generate()

    def test_extra_or_missing_digest_rejected(self):
        self.evidence["artifacts"]["unqualified.exe"] = "a" * 64
        with self.assertRaisesRegex(ValueError, "checksums"):
            self.generate()
        del self.evidence["artifacts"]["unqualified.exe"]
        self.evidence["artifacts"].pop(next(iter(self.evidence["artifacts"])))
        with self.assertRaisesRegex(ValueError, "checksums"):
            self.generate()

    def test_every_qualification_gate_required(self):
        original = copy.deepcopy(self.evidence)
        for platform, gates in MODULE.qualification.PLATFORMS.items():
            for gate in gates:
                self.evidence = copy.deepcopy(original)
                self.evidence["platforms"][platform][gate] = False
                with self.subTest(platform=platform, gate=gate), self.assertRaises(ValueError):
                    self.generate()
        for gate in MODULE.qualification.COMPATIBILITY:
            self.evidence = copy.deepcopy(original)
            self.evidence["compatibility"][gate] = False
            with self.subTest(gate=gate), self.assertRaises(ValueError):
                self.generate()

    def test_each_installed_package_and_device_target_is_required(self):
        original = copy.deepcopy(self.evidence)
        for target in MODULE.qualification.TESTED_TARGETS:
            for value in (False, "true", None):
                self.evidence = copy.deepcopy(original)
                self.evidence["testedTargets"][target] = value
                with self.subTest(target=target, value=value), self.assertRaisesRegex(ValueError, "testedTargets"):
                    self.generate()
        self.evidence = copy.deepcopy(original)
        del self.evidence["testedTargets"]
        with self.assertRaisesRegex(ValueError, "testedTargets"):
            self.generate()

    def test_source_archive_is_required(self):
        self.inventory["sources"] = []
        with self.assertRaisesRegex(ValueError, "source archive"):
            self.generate()

    def test_wrong_google_play_application_rejected(self):
        self.evidence["stores"]["googlePlay"]["url"] = "https://play.google.com/store/apps/details?id=unrelated.app"
        with self.assertRaisesRegex(ValueError, "googlePlay"):
            self.generate()
        self.evidence["stores"]["googlePlay"]["url"] = "https://play.google.com/store/apps/details?id=org.globulation2.glob2&id=unrelated.app"
        with self.assertRaisesRegex(ValueError, "googlePlay"):
            self.generate()

    def test_revision_mismatches(self):
        for container, key in ((self.inventory, "sourceCommit"), (self.evidence, "sourceCommit"), (self.evidence, "tag")):
            original = container[key]
            container[key] = "wrong"
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.generate()
            container[key] = original

    def test_store_urls_and_production_availability(self):
        original = copy.deepcopy(self.evidence)
        for channel in ("googlePlay", "appStore"):
            for value in ("https://example.org/store", "http://apps.apple.com/app/id123", "https://user:password@play.google.com/store/apps/details?id=app"):
                self.evidence = copy.deepcopy(original)
                self.evidence["stores"][channel]["url"] = value
                with self.subTest(channel=channel, value=value), self.assertRaises(ValueError):
                    self.generate()
            self.evidence = copy.deepcopy(original)
            self.evidence["stores"][channel]["production"] = False
            with self.assertRaises(ValueError):
                self.generate()

    def test_path_traversal_symlink_empty_and_wrong_extension(self):
        descriptor = self.inventory["packages"][0]
        original = descriptor["filename"]
        for value in ("../secret.apk", "/absolute.apk", "empty.apk", "linked.apk", "wrong.zip"):
            (self.directory / "empty.apk").touch()
            (self.directory / "wrong.zip").write_bytes(b"wrong")
            link = self.directory / "linked.apk"
            if not link.exists():
                link.symlink_to(self.directory / original)
            descriptor["filename"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.generate()
        descriptor["filename"] = original

    def test_review_time_and_reviewer_required(self):
        for key, value in (("reviewedBy", ""), ("reviewedAt", "2020-01-01"), ("reviewedAt", "2999-01-01T00:00:00Z"), ("evidenceUrl", "http://example.org/evidence")):
            original = self.evidence[key]
            self.evidence[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.generate()
            self.evidence[key] = original

    def test_checkout_requires_exact_commit_tag_and_version(self):
        with patch.object(MODULE.subprocess, "check_output", side_effect=[self.sha, self.sha]), patch.object(Path, "read_text", return_value='PACKAGE_VERSION = "1.2.3"\n'):
            MODULE.validate_checkout(self.tag, self.sha)
        with patch.object(MODULE.subprocess, "check_output", return_value="b" * 40):
            with self.assertRaisesRegex(ValueError, "checkout"):
                MODULE.validate_checkout(self.tag, self.sha)

    def test_malformed_nested_metadata_rejected(self):
        for key in ("platforms", "compatibility", "stores", "testedTargets"):
            original = self.evidence[key]
            self.evidence[key] = []
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.generate()
            self.evidence[key] = original
        self.inventory["packages"][0]["architecture"] = []
        with self.assertRaises(ValueError):
            self.generate()

    def test_cli_rejection_preserves_existing_snapshot(self):
        inventory = self.directory / "inventory.json"
        qualification = self.directory / "qualification.json"
        output = self.directory / "downloads-manifest.json"
        inventory.write_text(json.dumps(self.inventory))
        qualification.write_text(json.dumps({}))
        output.write_text("previous verified manifest")
        args = ["manifest", "--artifacts", str(self.directory), "--inventory", str(inventory), "--qualification", str(qualification), "--tag", self.tag, "--source-commit", self.sha, "--output", str(output)]
        with patch.object(sys, "argv", args), patch.object(MODULE, "validate_checkout"), self.assertRaises(SystemExit) as raised:
            MODULE.main()
        self.assertEqual(raised.exception.code, 1)
        self.assertEqual(output.read_text(), "previous verified manifest")

    def test_stage_inventory_maps_final_public_packages(self):
        version = self.tag[1:]
        filenames = [
            f"glob2-{version}-windows-x86_64-setup.exe",
            f"glob2-{version}-windows-x86_64.zip",
            f"Glob2-{version}-macos-arm64.dmg",
            f"Glob2-{version}-macos-x86_64.dmg",
            "glob2.flatpak",
            "globulation2_1_amd64.snap",
            f"glob2-{version}-1.fc43.x86_64.rpm",
            f"glob2-{version}-linux-x86_64-ubuntu22.04.tar.gz",
            f"glob2-{version}-android-arm64-v8a.apk",
            f"glob2-{version}-android-armeabi-v7a.apk",
            f"glob2-{version}-android-x86_64.apk",
            f"glob2-{version}.tar.gz",
        ]
        for filename in filenames:
            (self.directory / filename).write_bytes(filename.encode())
        inventory = STAGE.inventory(self.directory, self.tag, self.sha)
        self.assertEqual(inventory["schemaVersion"], 1)
        self.assertEqual(inventory["sourceCommit"], self.sha)
        self.assertEqual({(p["platform"], p["architecture"], p["format"]) for p in inventory["packages"]}, MODULE.REQUIRED)
        linux = next(p for p in inventory["packages"] if p["format"] == "tar.gz")
        self.assertIsInstance(linux["dependencies"], list)
        self.assertTrue((self.directory / f"glob2-{version}-linux-x86_64.snap").is_file())
        self.assertTrue((self.directory / f"glob2-{version}-linux-x86_64-fedora43.rpm").is_file())
        self.inventory = inventory
        self.evidence["artifacts"] = {
            descriptor["filename"]: hashlib.sha256((self.directory / descriptor["filename"]).read_bytes()).hexdigest()
            for descriptor in inventory["packages"] + inventory["sources"]
        }
        self.assertEqual(MODULE.generate(self.directory, inventory, self.evidence, self.tag, self.sha)["version"], version)

    def test_release_workflows_keep_draft_and_promotion_separate(self):
        github_release = (ROOT / ".github/workflows/github-release.yml").read_text()
        promote = (ROOT / ".github/workflows/promote-downloads.yml").read_text()
        self.assertIn("github.repository == 'genixpro/glob2-release'", github_release)
        self.assertIn("github.repository == 'genixpro/glob2-release'", promote)
        self.assertIn("environment: windows-signing", github_release)
        self.assertIn("environment: macos-developer-id", github_release)
        self.assertIn("environment: android-sideload", github_release)
        self.assertIn("environment: github-release", promote)
        self.assertIn("--publish-draft", github_release)
        self.assertIn("--draft=false", promote)
        self.assertIn("downloads_manifest.py", promote)
        self.assertNotIn("needs: [context, packages]\n    needs:", github_release)


if __name__ == "__main__":
    unittest.main()
