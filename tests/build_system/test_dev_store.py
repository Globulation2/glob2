"""Shared-store correctness, cache budgets, publication, and process leases."""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scons"))
import dev_store as store
from tool_archives import install

ROOT = Path(__file__).resolve().parents[2]


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name).resolve()
        self.env = patch.dict(
            os.environ,
            {"GLOB2_DEV_HOME": str(self.base / "store"), "GLOB2_DEV_MODE": "shared"},
        )
        self.env.start()
        self.a = self.base / "a"
        self.b = self.base / "b"
        for root in (self.a, self.b):
            shutil.copytree(
                ROOT / "mobile",
                root / "mobile",
                ignore=shutil.ignore_patterns("__pycache__"),
            )
            shutil.copytree(
                ROOT / "browser",
                root / "browser",
                ignore=shutil.ignore_patterns("node_modules", "__pycache__"),
            )

    def tearDown(self):
        for lease in store._HELD.values():
            lease.close()
        store._HELD.clear()
        self.env.stop()
        self.temporary.cleanup()

    def test_worktrees_share_pinned_tools_but_not_isolated_state(self):
        self.assertEqual(
            store.mobile_tools(self.a, False), store.mobile_tools(self.b, False)
        )
        self.assertEqual(
            store.browser_sdk(self.a, lease=False),
            store.browser_sdk(self.b, lease=False),
        )
        with patch.dict(os.environ, {"GLOB2_DEV_MODE": "isolated"}):
            self.assertNotEqual(store.mobile_tools(self.a), store.mobile_tools(self.b))
            self.assertEqual(
                store.gradle_home(self.a), self.a / "build/mobile-tools/gradle-home"
            )

    def test_manifest_changes_coexist(self):
        previous = store.mobile_tools(self.a, False)
        with (self.b / "mobile/toolchain.json").open("a") as output:
            output.write("\n")
        self.assertNotEqual(previous, store.mobile_tools(self.b, False))
        self.assertEqual(previous, store.mobile_tools(self.a, False))

    def test_explicit_sdk_and_browser_override(self):
        self.assertEqual(
            store.android_sdk(self.a, self.base / "sdk"), self.base / "sdk"
        )
        self.assertEqual(
            store.browser_sdk(self.a, self.base / "emsdk"), self.base / "emsdk"
        )
        with patch.dict(
            os.environ, {"ANDROID_HOME": "/ambient", "ANDROID_SDK_ROOT": "/ambient"}
        ):
            self.assertNotEqual(store.android_sdk(self.a), Path("/ambient"))

    def test_platform_defaults(self):
        concrete_path = type(self.base)
        with (
            patch.object(store, "Path", concrete_path),
            patch.object(concrete_path, "home", return_value=self.base),
        ):
            with (
                patch.dict(os.environ, {}, clear=True),
                patch.object(store.platform, "system", return_value="Darwin"),
            ):
                self.assertEqual(
                    store.home(),
                    self.base / "Library/Application Support/Glob2/Development",
                )
            for value in (str(self.base), "", "relative-data"):
                with (
                    patch.dict(os.environ, {"XDG_DATA_HOME": value}, clear=True),
                    patch.object(store.platform, "system", return_value="Linux"),
                    patch.object(store.os, "name", "posix"),
                ):
                    base = (
                        self.base
                        if value == str(self.base)
                        else self.base / ".local/share"
                    )
                    self.assertEqual(store.home(), base / "glob2/development")
                with (
                    patch.dict(os.environ, {"LOCALAPPDATA": value}, clear=True),
                    patch.object(store.os, "name", "nt"),
                    patch.object(store.platform, "system", return_value="Windows"),
                ):
                    base = (
                        self.base
                        if value == str(self.base)
                        else self.base / "AppData/Local"
                    )
                    self.assertEqual(store.home(), base / "Glob2/Development")

    def test_dependency_keys_include_compiler_triplets_manifest_and_sdl(self):
        identity = {
            "target": "android",
            "arch": "arm64-v8a",
            "environment": "device",
            "mode": "release",
        }
        before = store.dependency_key(self.a, identity, "compiler-a")
        self.assertNotEqual(
            before, store.dependency_key(self.a, identity, "compiler-b")
        )
        for name in (
            "mobile/vcpkg.json",
            "mobile/triplets/mobile-common.cmake",
            "mobile/sdl-java.json",
        ):
            target = self.a / name
            old = target.read_text()
            target.write_text(old + "\n")
            self.assertNotEqual(
                before, store.dependency_key(self.a, identity, "compiler-a")
            )
            target.write_text(old)

    def test_budget_skips_active_cache_and_preserves_tools_and_evidence(self):
        cache = store.cache(self.a, "active")
        cache.mkdir(parents=True)
        (cache / "data").write_bytes(b"x" * 8192)
        idle = store.cache(self.a, "idle", False)
        idle.mkdir(parents=True)
        (idle / "data").write_bytes(b"x" * 8192)
        tools = store.mobile_tools(self.a, False)
        tools.mkdir(parents=True)
        (tools / "tool").write_bytes(b"x" * 8192)
        evidence = self.a / "artifacts"
        evidence.mkdir()
        (evidence / "save").write_text("keep")
        report = store.prune(budget=0)
        self.assertTrue(cache.exists())
        self.assertFalse(idle.exists())
        self.assertTrue(tools.exists())
        self.assertTrue((evidence / "save").exists())
        self.assertIn(str(cache), report["busy"])

    def test_prune_dry_run_does_not_delete(self):
        idle = store.cache(self.a, "idle", False)
        idle.mkdir(parents=True)
        (idle / "data").write_text("cache")
        self.assertEqual(store.prune(True, 0)["after_bytes"], 0)
        self.assertTrue(idle.exists())

    def test_isolated_mode_cannot_prune_the_shared_store(self):
        with (
            patch.dict(os.environ, {"GLOB2_DEV_MODE": "isolated"}),
            self.assertRaisesRegex(ValueError, "requires GLOB2_DEV_MODE=shared"),
        ):
            store.prune(budget=0)

    def test_process_lease_blocks_install_and_prune(self):
        path = store.cache(self.a, "active")
        path.mkdir(parents=True)
        code = "import sys;sys.path.insert(0,sys.argv[1]);from dev_store import Lease;Lease(sys.argv[2],exclusive=True,blocking=False)"
        command = subprocess.run(
            [sys.executable, "-c", code, str(ROOT / "scons"), str(path)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(command.returncode, 0)
        self.assertIn("in use", command.stderr)

    def test_readers_can_share_a_resource(self):
        path = store.cache(self.a, "active")
        code = "import sys;sys.path.insert(0,sys.argv[1]);from dev_store import Lease;Lease(sys.argv[2],blocking=False)"
        command = subprocess.run(
            [sys.executable, "-c", code, str(ROOT / "scons"), str(path)],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(command.returncode, 0, command.stderr)

    def test_warm_dependency_setup_can_run_during_an_active_build(self):
        from build_layout import build_identity

        identity = build_identity({"target": "android", "release": 1})
        prefix = store.dependency_prefix(self.a, identity, "compiler")
        prefix.mkdir(parents=True)
        (prefix / "lib.a").write_bytes(b"validated archive")
        (prefix / "manifest.json").write_text(
            json.dumps(
                {
                    "identity": identity,
                    "toolchain": "compiler",
                    "archives": {
                        "lib.a": hashlib.sha256(b"validated archive").hexdigest()
                    },
                }
            )
        )
        code = "import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);import dependencies;dependencies.ROOT=Path(sys.argv[2]);dependencies.discover=lambda *args:{'fingerprint':'compiler'};sys.argv=['dependencies.py','--release'];sys.exit(dependencies.main())"
        command = subprocess.run(
            [sys.executable, "-c", code, str(ROOT / "mobile"), str(self.a)],
            capture_output=True,
            text=True,
            check=False,
            timeout=15,
        )
        self.assertEqual(command.returncode, 0, command.stderr)
        self.assertIn(str(prefix), command.stdout)

    def test_checksum_install_atomic_and_rejects_unverified_directory(self):
        import tarfile

        source = self.base / "source"
        source.mkdir()
        (source / "tool").write_text("pinned")
        archive = self.base / "tool.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(source, arcname="tool")
        artifact = {
            "url": archive.as_uri(),
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "directory": "tool",
        }
        destination = self.base / "installed"
        install(artifact, destination, self.base / "downloads", trusted=True)
        self.assertEqual((destination / "tool").read_text(), "pinned")
        install(artifact, destination, self.base / "downloads", trusted=True)
        (destination / ".glob2-archive.json").unlink()
        with self.assertRaisesRegex(ValueError, "Unverified"):
            install(artifact, destination, self.base / "downloads", trusted=True)

    def test_failed_checksum_never_publishes(self):
        archive = self.base / "broken.zip"
        archive.write_text("not an archive")
        artifact = {"url": archive.as_uri(), "sha256": "0" * 64, "directory": "tool"}
        destination = self.base / "installed"
        with self.assertRaisesRegex(ValueError, "checksum"):
            install(artifact, destination, self.base / "downloads", trusted=True)
        self.assertFalse(destination.exists())

    def test_concurrent_installation_publishes_one_complete_entry(self):
        import tarfile

        source = self.base / "source"
        source.mkdir()
        (source / "tool").write_text("pinned")
        archive = self.base / "tool.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(source, arcname="tool")
        artifact = {
            "url": archive.as_uri(),
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "directory": "tool",
        }
        destination = store.home() / "toolchains/mobile/concurrent/tool"
        code = "import sys,json;from pathlib import Path;sys.path.insert(0,sys.argv[1]);from dev_store import Lease;from tool_archives import install\nwith Lease(Path(sys.argv[2]).parent,exclusive=True): install(json.loads(sys.argv[3]),sys.argv[2],sys.argv[4],trusted=True)"
        command = [
            sys.executable,
            "-c",
            code,
            str(ROOT / "scons"),
            str(destination),
            json.dumps(artifact),
            str(self.base / "downloads"),
        ]
        processes = [
            subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            for _ in range(2)
        ]
        for process in processes:
            _output, error = process.communicate(timeout=60)
            self.assertEqual(process.returncode, 0, error)
        self.assertEqual((destination / "tool").read_text(), "pinned")
        self.assertEqual(
            len(list((store.home() / "toolchains/artifacts").iterdir())), 1
        )

    def test_dependency_bundle_rejects_changed_archives(self):
        sys.path.insert(0, str(ROOT / "mobile"))
        from dependencies import validate_bundle

        prefix = self.base / "bundle"
        prefix.mkdir()
        (prefix / "lib.a").write_text("correct")
        (prefix / "manifest.json").write_text(
            json.dumps(
                {
                    "identity": {},
                    "toolchain": "compiler",
                    "archives": {"lib.a": hashlib.sha256(b"correct").hexdigest()},
                }
            )
        )
        validate_bundle(prefix, {}, "compiler")
        (prefix / "lib.a").write_text("changed")
        with self.assertRaisesRegex(ValueError, "checksum"):
            validate_bundle(prefix, {}, "compiler")
        with self.assertRaisesRegex(ValueError, "compiler"):
            validate_bundle(prefix, {}, "another")

    def test_sdk_java_command_preserves_paths_with_spaces(self):
        sdk = self.base / "SDK with spaces"
        env = {
            "JAVA_HOME": str(self.base / "Java with spaces"),
            "JAVA_OPTS": "-Xmx512m",
        }
        command = store.android_java_command(sdk, "sdkmanager", env)
        self.assertEqual(command[0], str(self.base / "Java with spaces/bin/java"))
        self.assertIn(
            "-Dcom.android.sdklib.toolsdir=" + str(sdk / "cmdline-tools/19.0"), command
        )
        self.assertEqual(
            command[-2], str(sdk / "cmdline-tools/19.0/lib/sdkmanager-classpath.jar")
        )

    def test_java_sources_available_without_dependency_buildtrees(self):
        import tarfile

        sys.path.insert(0, str(ROOT / "mobile"))
        import dependencies

        java = (
            self.base
            / "SDL-release-test/android-project/app/src/main/java/org/libsdl/app"
        )
        java.mkdir(parents=True)
        (java / "SDLActivity.java").write_text("pinned java")
        archive = self.base / "sdl.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(self.base / "SDL-release-test", arcname="SDL-release-test")
        checksum = hashlib.sha512(archive.read_bytes()).hexdigest()
        (self.a / "mobile/sdl-java.json").write_text(
            json.dumps(
                {
                    "version": "test",
                    "url": archive.as_uri(),
                    "sha512": checksum,
                    "directory": "SDL-release-test",
                }
            )
        )
        vcpkg = self.base / "vcpkg"
        port = vcpkg / "ports/sdl2"
        port.mkdir(parents=True)
        (port / "portfile.cmake").write_text("SHA512 " + checksum)
        (port / "vcpkg.json").write_text(json.dumps({"version": "test"}))
        downloads = store.cache(self.a, "downloads")
        downloads.mkdir(parents=True)
        copytree = shutil.copytree

        def protected_copy(source, destination, *args, **kwargs):
            report = store.prune(budget=0)
            self.assertTrue(Path(source).is_dir())
            self.assertTrue(any("sdl-source-" in path for path in report["busy"]))
            return copytree(source, destination, *args, **kwargs)

        with (
            patch.object(dependencies, "ROOT", self.a),
            patch.object(dependencies.shutil, "copytree", side_effect=protected_copy),
        ):
            for name in ("cold", "binary-cache-hit"):
                prefix = self.base / name
                prefix.mkdir()
                hashes = dependencies.java_sources(vcpkg, prefix, downloads)
                self.assertTrue(hashes)
                self.assertEqual(
                    (
                        prefix / "share/glob2/sdl-java/org/libsdl/app/SDLActivity.java"
                    ).read_text(),
                    "pinned java",
                )
                self.assertFalse((prefix / "vcpkg-buildtrees").exists())

    def test_migration_preserves_customized_tools(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "dev_cli", ROOT / "tools/dev_environment.py"
        )
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        source = self.base / "old-tool"
        target = self.base / "verified-tool"
        source.mkdir()
        target.mkdir()
        (source / "binary").write_bytes(b"pinned")
        (target / "binary").write_bytes(b"pinned")
        (target / ".glob2-archive.json").write_text("{}")
        self.assertTrue(cli.equivalent(source, target))
        (source / "binary").write_bytes(b"customized")
        self.assertFalse(cli.equivalent(source, target))
        (source / "binary").write_bytes(b"pinned")
        (source / "local-notes").write_text("keep")
        self.assertFalse(cli.equivalent(source, target))

    @unittest.skipIf(os.name == "nt", "POSIX directory symlink")
    def test_migration_skips_symlinked_parent_overrides(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "dev_cli", ROOT / "tools/dev_environment.py"
        )
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        external = self.base / "external-sdk"
        (external / "compiler").mkdir(parents=True)
        (self.a / "sdk").symlink_to(external, target_is_directory=True)
        self.assertFalse(cli.local_directory(self.a / "sdk/compiler", self.a))
        (self.a / "local-sdk/compiler").mkdir(parents=True)
        self.assertTrue(cli.local_directory(self.a / "local-sdk/compiler", self.a))

    @unittest.skipIf(
        os.name == "nt", "Migration requires a supported open-file checker"
    )
    def test_browser_migration_preserves_config_caches_and_busy_checkouts(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location(
            "dev_cli", ROOT / "tools/dev_environment.py"
        )
        cli = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cli)
        target = store.browser_sdk(self.a, lease=False)
        target.mkdir(parents=True)
        manifest = self.a / "browser/toolchain.json"
        (target / ".glob2-toolchain.json").write_text(manifest.read_text())
        local = self.a / "tools/browser-emsdk"
        for sdk in (local, target):
            (sdk / "node").mkdir(parents=True)
            (sdk / "node/binary").write_bytes(b"pinned node")
            (sdk / "upstream/emscripten").mkdir(parents=True)
            (sdk / "upstream/emscripten/compiler").write_bytes(b"pinned compiler")
        (local / "upstream/emscripten/local-cache").write_bytes(b"retain")
        (local / ".emscripten").write_text("checkout config")
        (local / ".git").mkdir()
        (local / ".git/source-history").write_text("retain")
        receipt = {"browser_sdk": str(target), "checks": {"browser-build": 0}}
        item = {
            "busy": True,
            "replacement_candidates": [],
            "replaced": [],
            "reclaimed_bytes": 0,
        }
        cli.migrate_browser(self.a, item, receipt, True)
        self.assertFalse((local / "node").is_symlink())
        tools = store.mobile_tools(self.a, False)
        tools.mkdir(parents=True)
        receipt.update({"tools": str(tools), "toolchain_key": tools.name})
        receipt["checks"].update({"android-packaging": 0, "build-system-tests": 0})
        validation = self.base / "validation.json"
        validation.write_text(json.dumps(receipt))
        self.assertFalse((self.a / "build/mobile-tools").exists())
        with (
            patch.object(cli, "ROOT", self.a),
            patch.object(cli, "checkouts", return_value=[self.a]),
            patch.object(cli, "artifacts", return_value=[]),
            patch.object(cli, "busy", return_value=False),
        ):
            item = cli.migrate(apply=True, validation=validation)[0]
        self.assertEqual((local / "node").resolve(), target / "node")
        self.assertGreater(item["reclaimed_bytes"], 0)
        self.assertFalse((local / "upstream/emscripten").is_symlink())
        self.assertEqual((local / ".emscripten").read_text(), "checkout config")
        self.assertEqual((local / ".git/source-history").read_text(), "retain")

    @unittest.skipIf(os.name == "nt", "POSIX makefile alias")
    def test_sdk_alias_does_not_copy_installation(self):
        source = self.base / "NDK with spaces"
        source.mkdir()
        (source / "tool").write_text("compiler")
        alias = store.space_free_alias(source)
        try:
            self.assertNotIn(" ", str(alias))
            self.assertTrue(alias.is_symlink())
            self.assertEqual(alias.resolve(), source)
            self.assertEqual(store.size(alias), 0)
            self.assertEqual(store.space_free_alias(source), alias)
        finally:
            alias.unlink()

    def test_paths_cli_is_read_only_and_reports_isolated_mode(self):
        script = ROOT / "tools/dev_environment.py"
        command = subprocess.run(
            [sys.executable, str(script), "paths", "--json"],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(command.returncode, 0, command.stderr)
        self.assertEqual(json.loads(command.stdout)["mode"], "shared")
        self.assertFalse(store.home().exists())
        env = dict(os.environ, GLOB2_DEV_MODE="isolated")
        command = subprocess.run(
            [sys.executable, str(script), "paths", "--field", "android_sdk"],
            check=False,
            capture_output=True,
            text=True,
            env=env,
        )
        self.assertEqual(command.returncode, 0, command.stderr)
        self.assertEqual(
            command.stdout.strip(), str(ROOT / "build/mobile-tools/android-sdk")
        )

    def test_identical_components_are_shared_across_toolchain_bundles(self):
        import tarfile

        source = self.base / "source"
        source.mkdir()
        (source / "tool").write_text("pinned")
        archive = self.base / "tool.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(source, arcname="tool")
        artifact = {
            "url": archive.as_uri(),
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "directory": "tool",
        }
        first = store.home() / "toolchains/mobile/first/gradle"
        second = store.home() / "toolchains/mobile/second/gradle"
        downloads = self.base / "downloads"
        install(artifact, first, downloads, trusted=True)
        install(artifact, second, downloads, trusted=True)
        self.assertTrue(first.is_symlink())
        self.assertTrue(second.is_symlink())
        self.assertEqual(first.resolve(), second.resolve())
        self.assertEqual(
            len(list((store.home() / "toolchains/artifacts").iterdir())), 1
        )

    def test_compiler_fingerprint_survives_installation_relocation(self):
        import mobile_toolchain
        from build_layout import build_identity

        lock = json.loads((ROOT / "mobile/toolchain.json").read_text())
        sdk = self.base / "sdk"
        ndk = sdk / "ndk" / lock["android"]["ndk"]
        ndk.mkdir(parents=True)
        (ndk / "source.properties").write_text(
            "Pkg.Revision = " + lock["android"]["ndk"]
        )
        identity = build_identity({"target": "android"})
        with patch.object(
            mobile_toolchain,
            "run",
            return_value="clang version 19\nInstalledDir: /first",
        ):
            first = mobile_toolchain.discover(identity, {"android_sdk": str(sdk)})
        with patch.object(
            mobile_toolchain,
            "run",
            return_value="clang version 19\nInstalledDir: /second",
        ):
            second = mobile_toolchain.discover(identity, {"android_sdk": str(sdk)})
        self.assertEqual(first["fingerprint"], second["fingerprint"])
        with patch.object(
            mobile_toolchain,
            "run",
            return_value="clang version 20\nInstalledDir: /second",
        ):
            changed = mobile_toolchain.discover(identity, {"android_sdk": str(sdk)})
        self.assertNotEqual(first["fingerprint"], changed["fingerprint"])

    def test_compiler_bytes_invalidate_cached_digest(self):
        from mobile_toolchain import compiler_digest

        compiler = self.base / "clang"
        compiler.write_bytes(b"original compiler")
        before = compiler_digest(compiler)
        compiler.write_bytes(b"modified compiler")
        self.assertNotEqual(before, compiler_digest(compiler))


if __name__ == "__main__":
    unittest.main()
