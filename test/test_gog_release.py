"""Checks for GOG depot integrity and the public project contract."""

import argparse
import json
import io
import stat
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from gog_pipeline import render, run_builder
from gog_release import (REQUIRED_ASSETS, extract_archive, preflight, stage_linux,
                         verify_manifest, write_manifest, write_source_offer)


class GOGReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.depot = self.root / "depot"
        self.depot.mkdir()
        for name in REQUIRED_ASSETS:
            path = self.depot / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"asset")
        (self.depot / "glob2.exe").write_bytes(b"binary")
        self.metadata = {"platform": "windows", "version": "0.9.5.0",
                         "source_commit": "a" * 40, "workflow_commit": "b" * 40}
        write_source_offer(self.depot, self.metadata["source_commit"])
        write_manifest(self.depot, self.metadata)

    def test_changed_or_unlisted_files_are_rejected(self):
        self.assertEqual(verify_manifest(self.depot, "windows"), self.metadata)
        (self.depot / "data/fonts/sans.ttf").write_bytes(b"modified")
        with self.assertRaisesRegex(ValueError, "modified"):
            verify_manifest(self.depot, "windows")
        (self.depot / "data/fonts/sans.ttf").write_bytes(b"asset")
        (self.depot / "unlisted").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "inventory"):
            verify_manifest(self.depot, "windows")

    def test_verified_webp_artwork_can_replace_png(self):
        png = self.depot / "data/gfx/ressource0.png"
        png.with_suffix(".webp").write_bytes(b"exported artwork")
        png.unlink()
        write_manifest(self.depot, self.metadata)
        self.assertEqual(verify_manifest(self.depot, "windows"), self.metadata)
        png.with_suffix(".webp").unlink()
        write_manifest(self.depot, self.metadata)
        with self.assertRaisesRegex(ValueError, "missing asset"):
            verify_manifest(self.depot, "windows")

    def test_linux_bundles_sdl3_from_original_dependency_paths(self):
        for sdl_name in ("libSDL3.so.0", "libSDL2-2.0.so.0"):
            with self.subTest(runtime=sdl_name):
                source = (self.root / sdl_name).resolve()
                source.mkdir()
                (source / "docs/assets").mkdir(parents=True)
                (source / "COPYING").write_text("game license")
                (source / "docs/assets/source-attribution.md").write_text("attribution")
                install = source / "install"
                binary = install / "usr/bin/glob2"
                binary.parent.mkdir(parents=True)
                binary.write_bytes(b"game")
                for name in REQUIRED_ASSETS:
                    asset = install / "usr/share/glob2" / name
                    asset.parent.mkdir(parents=True, exist_ok=True)
                    asset.write_bytes(b"asset")
                private = install / "usr/lib/glob2"
                private.mkdir(parents=True)
                sdl = private / sdl_name
                codec = private / "libopus.so.0"
                sdl.write_bytes(b"sdl")
                codec.write_bytes(b"opus")
                dependency_paths = {
                    binary: [(sdl_name, sdl), ("libc.so.6", source / "system-libc")],
                    sdl: [(codec.name, codec)], codec: [],
                }
                args = argparse.Namespace(stage=install, output=source / "depot",
                                          version="0.11.0.0", source_commit="a" * 40,
                                          workflow_commit="b" * 40)
                with patch("gog_release.ROOT", source), patch(
                        "gog_release.dependencies", side_effect=lambda path: dependency_paths[path]):
                    if sdl_name.startswith("libSDL2"):
                        with self.assertRaisesRegex(ValueError, "SDL3 runtime"):
                            stage_linux(args)
                    else:
                        stage_linux(args)
                        self.assertEqual(verify_manifest(args.output, "linux")["version"], "0.11.0.0")
                        self.assertEqual((args.output / "lib" / codec.name).read_bytes(), b"opus")
                        self.assertFalse((args.output / "lib/libc.so.6").exists())

    def test_project_has_one_primary_task_and_no_credentials(self):
        project = render("windows", self.depot, self.root / "project.json",
                         "0.9.5.0-r123-a1", "1234", "1234")
        data = project["project"]
        self.assertEqual(data["platform"], "windows")
        self.assertEqual(data["version"], "0.9.5.0-r123-a1")
        self.assertEqual(data["products"][0]["tasks"][0]["path"], "glob2.exe")
        self.assertEqual(data["clientSecret"], "")
        self.assertEqual(json.loads((self.root / "project.json").read_text()), project)

    def test_upload_only_targets_staging_and_requires_credentials(self):
        builder = self.root / "builder"
        builder.write_bytes(b"tool")
        with self.assertRaisesRegex(ValueError, "only the private Staging"):
            run_builder(builder, self.root / "project.json", "Master", False)

    def test_linux_launch_permission_is_required(self):
        linux = self.root / "linux"
        linux.mkdir()
        for name in REQUIRED_ASSETS:
            path = linux / "share/glob2" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"asset")
        (linux / "start.sh").write_bytes(b"#!/bin/sh\n")
        write_source_offer(linux, self.metadata["source_commit"])
        write_manifest(linux, {**self.metadata, "platform": "linux"})
        with self.assertRaisesRegex(ValueError, "not executable"):
            verify_manifest(linux, "linux")
        (linux / "start.sh").chmod(stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
        self.assertEqual(verify_manifest(linux, "linux")["platform"], "linux")

    def test_archive_cannot_escape_depot_before_credentials_are_used(self):
        archive = self.root / "evil.tar.gz"
        payload = b"bad"
        with tarfile.open(archive, "w:gz") as packed:
            member = tarfile.TarInfo("../outside")
            member.size = len(payload)
            packed.addfile(member, io.BytesIO(payload))
        with self.assertRaises(tarfile.TarError):
            extract_archive(archive, self.root / "extracted", "windows")
        self.assertFalse((self.root / "outside").exists())

    def test_public_tag_source_diff_blocks_release(self):
        tag = "v0.9.5.0"
        with patch("gog_release.command", side_effect=["a" * 40, "b" * 40, "SConstruct"]), \
             patch("gog_release.subprocess.run"), patch("gog_release.version", return_value=tag[1:]):
            with self.assertRaisesRegex(ValueError, "differs from public tag"):
                preflight(tag, f"refs/gog-upstream/{tag}")


if __name__ == "__main__":
    unittest.main()
