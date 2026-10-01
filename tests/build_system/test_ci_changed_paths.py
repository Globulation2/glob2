"""Guard the CI path rules against accidental gaps in platform coverage."""

import importlib.util
from pathlib import Path
import re
import subprocess
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / ".github/scripts/ci_changed_paths.py"
SPEC = importlib.util.spec_from_file_location("ci_changed_paths", SCRIPT)
ci_changed_paths = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ci_changed_paths)


class ChangedPathsTest(unittest.TestCase):
    def assert_jobs(self, paths, **expected):
        self.assertEqual(ci_changed_paths.classify(paths), expected)

    def test_documentation_only(self):
        self.assert_jobs(
            ["docs/browser/implementation.md", "README.md", "browser/README.md"],
            native=False, browser=False, map_generators=False,
            deployment=False, cross_platform=False,
        )

    def test_browser_shell_only(self):
        self.assert_jobs(
            ["browser/storage.js", "browser/tests/storage.spec.js"],
            native=False, browser=True, map_generators=False,
            deployment=False, cross_platform=False,
        )

    def test_native_test_only(self):
        self.assert_jobs(
            ["test/PathGradientHarness.cpp"],
            native=True, browser=False, map_generators=False,
            deployment=False, cross_platform=False,
        )

    def test_shared_scripting_changes_run_native_browser_and_comparison(self):
        for path in ("test/ScriptRuntimeTest.cpp", "test/ScriptSimulationTest.cpp",
                     "test/ScriptEditorTest.cpp", "test/support/ScriptCorpus.h",
                     "test/fixtures/javascript/numeric-corpus.js",
                     "test/fixtures/javascript/profile1-initial.game.gz",
                     "test/check_javascript_corpus.py", "test/check_javascript.py",
                     "test/check_javascript_evidence.py", "test/build_provenance.py",
                     "test/support/TestMain.cpp"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=True, browser=True, map_generators=False,
                    deployment=False, cross_platform=True,
                )

    def test_golden_table_only_runs_golden_job(self):
        self.assert_jobs(
            ["test/map-generator-golden.txt"],
            native=False, browser=False, map_generators=True,
            deployment=False, cross_platform=False,
        )

    def test_transport_fixture_only_runs_browser_jobs(self):
        for path in ("test/NetConnectionHarness.cpp", "test/NativeMultiplayerPeer.cpp",
                     "test/WssTransportHarness.cpp", "test/run-network-transport-tests.py"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=False, browser=True, map_generators=False,
                    deployment=False, cross_platform=False,
                )

    def test_shared_source_runs_everything(self):
        self.assert_jobs(
            ["src/map/Map.cpp"],
            native=True, browser=True, map_generators=True,
            deployment=True, cross_platform=True,
        )

    def test_cross_platform_fixtures_run_everything(self):
        for path in ("browser/tests/determinism.spec.js", "browser/tests/fixtures/cross-replay.replay",
                     "test/run-browser-determinism.py", "test/MapGeneratorGoldenTest.cpp",
                     ".github/workflows/build.yml"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=True, browser=True,
                    map_generators=True, deployment=True, cross_platform=True,
                )

    def test_mixed_independent_changes(self):
        self.assert_jobs(
            ["browser/storage.js", "test/PathGradientHarness.cpp"],
            native=True, browser=True, map_generators=False,
            deployment=False, cross_platform=False,
        )

    def test_ai_and_ui_changes_skip_map_sweep_and_deployment(self):
        for path in ("src/ai/AI.cpp", "src/gui/GameGUIInput.cpp", "src/render/GameAnimations.cpp"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=True, browser=True, map_generators=False,
                    deployment=False, cross_platform=True,
                )

    def test_network_changes_keep_deployment(self):
        self.assert_jobs(
            ["src/yog/YOGServer.cpp", "src/net/Connection.cpp"],
            native=True, browser=True, map_generators=False,
            deployment=True, cross_platform=True,
        )

    def test_deployment_changes_only_run_browser_and_deployment(self):
        self.assert_jobs(
            ["deploy/compose.yaml", "tests/deployment/test_compose.py"],
            native=False, browser=True, map_generators=False,
            deployment=True, cross_platform=False,
        )

    def test_empty_diff_runs_everything(self):
        self.assert_jobs(
            [], native=True, browser=True, map_generators=True, cross_platform=True,
            deployment=True,
        )

    def test_downloaded_native_archives_do_not_dirty_source_provenance(self):
        root = SCRIPT.parents[2]
        workflow = (root / ".github/workflows/build.yml").read_text()
        archives = re.findall(r"run: tar -xzf (\S+)", workflow)
        self.assertTrue(archives, "No native archive consumers found")
        for archive in archives:
            with self.subTest(archive=archive):
                result = subprocess.run(
                    ["git", "check-ignore", "--no-index", archive], cwd=root,
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0,
                                 f"Downloaded archive would become a source input: {archive}")


if __name__ == "__main__":
    unittest.main()
