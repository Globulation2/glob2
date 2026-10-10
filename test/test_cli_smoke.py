#!/usr/bin/env python3
"""Production CLI contracts. Set GLOB2_CLI_BINARY or use --binary; never use a mock."""

import argparse
import gzip
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from check_javascript import complete_ticks

ROOT = Path(__file__).resolve().parents[1]
BINARY = os.environ.get("GLOB2_CLI_BINARY")
ARTIFACTS = None


class CliSmoke(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY:
            raise unittest.SkipTest("set GLOB2_CLI_BINARY to a production client")
        cls.binary = Path(BINARY).resolve()
        if not cls.binary.is_file():
            raise AssertionError(f"client binary does not exist: {cls.binary}")

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="glob2-cli-")
        self.addCleanup(self.temporary.cleanup)
        self.root = (
            (ARTIFACTS / self._testMethodName)
            if ARTIFACTS
            else Path(self.temporary.name)
        )
        self.root.mkdir(parents=True, exist_ok=True)
        self.calls = 0

    def command(self, *args, status=0):
        env = dict(
            os.environ,
            GLOB2_USER_DIR=str(self.root / "profile"),
            GLOB2_USER_DATA_DIR=str(self.root / "profile"),
            SDL_VIDEODRIVER="dummy",
            SDL_AUDIODRIVER="dummy",
        )
        self.calls += 1
        result = subprocess.run(
            [str(self.binary), *map(str, args)],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            timeout=180,
        )
        (self.root / f"command-{self.calls}.json").write_text(
            json.dumps(
                {
                    "args": list(map(str, args)),
                    "status": result.returncode,
                    "stdout": result.stdout,
                    "stderr": result.stderr,
                },
                indent=2,
            )
        )
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        return result

    def generated(self):
        path = self.root / "source.map"
        self.command(
            "map",
            "generate",
            "river",
            "--width",
            "128",
            "--height",
            "128",
            "--teams",
            "2",
            "--seed",
            "713",
            "--output",
            path,
            "--report-file",
            self.root / "map.json",
        )
        self.assertTrue(Path(str(path) + ".gz").is_file())
        report = json.loads((self.root / "map.json").read_text())
        self.assertIsInstance(report, dict)
        return Path(str(path) + ".gz")

    def game(self, output, source, workers=1, saved=False, extra=()):
        args = [
            "game",
            "run",
            "--output-dir",
            output,
            "--ticks",
            "64",
            "--compute-threads",
            str(workers),
            "--telemetry",
            "checksums",
            "--write-replay",
            "--save",
            "final",
        ]
        args += (
            ["--load-game", source]
            if saved
            else [
                "--map-file",
                source,
                "--game-seed",
                "713",
                "--player",
                "castor",
                "--player",
                "cortex",
            ]
        )
        self.command(*args, *extra)
        result = json.loads((output / "result.json").read_text())
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["ticks"], 64)
        self.assertEqual(len(result["teams"]), 2)
        records = complete_ticks((output / "game.replay.checksums").read_bytes())
        self.assertTrue(records)
        return records

    def test_every_static_help_is_asset_free_and_complete(self):
        empty = Path(self.temporary.name) / "empty"
        empty.mkdir()
        env = dict(
            os.environ,
            HOME=str(empty / "home"),
            GLOB2_USER_DIR=str(empty / "profile"),
            GLOB2_USER_DATA_DIR=str(empty / "profile"),
            GLOB2_ASSET_DIR=str(empty / "missing-assets"),
            SDL_VIDEODRIVER="invalid-no-display",
            SDL_AUDIODRIVER="invalid-no-audio",
        )

        def static(*args):
            result = subprocess.run(
                [str(self.binary), *args],
                cwd=empty,
                env=env,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                list(empty.iterdir()),
                [],
                "static help initialized a profile or artifact",
            )
            return result.stdout

        tree = json.loads(static("help", "--format=json"))
        self.assertEqual((tree["schema_version"], tree["cli_version"]), (1, 2))
        self.assertTrue(tree["commands"])
        static("-h")
        static("--help")
        static("map")
        static("online")
        for command in tree["commands"]:
            path = command["path"].split()
            self.assertIn(command["description"], static(*path, "--help"))
            subtree = json.loads(static("help", *path, "--format", "json"))
            self.assertEqual(subtree["commands"], [command])
            if command["path"] != "help":
                self.assertEqual(
                    json.loads(static(*path, "--help", "--format=json"))["commands"],
                    [command],
                )
        static("map", "generate", "--generator-package", "missing.json", "--help")
        for shell in ("bash", "zsh", "fish"):
            self.assertIn("Generated from Glob2 CLI 2", static("completion", shell))

    @unittest.skipUnless(shutil.which("bash"), "Bash completion check")
    def test_bash_completion_commands_enums_equals_and_paths(self):
        script = self.command("completion", "bash").stdout
        source = self.root / "completion.bash"
        source.write_text(script, encoding="utf-8", newline="\n")

        def complete(words):
            quoted = " ".join(__import__("shlex").quote(word) for word in words)
            code = (
                'source "$1"; COMP_WORDS=('
                + quoted
                + "); COMP_CWORD="
                + str(len(words) - 1)
                + '; _glob2_complete; printf "%s\\n" "${COMPREPLY[@]}"'
            )
            result = subprocess.run(
                [shutil.which("bash"), "-s", "--", source.name],
                input=code.encode("utf-8"),
                cwd=self.root,
                capture_output=True,
                timeout=10,
            )
            stdout = result.stdout.decode("utf-8", errors="replace")
            stderr = result.stderr.decode("utf-8", errors="replace")
            self.assertEqual(result.returncode, 0, stdout + stderr)
            return stdout.splitlines()

        self.assertIn("map", complete(["glob2", "m"]))
        self.assertIn("generate", complete(["glob2", "map", "g"]))
        self.assertIn("--window-size", complete(["glob2", "play", "--window"]))
        self.assertEqual(complete(["glob2", "play", "--renderer", "s"]), ["software"])
        self.assertEqual(
            complete(["glob2", "play", "--renderer=s"]), ["--renderer=software"]
        )
        (self.root / "path with spaces").mkdir()
        (self.root / "file with spaces.replay").touch()
        self.assertIn(
            "path with spaces", complete(["glob2", "play", "--data-dir", "path"])
        )
        self.assertIn("file with spaces.replay", complete(["glob2", "replay", "file"]))

    def test_generated_reference_matches_binary(self):
        result = subprocess.run(
            [
                __import__("sys").executable,
                str(ROOT / "tools/cli_reference.py"),
                "--binary",
                str(self.binary),
                "--check",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unified_compute_pool_preserves_trace_with_auto_sizing(self):
        source = self.generated()
        serial = self.game(self.root / "serial", source, workers=1)
        automatic = self.game(self.root / "auto", source, workers="auto")
        shared = self.game(self.root / "shared", source, workers=2)
        self.assertEqual(serial, automatic)
        self.assertEqual(serial, shared)
        report = json.loads((self.root / "auto/result.json").read_text())
        self.assertGreaterEqual(report["compute_resolved_threads"], 1)
        self.assertEqual(report["compute_requested_threads"], "auto")
        self.assertIn(
            report["compute_threads"], (1, report["compute_resolved_threads"])
        )
        self.assertEqual(report["compute_workers"], report["compute_threads"] - 1)
        self.assertEqual(report["gradient_workers"], report["compute_threads"] - 1)
        self.assertNotIn("compute_experiments", report)

    def test_removed_compute_options_point_to_unified_setting(self):
        for flag, value in (
            ("--ai-threads", "2"),
            ("--gradient-workers", "2"),
            ("--compute-experiments", "ai"),
        ):
            for prefix, status in (
                ([], 2),
                (["game", "run"], 2),
                (["match", "verify", "missing.record"], 2),
                (["online", "turn-client", "missing.json"], 2),
            ):
                with self.subTest(flag=flag, command=prefix):
                    result = self.command(*prefix, flag, value, status=status)
                    self.assertIn("has been removed", result.stderr)
                    self.assertIn("--compute-threads auto|N", result.stderr)

    def test_match_verification_preserves_trace_across_compute_sizes(self):
        reference = None
        # Git may check out the golden text with CRLF on Windows.
        golden = (
            ROOT / "test/fixtures/multiplayer/FourSquares1.verify-trace.txt"
        ).read_text(encoding="utf-8")
        for count in (1, 2, 4, 8, "auto"):
            output = self.root / f"verify-{count}"
            self.command(
                "match",
                "verify",
                ROOT / "test/fixtures/multiplayer/FourSquares1.g2mr",
                "--map-file",
                ROOT / "maps/FourSquares1.map.gz",
                "--output-dir",
                output,
                "--compute-threads",
                count,
            )
            self.assertEqual(
                (output / "checksums.txt").read_text(encoding="utf-8"), golden
            )
            result = (output / "result.json").read_bytes()
            if reference is None:
                reference = result
            self.assertEqual(result, reference)
            compute = json.loads((output / "compute.json").read_text())
            self.assertEqual(compute["compute_requested_threads"], str(count))
            self.assertIn(
                compute["compute_threads"], (1, compute["compute_resolved_threads"])
            )
            self.assertEqual(compute["compute_workers"], compute["compute_threads"] - 1)

    def test_help_and_catalog_describe_real_commands(self):
        self.assertIn("game repeat", self.command("--help").stdout)
        catalog = json.loads(self.command("info", "catalog", "--format", "json").stdout)
        self.assertIn("game", catalog["commands"])
        self.assertIn("checksums", catalog["telemetry"])
        self.assertGreater(len(catalog["ais"]), 5)
        self.assertIn("river", self.command("map", "generators").stdout)

    def test_invalid_map_arguments_fail_before_writing_outputs(self):
        for args in [
            ("map", "generate", "missing-generator"),
            ("map", "generate", "river", "--width"),
            ("map", "generate", "river", "--width", "not-a-number"),
        ]:
            with self.subTest(args=args):
                self.command(*args, status=2)
        self.assertFalse((self.root / "source.map.gz").exists())

    def test_headless_numeric_and_missing_input_errors_are_structured(self):
        for index, extra in enumerate(
            [
                ["--ticks", "0"],
                ["--compute-threads", "-1"],
                ["--compute-threads", "4294967296"],
                [
                    "--map-file",
                    self.root / "missing.map",
                    "--game-seed",
                    "713",
                    "--player",
                    "castor",
                ],
            ]
        ):
            output = self.root / f"invalid-{index}"
            self.command("game", "run", "--output-dir", output, *extra, status=2)
            if index < 3:
                self.assertFalse(
                    output.exists(), "syntax errors must not create job artifacts"
                )
            else:
                report = json.loads((output / "result.json").read_text())
                self.assertEqual(report["status"], "invalid_request")
                self.assertTrue(report["diagnostic"])

    def test_probability_rule_persists_across_save_reload_and_rejects_ambiguous_thresholds(
        self,
    ):
        source = self.generated()
        output = self.root / "probability"
        first = self.game(
            output, source, extra=("--win-probability", "970", "--save", "every:32")
        )
        report = json.loads((output / "result.json").read_text())
        self.assertIn(7, report["resolved"]["winning_conditions"])
        continuation = self.game(
            self.root / "probability-checkpoint",
            output / "checkpoint-32.game.gz",
            saved=True,
        )
        self.assertEqual(
            continuation,
            {tick: record for tick, record in first.items() if tick in continuation},
        )
        self.assertEqual(len(continuation), 32)
        resumed = self.root / "probability-resumed"
        self.command(
            "game",
            "run",
            "--load-game",
            output / "final.game.gz",
            "--ticks",
            "96",
            "--output-dir",
            resumed,
            "--telemetry",
            "checksums",
        )
        loaded = json.loads((resumed / "result.json").read_text())
        self.assertIn(7, loaded["resolved"]["winning_conditions"])
        self.assertEqual(loaded["ticks"], 96)
        for index, threshold in enumerate(("0", "500", "1001")):
            invalid = self.root / f"probability-invalid-{index}"
            self.command(
                "game",
                "run",
                "--map-file",
                source,
                "--player",
                "castor",
                "--player",
                "cortex",
                "--ticks",
                "64",
                "--win-probability",
                threshold,
                "--output-dir",
                invalid,
                status=2,
            )
            self.assertFalse(
                invalid.exists(), "invalid thresholds fail before creating the job"
            )

    def test_corrupt_saved_game_fails_with_structured_diagnostic(self):
        source = self.root / "corrupt.game"
        source.write_bytes(b"not a saved game")
        output = self.root / "corrupt-run"
        result = subprocess.run(
            [
                str(self.binary),
                "game",
                "run",
                "--load-game",
                str(source),
                "--ticks",
                "64",
                "--output-dir",
                str(output),
            ],
            cwd=ROOT,
            env=dict(
                os.environ,
                GLOB2_USER_DIR=str(self.root / "profile"),
                GLOB2_USER_DATA_DIR=str(self.root / "profile"),
            ),
            capture_output=True,
            text=True,
            timeout=180,
        )
        self.assertNotEqual(result.returncode, 0)
        report = json.loads((output / "result.json").read_text())
        self.assertNotEqual(report["status"], "completed")
        self.assertTrue(report["diagnostic"])

    def test_generated_map_image_export_import_and_preview_are_readable(self):
        source = self.generated()
        image = self.root / "map.png"
        self.command("map", "export-image", source, "--output", image)
        self.assertEqual(image.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
        imported = self.root / "imported.map"
        self.command(
            "map",
            "import-image",
            image,
            "--output",
            imported,
            "--teams",
            "2",
            "--width",
            "128",
            "--height",
            "128",
            "--preview",
            self.root / "preview.png",
        )
        self.assertGreater(
            len(gzip.decompress(Path(str(imported) + ".gz").read_bytes())), 1000
        )
        self.assertEqual(
            (self.root / "preview.png").read_bytes()[:8], b"\x89PNG\r\n\x1a\n"
        )

    def test_structured_map_study_writes_report_map_and_manifest(self):
        output = self.root / "study"
        self.command(
            "map",
            "study",
            "2",
            "--output-dir",
            output,
            "--seed",
            "713",
            "--set",
            "width=7",
            "--set",
            "height=7",
            "--set",
            "teams=2",
            "--write-map",
            "--report",
            "terrain",
        )
        report = json.loads((output / "result.json").read_text())
        self.assertEqual(report["status"], "completed")
        self.assertFalse(
            (self.root / "profile").exists(),
            "headless commands must use their output profile, not inherited user data",
        )
        self.assertTrue(list(output.glob("map-r*.map.gz")))
        self.assertGreater((output / "terrain.txt").stat().st_size, 100)
        self.assertIsInstance(json.loads((output / "artifacts.json").read_text()), dict)

    def test_experimental_catalog_survives_generated_map_and_saved_continuation(self):
        # Exercise the production argument forwarding with a nonnumeric path
        # containing spaces, then remove it so reloads must use embedded rules.
        catalog_dir = self.root / "custom building catalog"
        shutil.copytree(ROOT / "data/buildings", catalog_dir)
        example = ROOT / "test/fixtures/building-catalog/authoring"
        shutil.copy2(example / "field-kitchen.json", catalog_dir)
        manifest_path = catalog_dir / "manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["catalogKey"] = "cli-field-kitchens"
        manifest["files"].append("field-kitchen.json")
        manifest["experiments"].extend(
            json.loads((example / "manifest.json").read_text())["experiments"]
        )
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

        output = self.root / "generated-game"
        self.command(
            "game",
            "run",
            "--building-catalog",
            manifest_path,
            "--generator",
            "15",
            "--map-seed",
            "42",
            "--set",
            "teams=2",
            "--set",
            "width=7",
            "--set",
            "height=7",
            "--game-seed",
            "713",
            "--player",
            "castor",
            "--player",
            "cortex",
            "--experiment",
            "field-kitchens",
            "--ticks",
            "64",
            "--compute-threads",
            "1",
            "--telemetry",
            "checksums",
            "--write-replay",
            "--save",
            "every:32",
            "--save",
            "final",
            "--output-dir",
            output,
        )
        report = json.loads((output / "result.json").read_text())
        self.assertEqual(report["status"], "completed")
        self.assertEqual(report["ticks"], 64)
        self.assertEqual(report["resolved"]["experiments"], ["field-kitchens"])
        original = complete_ticks((output / "game.replay.checksums").read_bytes())
        self.assertEqual(len(original), 64)
        maps = list((output / "generated").glob("map-r*.map.gz"))
        self.assertEqual(len(maps), 1)
        shutil.rmtree(catalog_dir)

        # Stock startup does not declare field-kitchens. Accepting the requested
        # gate here proves the generated map retained its catalog metadata.
        from_map = self.root / "embedded-map"
        reopened = self.game(
            from_map, maps[0], extra=("--experiment", "field-kitchens")
        )
        self.assertEqual(reopened, original)
        self.assertEqual(
            json.loads((from_map / "result.json").read_text())["resolved"][
                "experiments"
            ],
            ["field-kitchens"],
        )

        # A save additionally retains its enabled selection without a CLI gate.
        from_save = self.root / "embedded-save"
        resumed = self.game(from_save, output / "checkpoint-32.game.gz", saved=True)
        self.assertEqual(len(resumed), 32)
        self.assertEqual(
            resumed,
            {tick: record for tick, record in original.items() if tick in resumed},
        )
        self.assertEqual(
            json.loads((from_save / "result.json").read_text())["resolved"][
                "experiments"
            ],
            ["field-kitchens"],
        )

    def test_headless_workers_and_saved_continuation_match_complete_tick_records(self):
        source = self.generated()
        first = self.game(
            self.root / "one-worker", source, extra=["--save", "every:32"]
        )
        parallel = self.game(
            self.root / "four-workers", source, workers=4, extra=["--save", "every:32"]
        )
        self.assertEqual(first, parallel)
        resumed = self.game(
            self.root / "resumed",
            self.root / "one-worker/checkpoint-32.game.gz",
            saved=True,
        )
        self.assertEqual(
            resumed, {tick: record for tick, record in first.items() if tick in resumed}
        )
        self.assertEqual(len(resumed), 32)
        for file in ("game.replay", "final.game.gz"):
            self.assertEqual(
                (self.root / "one-worker" / file).read_bytes(),
                (self.root / "four-workers" / file).read_bytes(),
            )
        (self.root / "parity.json").write_text(
            json.dumps(
                {
                    "ticks": 64,
                    "workers": [1, 4],
                    "checkpoint": 32,
                    "records_sha256": hashlib.sha256(
                        b"".join(first.values())
                    ).hexdigest(),
                },
                indent=2,
            )
        )


class JunitResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.report = ET.Element("testsuites")
        self.suite = ET.SubElement(self.report, "testsuite", name="CliSmoke")

    def record(self, test, kind=None, text=""):
        row = ET.SubElement(
            self.suite, "testcase", classname="CliSmoke", name=str(test)
        )
        if kind:
            ET.SubElement(row, kind).text = text

    def addSuccess(self, test):
        super().addSuccess(test)
        self.record(test)

    def addFailure(self, test, error):
        super().addFailure(test, error)
        self.record(test, "failure", self._exc_info_to_string(error, test))

    def addError(self, test, error):
        super().addError(test, error)
        self.record(test, "error", self._exc_info_to_string(error, test))

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self.record(test, "skipped", reason)

    def addSubTest(self, test, subtest, error):
        super().addSubTest(test, subtest, error)
        if error:
            self.record(subtest, "failure", self._exc_info_to_string(error, test))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    parser.add_argument("--junit", type=Path)
    args = parser.parse_args()
    BINARY = str(args.binary)
    ARTIFACTS = args.artifacts.resolve() if args.artifacts else None
    result = unittest.TextTestRunner(verbosity=2, resultclass=JunitResult).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(CliSmoke)
    )
    if args.junit:
        args.junit.parent.mkdir(parents=True, exist_ok=True)
        ET.ElementTree(result.report).write(
            args.junit, encoding="utf-8", xml_declaration=True
        )
    raise SystemExit(not result.wasSuccessful())
