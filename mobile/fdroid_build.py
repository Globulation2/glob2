#!/usr/bin/env python3
"""Build one F-Droid ABI from its version code using the supplied Android SDK."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from android_release import ABI_CODES, ROOT, verify_apk, version_code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version-code", type=int, required=True)
    parser.add_argument("--android-sdk", type=Path, required=True)
    parser.add_argument("--android-ndk", type=Path, required=True)
    args = parser.parse_args()
    matching = [arch for arch in ABI_CODES if version_code(arch) == args.version_code]
    if len(matching) != 1:
        parser.error("version code does not identify a current release ABI")
    arch = matching[0]
    source_sdk = args.android_sdk.resolve()
    source_ndk = args.android_ndk.resolve()
    for required in ("platforms/android-35", "build-tools/35.0.0", "platform-tools"):
        if not (source_sdk / required).exists():
            raise ValueError("F-Droid Android SDK is missing " + required)
    if not (source_ndk / "source.properties").is_file():
        raise ValueError("F-Droid Android NDK is missing source.properties")
    sdk = ROOT / "build/mobile-tools/android-sdk"
    sdk.mkdir(parents=True, exist_ok=True)
    for name in ("platforms", "build-tools", "platform-tools", "licenses", "cmdline-tools"):
        link = sdk / name
        if not link.exists() and (source_sdk / name).exists():
            link.symlink_to(source_sdk / name, target_is_directory=True)
    toolchain = json.loads((ROOT / "mobile/toolchain.json").read_text())["android"]
    ndk_revision = toolchain["ndk"]
    ndk_link = sdk / "ndk" / ndk_revision
    ndk_link.parent.mkdir(exist_ok=True)
    if not ndk_link.exists():
        ndk_link.symlink_to(source_ndk, target_is_directory=True)
    gradle = ROOT / "build/mobile-tools/gradle-8.13/bin/gradle"
    subprocess.run([sys.executable, "mobile/setup_tools.py", "--gradle-only"], cwd=ROOT, check=True)
    subprocess.run([sys.executable, "mobile/dependencies.py", "--arch", arch, "--release", "--jobs", "2",
                    "--android-sdk", str(sdk)], cwd=ROOT, check=True)
    subprocess.run([sys.executable, "mobile/android.py", "build", "--arch", arch, "--release", "--jobs", "2",
                    "--android-sdk", str(sdk), "--gradle", str(gradle)], cwd=ROOT, check=True)
    source = (ROOT / "build/android/device" / arch / str(toolchain["min_api"]) /
              "client/release/android-project/app/build/outputs/apk/release/app-release-unsigned.apk")
    digest = verify_apk(source, arch, sdk)
    output = ROOT / "build/fdroid-output.apk"
    shutil.copyfile(source, output)
    print(f"F-Droid APK {arch}: {output} sha256={digest}")


if __name__ == "__main__":
    main()
