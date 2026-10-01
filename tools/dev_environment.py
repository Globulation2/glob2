#!/usr/bin/env python3
"""Inspect, prune, and migrate the managed Glob2 development environment."""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))
import dev_store as store
from tool_archives import digest

ROOT = Path(__file__).resolve().parents[1]


def checkouts(extra=()):
    result = {ROOT.resolve()}
    for repository in (
        ROOT,
        Path.home() / "glob2",
        Path.home() / "glob2-claude",
        *extra,
    ):
        if not Path(repository).exists():
            continue
        command = subprocess.run(
            ["git", "-C", str(repository), "worktree", "list", "--porcelain"],
            check=False,
            capture_output=True,
            text=True,
        )
        result.update(
            Path(line[9:]).resolve()
            for line in command.stdout.splitlines()
            if line.startswith("worktree ")
        )
    return sorted(p for p in result if (p / "mobile/toolchain.json").is_file())


def busy(checkout):
    """Conservative process/open-file check; failure means migration cannot delete."""
    if os.name == "nt":
        return True  # Inventory/seed is supported; deletion requires an OS open-file checker.
    for tools in (checkout / "build/mobile-tools", checkout / "tools/browser-emsdk"):
        if not tools.is_dir() or tools.is_symlink():
            continue
        command = subprocess.run(
            ["lsof", "-n", "-P", "+D", str(tools)],
            check=False,
            capture_output=True,
            text=True,
        )
        if command.returncode not in (0, 1) or command.stdout.strip():
            return True
    # A build can be between file opens; inspect cwd of all processes too.
    cwd = subprocess.run(
        ["lsof", "-n", "-P", "-a", "-d", "cwd", "-F", "n"],
        check=False,
        capture_output=True,
        text=True,
    )
    if cwd.returncode not in (0, 1):
        return True
    return any(
        line.startswith("n" + str(checkout))
        and (line[1:] == str(checkout) or line[1:].startswith(str(checkout) + "/"))
        for line in cwd.stdout.splitlines()
    )


def artifacts(root):
    tools = json.loads((root / "mobile/android-tools.json").read_text())
    result = [tools["gradle"]]
    host = store.platform.system() + "-" + store.platform.machine()
    if "jdk-" + host in tools:
        result.append(tools["jdk-" + host])
    if "sdk-tools-" + store.platform.system() in tools:
        result.append(tools["sdk-tools-" + store.platform.system()])
    android = json.loads((root / "mobile/toolchain.json").read_text())["android"]
    if store.platform.system() in android["ndk_archives"]:
        result.append(
            dict(
                android["ndk_archives"][store.platform.system()],
                directory="android-sdk/ndk/" + android["ndk"],
                archive_directory="android-ndk-r28c",
            )
        )
    emulator = json.loads((root / "mobile/emulator.json").read_text())["archives"]
    if host in emulator:
        result.append(emulator[host])
    arch = "arm64-v8a" if store.platform.machine() in ("arm64", "aarch64") else "x86_64"
    if arch in emulator:
        result.append(emulator[arch])
    return result


def equivalent(source, target):
    """Do not discard customized extracted tools during duplicate replacement."""

    def files(root):
        return {
            str(p.relative_to(root)): p
            for p in root.rglob("*")
            if (p.is_file() or p.is_symlink()) and p.name != ".glob2-archive.json"
        }

    left, right = files(source), files(target)
    if left.keys() != right.keys():
        return False
    for name, p in left.items():
        q = right[name]
        if p.is_symlink() or q.is_symlink():
            if (
                not p.is_symlink()
                or not q.is_symlink()
                or os.readlink(p) != os.readlink(q)
            ):
                return False
        elif p.stat().st_size != q.stat().st_size or digest(p) != digest(q):
            return False
    return True


def local_directory(candidate, checkout):
    """Never remove an override reached through a directory symlink."""
    if not candidate.is_dir() or not candidate.is_relative_to(checkout):
        return False
    return not any(
        p.is_symlink()
        for p in (candidate, *candidate.parents)
        if p != checkout and p.is_relative_to(checkout)
    )


