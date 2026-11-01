#!/usr/bin/env python3
"""Download and verify the pinned NDK in the selected development store."""

import json
import platform
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))
from build_layout import BuildLock
from dev_store import Lease, cache, isolated, mobile_tools
from mobile_toolchain import LOCK, ROOT
from tool_archives import install


def main():
    android = json.loads(LOCK.read_text())["android"]
    archive = android["ndk_archives"].get(platform.system())
    if not archive:
        raise ValueError(
            "Automatic NDK setup supports macOS and Linux. Use android_sdk=PATH for an existing SDK."
        )
    tools = mobile_tools(ROOT, lease=False)
    tools.mkdir(parents=True, exist_ok=True)
    downloads = cache(ROOT, "downloads")
    guard = BuildLock(tools) if isolated() else Lease(tools, exclusive=True)
    with guard:
        artifact = dict(archive, directory="android-ndk-r28c")
        destination = tools / "android-sdk/ndk" / android["ndk"]
        install(
            artifact,
            destination,
            downloads,
            [ROOT / "build/mobile-tools/downloads"],
            trusted=not isolated(),
        )
        properties = destination / "source.properties"
        if "Pkg.Revision = " + android["ndk"] not in properties.read_text():
            raise ValueError("Installed NDK revision differs from pin")
        print(destination)
        print("NDK license files are included in the installed package.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
