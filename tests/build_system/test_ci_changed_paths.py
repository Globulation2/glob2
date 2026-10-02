"""Guard the CI path rules against accidental gaps in platform coverage."""

import importlib.util
from pathlib import Path
import re
import subprocess
from types import SimpleNamespace
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / ".github/scripts/ci_changed_paths.py"
SPEC = importlib.util.spec_from_file_location("ci_changed_paths", SCRIPT)
ci_changed_paths = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ci_changed_paths)


class ChangedPathsTest(unittest.TestCase):
    def assert_jobs(self, paths, platform=None, platform_stack=None, **expected):
        # The TypeScript platform job runs on platform/ changes and with full CI.
        if platform is None:
            platform = all(expected.values())
        # The self-hosted stack smoke test runs on its own paths and with full CI.
        if platform_stack is None:
            platform_stack = not paths
        self.assertEqual(ci_changed_paths.classify(paths),
                         {**expected, "platform": platform, "platform_stack": platform_stack})

    def test_platform_stack_paths(self):
        for path in ("deploy/compose.yaml", "deploy/Caddyfile", "tests/deployment/platform_stack_smoke.py"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=False, browser=True, map_generators=False,
                    deployment=True, cross_platform=False, platform_stack=True,
                )
        self.assert_jobs(
            ["src/relay/RelayServer.cpp"], native=False, browser=True, map_generators=False,
            deployment=False, cross_platform=False, platform_stack=True,
        )
        self.assert_jobs(
            ["platform/packages/db/migrations/0005_rooms.sql"], native=False, browser=False,
            map_generators=False, deployment=False, cross_platform=False, platform=True,
            platform_stack=True,
        )
        # Engine changes rely on full CI for the stack, not every pull request.
        self.assert_jobs(
            ["src/map/Map.cpp", "deploy/Caddyfile"], native=True, browser=True, map_generators=True,
            deployment=True, cross_platform=True, platform_stack=True,
        )

    def test_platform_only(self):
        self.assert_jobs(
            ["platform/apps/api/src/app.ts", "platform/package-lock.json"],
            native=False, browser=False, map_generators=False,
            deployment=False, cross_platform=False, platform=True,
            # Dependency changes also rebuild the stack's images.
            platform_stack=True,
        )

    def test_platform_docs_only(self):
        self.assert_jobs(
            ["platform/README.md", "docs/multiplayer/architecture.md"],
            native=False, browser=False, map_generators=False,
            deployment=False, cross_platform=False, platform=False,
        )

    def test_protocol_fixtures_run_platform_and_native_contract_tests(self):
        self.assert_jobs(
            ["platform/packages/protocol/fixtures/valid/MatchSetup/catalog-1v1.json"],
            native=True, browser=False, map_generators=False,
            deployment=False, cross_platform=False, platform=True,
        )

    def test_documentation_only(self):
        self.assert_jobs(
            ["docs/browser/implementation.md", "README.md", "browser/README.md"],
            native=False, browser=False, map_generators=False,
            deployment=False, cross_platform=False,
        )

    def test_ci_tool_tests_only_run_selector_contracts(self):
        for path in ci_changed_paths.CI_TOOL_TESTS:
            with self.subTest(path=path):
                self.assert_jobs([path], native=False, browser=False,
                                 map_generators=False, deployment=False, cross_platform=False)
        # Actual runner changes still exercise native execution, and mixed
        # production changes must never inherit the test-only shortcut.
        self.assert_jobs(["test/test_run_tests.py", "src/map/Map.cpp"],
                         native=True, browser=True, map_generators=True,
                         deployment=True, cross_platform=True)
        self.assert_jobs(["test/run_tests.py"], native=True, browser=False,
                         map_generators=False, deployment=False, cross_platform=False)

    def test_lightweight_suites_are_executed_and_packaging_paths_remain(self):
        root = SCRIPT.parents[2]
        workflow = (root / ".github/workflows/build.yml").read_text()
        selector = workflow.split("  changes:\n", 1)[1].split("\n  linux-build:", 1)[0]
        for filename in ("test_run_tests.py", "test_ci_failure_aggregation.py", "test_ci_changed_paths.py"):
            self.assertIn(filename, selector)
        package = (root / ".github/workflows/steam-windows-package.yml").read_text()
        paths = package.split("    paths:\n", 1)[1].split("\npermissions:", 1)[0]
        self.assertNotIn("docs/development/reference.md", paths)
        self.assertIn("cancel-in-progress: ${{ github.event_name == 'pull_request' }}", package)
        for filename in ("tools/package_steam_windows.py", "test/test_steam_windows_package.py",
                         ".github/workflows/steam-windows-package.yml"):
            self.assertIn(filename, paths)

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
                     "test/support/TestMain.cpp", "test/ImageAssetTest.cpp"):
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

    def test_relay_changes_run_the_relay_job_only(self):
        for path in ("src/relay/RelayServer.cpp", "tests/relay/test_relay.py", "test/relay/RelayTicketTest.cpp",
                     "test/fixtures/relay-tickets/valid.jwt"):
            with self.subTest(path=path):
                self.assert_jobs(
                    [path], native=False, browser=True, map_generators=False,
                    deployment=False, cross_platform=False, platform_stack=path.startswith("src/relay/"),
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

    def test_drawing_implementations_keep_all_platforms_without_map_sweeps(self):
        for path in ci_changed_paths.RENDER_IMPLEMENTATIONS:
            with self.subTest(path=path):
                self.assert_jobs([path], native=True, browser=True, map_generators=False,
                                 deployment=False, cross_platform=True)
        for paths in (["libgag/include/RenderBackend.h"], ["libgag/src/FileManager.cpp"],
                      ["libgag/src/SurfaceRaster.cpp", "src/map/generator/core/MapGenerator.cpp"],
                      ["libgag/src/UnknownRenderer.cpp"]):
            with self.subTest(paths=paths):
                self.assert_jobs(paths, native=True, browser=True, map_generators=True,
                                 deployment=True, cross_platform=True)

    def test_network_changes_keep_deployment(self):
        self.assert_jobs(
            ["src/yog/YOGServer.cpp", "src/net/Connection.cpp"],
            native=True, browser=True, map_generators=False,
            deployment=True, cross_platform=True,
        )

    def test_deployment_changes_only_run_browser_and_deployment(self):
        self.assert_jobs(
            ["deploy/compose.legacy.yaml", "tests/deployment/test_compose.py"],
            native=False, browser=True, map_generators=False,
            deployment=True, cross_platform=False, platform_stack=True,
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

    def test_workspace_compiler_caches_do_not_dirty_source_provenance(self):
        root = SCRIPT.parents[2]
        workflow = (root / ".github/workflows/build.yml").read_text()
        caches = re.findall(r"CCACHE_DIR:\s*\$\{\{ github.workspace \}\}([^\n]+)", workflow)
        self.assertTrue(caches, "No workspace compiler cache configuration found")
        for cache in caches:
            path = cache.strip().replace("\\", "/").lstrip("/") + "/probe"
            with self.subTest(path=path):
                result = subprocess.run(
                    ["git", "check-ignore", "--no-index", path], cwd=root,
                    capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 0,
                                 f"Generated compiler cache would become a source input: {path}")

    def test_windows_cache_writes_require_authorized_event_and_successful_build(self):
        workflow = (SCRIPT.parents[2] / ".github/workflows/build.yml").read_text()
        for job, build in (("windows", "build_glob2_and_the_regression_harnesses"),
                           ("windows-server", "build_the_yog_server")):
            block = workflow.split(f"  {job}:\n", 1)[1].split("\n  # The browser checks", 1)[0]
            if job == "windows":
                block = block.split("\n  windows-server:", 1)[0]
            for step in ("Drop cache entries this run did not use", "Save the compiler cache"):
                guard = re.search(r"      - name: " + re.escape(step) +
                                  r"\n.*?        if: \$\{\{ (.*?) \}\}", block, re.S).group(1)
                expected = ("!cancelled() && steps.cache_ready.outcome == 'success' && "
                            f"steps.{build}.outcome == 'success' && "
                            "((github.event_name == 'push' && github.ref == 'refs/heads/master') "
                            "|| github.event_name == 'workflow_dispatch')")
                self.assertEqual(guard, expected)
                expression = guard.replace("!cancelled()", "not cancelled").replace("&&", "and").replace("||", "or")
                for event, ref, built, cancelled, allowed in (
                    ("pull_request", "refs/pull/478/merge", "success", False, False),
                    ("push", "refs/heads/codex/javascript-foundation", "success", False, False),
                    ("workflow_dispatch", "refs/heads/codex/javascript-foundation", "success", False, True),
                    ("push", "refs/heads/master", "success", False, True),
                    ("workflow_dispatch", "refs/heads/master", "failure", False, False),
                    ("workflow_dispatch", "refs/heads/master", "success", True, False),
                ):
                    with self.subTest(job=job, step=step, event=event, built=built, cancelled=cancelled):
                        context = {
                            "cancelled": cancelled,
                            "github": SimpleNamespace(event_name=event, ref=ref),
                            "steps": SimpleNamespace(cache_ready=SimpleNamespace(outcome="success"),
                                                     **{build: SimpleNamespace(outcome=built)}),
                        }
                        # Evaluate only the exact, asserted expression above.
                        self.assertEqual(eval(expression, {"__builtins__": {}}, context), allowed)

    def test_windows_git_newline_policy_is_pinned_before_cache_and_build(self):
        workflow = (SCRIPT.parents[2] / ".github/workflows/build.yml").read_text()
        for job, build in (("windows", "Build glob2 and the regression harnesses"),
                           ("windows-server", "Build the YOG server")):
            block = workflow.split(f"  {job}:\n", 1)[1].split("\n  # The browser checks", 1)[0]
            if job == "windows":
                block = block.split("\n  windows-server:", 1)[0]
            with self.subTest(job=job):
                checkout = block.index("      - uses: actions/checkout@v4")
                pin = block.index("        run: git config --local core.autocrlf true")
                cache = block.index("      - name: Restore the compiler cache")
                compile = block.index(f"      - name: {build}")
                self.assertLess(checkout, pin)
                self.assertLess(pin, cache)
                self.assertLess(cache, compile)
                self.assertEqual(block.count("git config --local core.autocrlf true"), 1)

    def test_linux_platform_tests_depend_only_on_their_own_build(self):
        root = SCRIPT.parents[2]
        workflow = (root / '.github/workflows/build.yml').read_text()
        helper = (root / '.github/workflows/ci-linux-build.yml').read_text()
        for gcc in ('11', '13'):
            block = workflow.split(f'  linux-gcc{gcc}:\n', 1)[1].split('\n  linux-', 1)[0]
            self.assertIn('run_tests: true', block)
            self.assertIn('needs: changes', block)
        tests = helper.split('  tests:\n', 1)[1]
        self.assertIn('needs: build', tests)
        self.assertNotIn('linux-clang', tests)
        gate = workflow.split('  linux:\n', 1)[1].split('  linux-variants:\n', 1)[0]
        self.assertIn('needs: [changes, linux-build, linux-clang]', gate)
        self.assertIn('if [ "$COMPATIBILITY" = true ]; then expected=success; fi', gate)
        self.assertIn('test "$CLANG_RESULT" = "$expected"', gate)
        self.assert_jobs(['.github/workflows/ci-linux-build.yml'], native=True,
                         browser=True, map_generators=True, deployment=True, cross_platform=True)

    def test_javascript_evidence_steps_are_visible_to_ci_failure_summary(self):
        workflow = (SCRIPT.parents[2] / ".github/workflows/build.yml").read_text()
        workflow += (SCRIPT.parents[2] / ".github/workflows/ci-linux-build.yml").read_text()
        for name, identifier in (
            ("Execute shared JavaScript corpus", "execute_shared_javascript_corpus"),
            ("Verify frozen JavaScript simulation profile", "verify_frozen_javascript_simulation_profile"),
        ):
            with self.subTest(step=name):
                definitions = re.findall(r"      - name: " + re.escape(name) +
                                         r"\n(.*?)(?=      - |\n  \w|\Z)", workflow, re.S)
                self.assertEqual(len(definitions), 2, "Expected Linux and Windows evidence steps")
                for definition in definitions:
                    self.assertIn(f"        id: {identifier}\n", definition)


if __name__ == "__main__":
    unittest.main()