def replace_duplicate(candidate, target):
    """Leave a compatibility link only after preserving a recoverable original."""
    amount = store.size(candidate)
    backup = candidate.with_name(candidate.name + ".glob2-migration")
    if backup.exists():
        raise ValueError("Previous migration backup needs inspection: " + str(backup))
    candidate.rename(backup)
    try:
        candidate.symlink_to(target, target_is_directory=True)
    except BaseException:
        backup.rename(candidate)
        raise
    shutil.rmtree(backup)
    return amount


def migrate_browser(checkout, item, receipt, apply):
    """Compare installed components, preserving local SDK configuration and caches."""
    if not receipt or receipt.get("checks", {}).get("browser-build") != 0:
        return
    manifest = checkout / "browser/toolchain.json"
    if not manifest.is_file():
        return
    target = store.browser_sdk(checkout, lease=False)
    marker = target / ".glob2-toolchain.json"
    if (
        receipt.get("browser_sdk") != str(target)
        or not marker.is_file()
        or json.loads(marker.read_text()) != json.loads(manifest.read_text())
    ):
        return
    local = checkout / "tools/browser-emsdk"
    if local.is_symlink() or not local.is_dir():
        return
    # Avoid replacing the SDK root: keep its Git history and generated local config.
    # A component containing different files (including a populated legacy cache)
    # stays local. Revisions alone never authorize replacement.
    components = (
        "node",
        "python",
        "upstream/bin",
        "upstream/lib",
        "upstream/include",
        "upstream/share",
        "upstream/emscripten",
    )
    candidates = [
        local / name
        for name in components
        if local_directory(local / name, checkout) and (target / name).is_dir()
    ]
    item["replacement_candidates"].extend(str(p) for p in candidates)
    if not apply or item["busy"]:
        return
    from build_layout import BuildLock

    state = checkout / "build/mobile-tools"
    state.mkdir(parents=True, exist_ok=True)
    with BuildLock(state), store.Lease(target):
        for candidate in candidates:
            destination = target / candidate.relative_to(local)
            if not equivalent(candidate, destination):
                item.setdefault("customized", []).append(str(candidate))
                continue
            if busy(checkout):
                item["busy"] = True
                break
            item["reclaimed_bytes"] += replace_duplicate(candidate, destination)
            item["replaced"].append(str(candidate))


