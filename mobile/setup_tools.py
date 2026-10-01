#!/usr/bin/env python3
"""Install checksum-pinned packaging tools in the selected development store."""

import argparse
import json
import os
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))

from build_layout import BuildLock, write_if_changed
from dev_store import Lease, android_java_command, cache, isolated, mobile_tools
from setup_ndk import ROOT
from tool_archives import install


def ensure_sdk_metadata(destination, artifact):
    """Register directly extracted SDK archives without changing license acceptance."""
    metadata = artifact.get("sdk_metadata")
    if not metadata:
        return
    template = ROOT / metadata
    package = ET.parse(template).getroot().find("localPackage")
    expected = ".".join(
        package.findtext("revision/" + part, "0")
        for part in ("major", "minor", "micro")
    )
    properties = dict(
        line.split("=", 1)
        for line in (destination / "source.properties").read_text().splitlines()
        if "=" in line and not line.startswith("#")
    )
    if properties.get("Pkg.Revision") != expected:
        raise ValueError(
            "SDK archive revision differs from package metadata: " + str(destination)
        )
    target = destination / "package.xml"
    if target.exists():
        installed = ET.parse(target).getroot().find("localPackage")
        revision = ".".join(
            installed.findtext("revision/" + part, "0")
            for part in ("major", "minor", "micro")
        )
        if installed.get("path") != package.get("path") or revision != expected:
            raise ValueError(
                "Installed SDK package metadata does not match the pinned archive"
            )
    else:
        write_if_changed(target, template.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--emulator",
        action="store_true",
        help="Also install the pinned host emulator and matching API 35 image",
    )
    parser.add_argument(
        "--gradle-only",
        action="store_true",
        help="Only install pinned Gradle; use the supplied SDK and JDK",
    )
    parser.add_argument(
        "--sdk-packages",
        action="store_true",
        help="Install pinned platform/build packages; accept licenses explicitly with sdkmanager --licenses first",
    )
    args = parser.parse_args()
    lock = json.loads((ROOT / "mobile/android-tools.json").read_text())
    tools = mobile_tools(ROOT, lease=False)
    tools.mkdir(parents=True, exist_ok=True)
    downloads = cache(ROOT, "downloads")
    guard = BuildLock(tools) if isolated() else Lease(tools, exclusive=True)
    with guard:
        selected = [lock["gradle"]]
        if args.emulator and args.gradle_only:
            parser.error("--emulator and --gradle-only cannot be combined")
        if args.emulator:
            emulators = json.loads((ROOT / "mobile/emulator.json").read_text())[
                "archives"
            ]
            host = platform.system() + "-" + platform.machine()
            if host not in emulators:
                raise ValueError("No pinned emulator for " + host)
            arch = (
                "arm64-v8a" if platform.machine() in ("arm64", "aarch64") else "x86_64"
            )
            selected.extend([emulators[host], emulators[arch]])
        if not args.gradle_only:
            sdk = lock.get("sdk-tools-" + platform.system())
            if sdk:
                selected.append(sdk)
            jdk = lock.get("jdk-" + platform.system() + "-" + platform.machine())
            if jdk:
                selected.append(jdk)
            else:
                print(
                    "Install JDK 17 for this host and set JAVA_HOME when running Gradle."
                )
        for artifact in selected:
            destination = tools / artifact["directory"]
            install(
                artifact,
                destination,
                downloads,
                [ROOT / "build/mobile-tools/downloads"],
                trusted=not isolated(),
            )
            ensure_sdk_metadata(destination, artifact)
            print("Installed", destination)
        if args.sdk_packages:
            sdk = tools / "android-sdk"
            android = json.loads((ROOT / "mobile/toolchain.json").read_text())[
                "android"
            ]
            jdk = lock.get("jdk-" + platform.system() + "-" + platform.machine())
            env = dict(os.environ)
            if jdk and "JAVA_HOME" not in env:
                env["JAVA_HOME"] = str(
                    tools / jdk["directory"] / jdk.get("java_home", ".")
                )
            subprocess.run(
                android_java_command(sdk, "sdkmanager", env)
                + [
                    "--sdk_root=" + str(sdk),
                    "platforms;android-" + str(android["compile_api"]),
                    "build-tools;" + android["build_tools"],
                    "platform-tools",
                ],
                env=env,
                check=True,
            )


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
