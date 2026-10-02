"""Package experiments preserve contents, isolate builds and reject slowdowns."""

import sys
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scons"))
from build_layout import build_identity, default_directory, prepare_directory

from tools.release.archives import MIB, choose_xz, linux_archives, zip_archive
from tools.release.benchmark_profiles import repeatable_slowdown
from tools.release.check_profile_platforms import verify


class PackageOptimizationTests(unittest.TestCase):
    def test_compiler_profiles_cannot_mix_cached_objects(self):
        identities = [
            build_identity({"release": "1", "size_optimization": profile}, host="linux")
            for profile in ("none", "gc", "lto", "size")
        ]
        self.assertEqual(len(set(map(default_directory, identities))), 4)
        with tempfile.TemporaryDirectory() as directory:
            prepare_directory(directory, identities[0])
            with self.assertRaises(ValueError):
                prepare_directory(directory, identities[1])
        for arguments in (
            {"size_optimization": "unknown"},
            {"size_optimization": "lto"},
            {"size_optimization": "size", "release": "1", "target": "web"},
        ):
            with self.assertRaises(ValueError):
                build_identity(arguments, host="linux")

    def test_archives_preserve_all_payload_bytes_and_linux_aliases(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "Globulation2"
            stage.mkdir()
            original = bytes(range(256)) * 200
            (stage / "glob2.exe").write_bytes(original)
            zip_path = root / "game.zip"
            report = zip_archive(stage, zip_path)
            with zipfile.ZipFile(zip_path) as archive:
                self.assertEqual(archive.read("Globulation2/glob2.exe"), original)
            self.assertEqual(
                zip_path.stat().st_size, min(report["bytes_by_level"].values())
            )
            (stage / "library.so").symlink_to("glob2.exe")
            linux_archives(stage, root / "game.tar.gz")
            with tarfile.open(root / "game.tar.gz") as archive:
                self.assertEqual(archive.extractfile("glob2.exe").read(), original)
                self.assertTrue(archive.getmember("library.so").issym())
                self.assertEqual(archive.getmember("library.so").linkname, "glob2.exe")

    def test_archive_adoption_requires_meaningful_savings(self):
        self.assertIsNone(choose_xz(10 * MIB, 10 * MIB, 10 * MIB))
        self.assertEqual(choose_xz(10 * MIB, 9 * MIB, 9 * MIB - 100), 6)
        self.assertEqual(choose_xz(10 * MIB, 9 * MIB, 8 * MIB), 9)

    def test_cross_platform_evidence_requires_all_profiles_and_matching_tick_traces(
        self,
    ):
        with tempfile.TemporaryDirectory() as temporary:
            roots = [Path(temporary) / name for name in ("Linux", "Windows")]
            for root in roots:
                for profile in ("gc", "lto", "size"):
                    folder = root / profile
                    folder.mkdir(parents=True)
                    checks = {"game.replay.checksums": "same trace"}
                    report = dict(
                        profile=profile,
                        platform=root.name,
                        architecture="x86_64",
                        revision="source",
                        scenarios={
                            "fixture": dict(
                                fixture_sha256="fixture bytes",
                                checksums=dict(baseline=checks, candidate=checks),
                            )
                        },
                    )
                    (folder / "measurements.json").write_text(json.dumps(report))
            self.assertEqual(set(verify(roots)["platforms"]), {"Linux", "Windows"})
            path = roots[1] / "size/measurements.json"
            changed = json.loads(path.read_text())
            changed["scenarios"]["fixture"]["checksums"]["candidate"][
                "game.replay.checksums"
            ] = "diverged"
            path.write_text(json.dumps(changed))
            with self.assertRaisesRegex(ValueError, "per-tick"):
                verify(roots)

    def test_repeated_slowdown_requires_evidence_in_both_batches(self):
        self.assertTrue(
            repeatable_slowdown(
                [(batch, 1, 1.02) for batch in range(2) for _ in range(7)]
            )
        )
        self.assertFalse(
            repeatable_slowdown(
                [
                    (batch, 1, 1.02 if batch == 0 else 0.98)
                    for batch in range(2)
                    for _ in range(7)
                ]
            )
        )
        self.assertFalse(
            repeatable_slowdown(
                [(batch, 1, 0.99) for batch in range(2) for _ in range(7)]
            )
        )


if __name__ == "__main__":
    unittest.main()