def migrate(apply=False, extra=(), validation=None):
    """Seed archives only. Duplicate removal needs independently recorded validation."""
    if store.isolated():
        raise ValueError("Shared-store migration requires GLOB2_DEV_MODE=shared")
    report = []
    receipt = json.loads(Path(validation).read_text()) if validation else None
    if receipt:
        if (
            receipt.get("tools") != str(store.mobile_tools(ROOT, False))
            or receipt.get("toolchain_key") != store.mobile_tools(ROOT, False).name
        ):
            raise ValueError("Validation receipt belongs to another toolchain")
        checks = receipt.get("checks", {})
        if (
            checks.get("android-packaging") != 0
            or checks.get("build-system-tests") != 0
        ):
            raise ValueError(
                "Migration requires successful Android packaging and build-system validation"
            )
    downloads = store.cache(ROOT, "downloads", lease=apply)
    if apply:
        downloads.mkdir(parents=True, exist_ok=True)
    for checkout in checkouts(extra):
        local = checkout / "build/mobile-tools"
        browser = checkout / "tools/browser-emsdk"
        if not local.is_dir() and not browser.is_dir():
            continue
        item = {
            "checkout": str(checkout),
            "local_bytes": store.size(local),
            "legacy_browser_bytes": store.size(browser),
            "legacy_browser_path": str(browser),
            "busy": busy(checkout),
            "seeded": [],
            "replaced": [],
            "reclaimed_bytes": 0,
            "replacement_candidates": [],
        }
        if apply:
            for artifact in artifacts(checkout):
                name = artifact["url"].rsplit("/", 1)[-1]
                source = local / "downloads" / name
                algorithm = next(
                    x for x in ("sha256", "sha512", "sha1") if x in artifact
                )
                if (
                    source.is_file()
                    and digest(source, algorithm) == artifact[algorithm]
                ):
                    target = downloads / name
                    if (
                        not target.exists()
                        or digest(target, algorithm) != artifact[algorithm]
                    ):
                        temporary = downloads / (
                            name + ".migration-" + str(os.getpid())
                        )
                        shutil.copyfile(source, temporary)
                        if digest(temporary, algorithm) != artifact[algorithm]:
                            temporary.unlink()
                            raise ValueError(
                                "Archive changed during migration: " + str(source)
                            )
                        temporary.replace(target)
                    item["seeded"].append(name)
        if receipt:
            for artifact in artifacts(checkout):
                candidate = local / artifact["directory"]
                target = Path(receipt["tools"]) / artifact["directory"]
                proof = target / ".glob2-archive.json"
                expected = {
                    name: artifact[name]
                    for name in ("url", "sha256", "sha512", "sha1")
                    if name in artifact
                }
                if (
                    local_directory(candidate, checkout)
                    and proof.is_file()
                    and json.loads(proof.read_text()) == expected
                ):
                    item["replacement_candidates"].append(str(candidate))
        if apply and receipt and not item["busy"]:
            target_tools = Path(receipt["tools"])
            if target_tools.is_dir():
                from build_layout import BuildLock

                try:
                    with BuildLock(local), store.Lease(target_tools):
                        for artifact in artifacts(checkout):
                            candidate = local / artifact["directory"]
                            target = target_tools / artifact["directory"]
                            proof = target / ".glob2-archive.json"
                            expected = {
                                name: artifact[name]
                                for name in ("url", "sha256", "sha512", "sha1")
                                if name in artifact
                            }
                            if (
                                not local_directory(candidate, checkout)
                                or not proof.is_file()
                            ):
                                continue
                            if json.loads(proof.read_text()) != expected:
                                continue
                            if not equivalent(candidate, target):
                                item.setdefault("customized", []).append(str(candidate))
                                continue
                            # Recheck process state immediately before each mutation.
                            if busy(checkout):
                                item["busy"] = True
                                break
                            item["reclaimed_bytes"] += replace_duplicate(
                                candidate, target
                            )
                            item["replaced"].append(str(candidate))
                        if not item["busy"]:
                            for artifact in artifacts(checkout):
                                name = artifact["url"].rsplit("/", 1)[-1]
                                source = local / "downloads" / name
                                target = downloads / name
                                algorithm = next(
                                    x
                                    for x in ("sha256", "sha512", "sha1")
                                    if x in artifact
                                )
                                if (
                                    source.is_symlink()
                                    or not source.is_file()
                                    or not target.is_file()
                                ):
                                    continue
                                if (
                                    digest(source, algorithm) != artifact[algorithm]
                                    or digest(target, algorithm) != artifact[algorithm]
                                ):
                                    continue
                                if busy(checkout):
                                    item["busy"] = True
                                    break
                                amount = (
                                    source.stat().st_blocks * 512
                                    if hasattr(source.stat(), "st_blocks")
                                    else source.stat().st_size
                                )
                                backup = source.with_name(
                                    source.name + ".glob2-migration"
                                )
                                if backup.exists():
                                    raise ValueError(
                                        "Previous migration backup needs inspection: "
                                        + str(backup)
                                    )
                                source.rename(backup)
                                try:
                                    backup.unlink()
                                except BaseException:
                                    backup.rename(source)
                                    raise
                                item.setdefault("removed_archives", []).append(
                                    str(source)
                                )
                                item["reclaimed_bytes"] += amount
                except ValueError as error:
                    item["skipped"] = str(error)
        try:
            migrate_browser(checkout, item, receipt, apply)
        except ValueError as error:
            item["skipped"] = str(error)
        report.append(item)
    return report


