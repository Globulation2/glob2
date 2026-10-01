#!/usr/bin/env python3
"""Build and publish checksum-verified pinned mobile dependency bundles."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))
from build_layout import (
    BuildLock,
    build_identity,
    default_directory,
    write_if_changed,
)
from dev_store import (
    Lease,
    cache,
    dependency_key,
    dependency_prefix,
    home,
    isolated,
    key,
    mobile_tools,
    space_free_alias,
)
from mobile_artifacts import verify_android_library
from mobile_toolchain import LOCK, ROOT, discover
from tool_archives import install


def validate_bundle(prefix, identity, fingerprint):
    manifest = json.loads((prefix / "manifest.json").read_text())
    if manifest["identity"] != identity or manifest["toolchain"] != fingerprint:
        raise ValueError("Dependency bundle belongs to another compiler/configuration")
    if not manifest["archives"]:
        raise ValueError("Empty dependency bundle")
    for name, expected in dict(
        manifest.get("files", {}),
        **manifest["archives"],
        **manifest.get("java_sources", {}),
    ).items():
        path = (prefix / name).resolve()
        if (
            not path.is_relative_to(prefix.resolve())
            or not path.is_file()
            or hashlib.sha256(path.read_bytes()).hexdigest() != expected
        ):
            raise ValueError("Dependency checksum mismatch: " + str(path))
    return manifest


def java_sources(vcpkg, prefix, downloads):
    artifact = json.loads((ROOT / "mobile/sdl-java.json").read_text())
    port = (vcpkg / "ports/sdl2/portfile.cmake").read_text()
    version = json.loads((vcpkg / "ports/sdl2/vcpkg.json").read_text())["version"]
    if version != artifact["version"] or artifact["sha512"] not in port:
        raise ValueError("SDL Java source pin differs from the pinned vcpkg SDL port")
    source = cache(
        ROOT,
        "sdl-source-" + artifact["version"] + "-" + artifact["sha512"][:24],
        lease=False,
    )
    # Source extraction is an immutable completed entry, shared independently of vcpkg buildtrees.
    # Keep one continuous lease through publication and copying. Releasing the
    # writer before acquiring a reader would let pruning remove this cache.
    with Lease(source, exclusive=True):
        source.mkdir(parents=True, exist_ok=True)
        install(artifact, source / "source", downloads, trusted=not isolated())
        java = source / "source/android-project/app/src/main/java"
        shutil.copytree(java, prefix / "share/glob2/sdl-java")
        return {
            str(p.relative_to(prefix)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted((prefix / "share/glob2/sdl-java").rglob("*"))
            if p.is_file()
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default="android", choices=["android", "ios"])
    parser.add_argument("--arch")
    parser.add_argument(
        "--environment", default="device", choices=["device", "simulator"]
    )
    parser.add_argument("--developer-dir")
    parser.add_argument("--android-sdk")
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--china", action="store_true")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    args.arch = args.arch or ("arm64-v8a" if args.target == "android" else "arm64")
    options = {
        "target": args.target,
        "arch": args.arch,
        "environment": args.environment,
        "release": int(args.release),
        "china": int(args.china),
    }
    if args.android_sdk:
        options["android_sdk"] = args.android_sdk
    if args.developer_dir:
        options["developer_dir"] = args.developer_dir
    identity = build_identity(options)
    toolchain = discover(identity, options)
    final = dependency_prefix(ROOT, identity, toolchain["fingerprint"], lease=False)
    output = ROOT / default_directory(identity)
    output.mkdir(parents=True, exist_ok=True)
    guard = BuildLock(output) if isolated() else Lease(final, exclusive=True)
    with guard:
        if (final / "manifest.json").is_file():
            validate_bundle(final, identity, toolchain["fingerprint"])
            print("Dependency prefix:", final)
            return 0
        revision = json.loads((ROOT / "mobile/vcpkg.json").read_text())[
            "builtin-baseline"
        ]
        vcpkg = (
            mobile_tools(ROOT, lease=False) / "vcpkg"
            if isolated()
            else home() / "toolchains/vcpkg" / key(ROOT, ["mobile/vcpkg.json"])
        )
        vcpkg.parent.mkdir(parents=True, exist_ok=True)
        # vcpkg's own internal tools directory is mutable; serialize cold builds using this revision.
        with Lease(vcpkg, exclusive=True):
            if not (vcpkg / ".git").is_dir():
                staging = Path(tempfile.mkdtemp(dir=vcpkg.parent, prefix=".install-"))
                try:
                    checkout = staging / "vcpkg"
                    subprocess.run(
                        [
                            "git",
                            "clone",
                            "--filter=blob:none",
                            "--no-checkout",
                            "https://github.com/microsoft/vcpkg.git",
                            str(checkout),
                        ],
                        check=True,
                    )
                    subprocess.run(
                        ["git", "-C", str(checkout), "checkout", "--detach", revision],
                        check=True,
                    )
                    subprocess.run(
                        [str(checkout / "bootstrap-vcpkg.sh"), "-disableMetrics"],
                        check=True,
                    )
                    checkout.rename(vcpkg)
                finally:
                    shutil.rmtree(staging)
            current = subprocess.check_output(
                ["git", "-C", str(vcpkg), "rev-parse", "HEAD"], text=True
            ).strip()
            if current != revision:
                raise ValueError("Pinned vcpkg checkout revision mismatch")
            arch = {
                "arm64-v8a": "arm64",
                "armeabi-v7a": "arm",
                "x86_64": "x64",
                "arm64": "arm64",
            }[args.arch]
            triplet = (
                "glob2-"
                + arch
                + "-"
                + args.target
                + ("-simulator" if args.environment == "simulator" else "")
            )
            scratch = (
                output
                if isolated()
                else Path(tempfile.mkdtemp(dir=output, prefix="dependency-staging-"))
            )
            installed = scratch / "vcpkg-installed"
            env = dict(os.environ)
            (scratch / "tmp").mkdir(exist_ok=True)
            downloads = cache(ROOT, "downloads")
            registries = cache(ROOT, "vcpkg-registries")
            binaries = cache(ROOT, "vcpkg-binary")
            for directory in (downloads, registries, binaries):
                directory.mkdir(parents=True, exist_ok=True)
            for name in (
                "CPATH",
                "C_INCLUDE_PATH",
                "CPLUS_INCLUDE_PATH",
                "LIBRARY_PATH",
                "SDKROOT",
                "MACOSX_DEPLOYMENT_TARGET",
            ):
                env.pop(name, None)
            if args.target == "android":
                from dev_store import android_sdk

                env["ANDROID_NDK_HOME"] = str(
                    space_free_alias(
                        android_sdk(ROOT, args.android_sdk)
                        / "ndk"
                        / json.loads(LOCK.read_text())["android"]["ndk"]
                    )
                )
            env.update(
                VCPKG_ROOT=str(vcpkg),
                VCPKG_DISABLE_METRICS="1",
                VCPKG_DOWNLOADS=str(downloads),
                VCPKG_BINARY_SOURCES="clear"
                if isolated()
                else "clear;files," + str(binaries) + ",readwrite",
                VCPKG_MAX_CONCURRENCY=str(args.jobs),
                VCPKG_REGISTRIES_CACHE=str(registries),
                TMPDIR=str(scratch / "tmp"),
            )
            if args.developer_dir:
                env["DEVELOPER_DIR"] = args.developer_dir
            subprocess.run(
                [
                    str(vcpkg / "vcpkg"),
                    "install",
                    "--triplet=" + triplet,
                    "--overlay-triplets=" + str(ROOT / "mobile/triplets"),
                    "--x-manifest-root=" + str(ROOT / "mobile"),
                    "--x-install-root=" + str(installed),
                    "--x-buildtrees-root=" + str(scratch / "vcpkg-buildtrees"),
                    "--x-packages-root=" + str(scratch / "vcpkg-packages"),
                ],
                env=env,
                check=True,
            )
            prefix = installed / triplet
            libraries = prefix / ("lib" if args.release else "debug/lib")
            for path in libraries.iterdir():
                if args.target == "android" and path.suffix in (".a", ".so"):
                    verify_android_library(path, args.arch)
            archives = {
                str(p.relative_to(prefix)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(libraries.iterdir())
                if p.suffix in (".a", ".so")
            }
            java = (
                java_sources(vcpkg, prefix, downloads)
                if args.target == "android"
                else {}
            )
            manifest = {
                "identity": identity,
                "toolchain": toolchain["fingerprint"],
                "registry": revision,
                "dependency_key": dependency_key(
                    ROOT, identity, toolchain["fingerprint"]
                ),
                "archives": archives,
                "java_sources": java,
                "files": {
                    str(p.relative_to(prefix)): hashlib.sha256(
                        p.read_bytes()
                    ).hexdigest()
                    for p in sorted(prefix.rglob("*"))
                    if p.is_file() and p.name != "manifest.json"
                },
            }
            write_if_changed(
                prefix / "manifest.json", json.dumps(manifest, indent=2) + "\n"
            )
            validate_bundle(prefix, identity, toolchain["fingerprint"])
            if not isolated():
                final.parent.mkdir(parents=True, exist_ok=True)
                publication = Path(
                    tempfile.mkdtemp(dir=final.parent, prefix=".publish-")
                )
                try:
                    shutil.copytree(prefix, publication / "bundle")
                    (publication / "bundle").rename(final)
                finally:
                    shutil.rmtree(publication)
                shutil.rmtree(scratch)
            print("Dependency prefix:", final)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