def external_storage():
    user = Path.home()
    system = store.platform.system()
    cache_home = (
        user / "Library/Caches"
        if system == "Darwin"
        else Path(os.environ.get("LOCALAPPDATA", user / "AppData/Local"))
        if os.name == "nt"
        else Path(os.environ.get("XDG_CACHE_HOME", user / ".cache"))
    )
    candidates = {
        "legacy_ccache": Path(os.environ.get("CCACHE_DIR", cache_home / "ccache")),
        "npm": Path(
            os.environ.get(
                "npm_config_cache",
                cache_home / "npm-cache" if os.name == "nt" else user / ".npm",
            )
        ),
        "playwright": Path(
            os.environ.get("PLAYWRIGHT_BROWSERS_PATH", cache_home / "ms-playwright")
        ),
        "legacy_gradle": Path(os.environ.get("GRADLE_USER_HOME", user / ".gradle")),
    }
    if system == "Darwin":
        candidates["global_android_sdk"] = Path(
            "/opt/homebrew/share/android-commandlinetools"
        )
    for name in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        if os.environ.get(name):
            candidates[name] = Path(os.environ[name])
    seen = set()
    result = []
    for name, p in candidates.items():
        p = p.expanduser().resolve()
        if not p.is_dir() or p in seen or p.is_relative_to(store.home().resolve()):
            continue
        seen.add(p)
        result.append({"name": name, "path": str(p), "bytes": store.size(p)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    paths = commands.add_parser("paths")
    paths.add_argument("--json", action="store_true")
    paths.add_argument("--field")
    commands.add_parser("status")
    sdkmanager = commands.add_parser("sdkmanager")
    sdkmanager.add_argument("arguments", nargs=argparse.REMAINDER)
    prune = commands.add_parser("prune")
    prune.add_argument("--dry-run", action="store_true")
    migrate_parser = commands.add_parser("migrate")
    mode = migrate_parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--apply", action="store_true")
    migrate_parser.add_argument("--checkout", type=Path, action="append", default=[])
    migrate_parser.add_argument(
        "--validation",
        type=Path,
        help="Recorded successful toolchain validation; enables duplicate replacement with compatibility links",
    )
    args = parser.parse_args()
    if args.command == "sdkmanager":
        tools = store.mobile_tools(ROOT, lease=False)
        sdk = tools / "android-sdk"
        env = dict(os.environ)
        if not env.get("JAVA_HOME"):
            env["JAVA_HOME"] = store.paths(ROOT)["java_home"]
        arguments = (
            args.arguments[1:] if args.arguments[:1] == ["--"] else args.arguments
        )
        with store.Lease(tools, exclusive=True):
            raise SystemExit(
                subprocess.call(
                    store.android_java_command(sdk, "sdkmanager", env)
                    + ["--sdk_root=" + str(sdk), *arguments],
                    env=env,
                )
            )
    if args.command == "paths":
        result = store.paths(ROOT)
        if args.field:
            if args.field not in result:
                parser.error("Unknown path field: " + args.field)
            print(result[args.field])
            return
    elif args.command == "status":
        result = {
            "paths": store.paths(ROOT),
            "cache_entries": [
                {"path": str(p), "bytes": size, "last_use": stamp}
                for p, size, stamp in store.entries()
            ],
            "installed_toolchains_bytes": store.size(store.home() / "toolchains"),
            "checkout_build_bytes": store.size(ROOT / "build"),
            "checkout_artifacts_bytes": store.size(ROOT / "artifacts"),
            "simulator_state_bytes": sum(
                store.size(ROOT / "build/mobile-tools" / name)
                for name in ("avd", "ios-simulators")
            ),
            "external_storage": external_storage(),
        }
    elif args.command == "prune":
        result = store.prune(args.dry_run)
    else:
        result = migrate(args.apply, args.checkout, args.validation)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
