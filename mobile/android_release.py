"""Android release identity and APK checks shared by CI and F-Droid builds."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

from asset_bundle import verify_apk_assets

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "org.globulation.glob2"
ABI_CODES = {"armeabi-v7a": 1, "arm64-v8a": 2, "x86_64": 3}


def release_identity(root=ROOT):
    identity = json.loads((root / "mobile/android-release.json").read_text())
    name, base = identity.get("versionName"), identity.get("versionCodeBase")
    parts = name.split(".") if isinstance(name, str) else []
    if len(parts) != 4 or any(not part.isdecimal() or int(part) >= 100 or
                              (len(part) > 1 and part.startswith("0")) for part in parts):
        raise ValueError("Android versionName must have four numeric components below 100")
    expected = sum(int(part) * factor for part, factor in zip(parts, (1000000, 10000, 100, 1)))
    if type(base) is not int or base != expected or base <= 0 or base * 10 + 3 > 2100000000:
        raise ValueError("Android versionCodeBase does not encode versionName")
    version_file = (root / "scons/build_layout.py").read_text()
    match = re.search(r'^PACKAGE_VERSION = "([0-9]+(?:\.[0-9]+)+)"$', version_file, re.MULTILINE)
    if not match or name != match.group(1):
        raise ValueError("Android versionName differs from PACKAGE_VERSION")
    return identity


def version_code(arch, root=ROOT):
    if arch not in ABI_CODES:
        raise ValueError("Unknown Android ABI: " + arch)
    return release_identity(root)["versionCodeBase"] * 10 + ABI_CODES[arch]


def check_prior_tags(root=ROOT):
    current = release_identity(root)["versionCodeBase"]
    tags = subprocess.check_output(["git", "tag", "--list", "v*"], cwd=root, text=True).splitlines()
    for tag in tags:
        parts = tag.removeprefix("v").split(".")
        if len(parts) != 4 or any(not part.isdecimal() or int(part) >= 100 for part in parts):
            continue
        previous = sum(int(part) * factor for part, factor in zip(parts, (1000000, 10000, 100, 1)))
        if previous >= current and tag != "v" + release_identity(root)["versionName"]:
            raise ValueError(f"Android version codes would not increase past {tag}")


def check_listing(root=ROOT):
    identity = release_identity(root)
    locale = root / "fastlane/metadata/android/en-US"
    required = [locale / "title.txt", locale / "short_description.txt", locale / "full_description.txt",
                locale / "images/icon.png", locale / "images/phoneScreenshots/1.png"]
    required.extend(locale / "changelogs" / f"{version_code(arch, root)}.txt" for arch in ABI_CODES)
    missing = [str(path.relative_to(root)) for path in required if not path.is_file() or path.stat().st_size == 0]
    if missing:
        raise ValueError("Android F-Droid listing is incomplete: " + ", ".join(missing))
    if len((locale / "short_description.txt").read_text().strip()) >= 80:
        raise ValueError("F-Droid short description must be less than 80 characters")
    if len((locale / "title.txt").read_text().strip()) > 50:
        raise ValueError("F-Droid title must be at most 50 characters")
    if len((locale / "full_description.txt").read_text().strip()) > 4000:
        raise ValueError("F-Droid full description must be at most 4000 characters")
    for image in (locale / "images/icon.png", locale / "images/phoneScreenshots/1.png"):
        if not image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("F-Droid image is not a PNG: " + str(image))
    for arch in ABI_CODES:
        path = locale / "changelogs" / f"{version_code(arch, root)}.txt"
        if len(path.read_text().strip()) > 500:
            raise ValueError("F-Droid changelog is too long: " + str(path))
    return identity


def check_recipe(root=ROOT):
    recipe = (root / "fdroid/metadata/org.globulation.glob2.yml").read_text()
    identity = release_identity(root)
    expected_codes = [version_code(arch, root) for arch in ABI_CODES]
    codes = [int(code) for code in re.findall(r"^    versionCode: (\d+)$", recipe, re.MULTILINE)]
    names = re.findall(r"^  - versionName: ([^\n]+)$", recipe, re.MULTILINE)
    commits = re.findall(r"^    commit: ([^\n]+)$", recipe, re.MULTILINE)
    commands = re.findall(r"^    build: ([^\n]+)$", recipe, re.MULTILINE)
    operations = re.search(r"^VercodeOperation:\n((?:  - [^\n]+\n)+)", recipe, re.MULTILINE)
    operation_lines = [line.strip().removeprefix("- ") for line in operations.group(1).splitlines()] if operations else []
    expected_operations = [f"10 * %c + {suffix}" for suffix in ABI_CODES.values()]
    if (codes != expected_codes or names != [identity["versionName"]] * len(ABI_CODES) or
            commits != ["v" + identity["versionName"]] * len(ABI_CODES) or
            len(commands) != len(ABI_CODES) or
            any("--version-code $$VERCODE$$" not in command for command in commands) or
            operation_lines != expected_operations):
        raise ValueError("F-Droid recipe does not declare every ABI and matching release version")
    return codes


def verify_apk(apk, arch, sdk, root=ROOT):
    identity = release_identity(root)
    expected_code = version_code(arch, root)
    if not apk.is_file():
        raise ValueError("Missing Android APK: " + str(apk))
    with zipfile.ZipFile(apk) as archive:
        libraries = [name for name in archive.namelist() if name.startswith("lib/") and name.endswith(".so")]
        found_abis = {name.split("/")[1] for name in libraries}
        if found_abis != {arch} or f"lib/{arch}/libmain.so" not in libraries:
            raise ValueError(f"APK native libraries have ABIs {sorted(found_abis)}, expected {arch}")
    verify_apk_assets(apk)
    tools = json.loads((root / "mobile/toolchain.json").read_text())["android"]["build_tools"]
    binaries = sdk / "build-tools" / tools
    subprocess.run([str(binaries / "zipalign"), "-c", "-P", "16", "4", str(apk)], check=True)
    signature = subprocess.run([str(binaries / "apksigner"), "verify", str(apk)],
                               capture_output=True, text=True)
    if signature.returncode == 0:
        raise ValueError("F-Droid input APK must be unsigned")
    output = subprocess.check_output([str(binaries / "aapt"), "dump", "badging", str(apk)], text=True)
    package = re.search(r"^package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'", output, re.MULTILINE)
    if not package or package.groups() != (PACKAGE, str(expected_code), identity["versionName"]):
        raise ValueError("APK package identity, version code, or version name differs from release manifest")
    return hashlib.sha256(apk.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "check-listing", "verify-apk", "code"))
    parser.add_argument("--arch", choices=tuple(ABI_CODES))
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--android-sdk", type=Path, default=ROOT / "build/mobile-tools/android-sdk")
    args = parser.parse_args()
    if args.command == "check":
        check_prior_tags()
        check_recipe()
        print(release_identity()["versionName"])
    elif args.command == "check-listing":
        check_listing()
        print("F-Droid listing ready")
    else:
        if not args.arch:
            parser.error("--arch is required")
        if args.command == "code":
            print(version_code(args.arch))
        else:
            if not args.apk:
                parser.error("--apk is required")
            print(verify_apk(args.apk, args.arch, args.android_sdk))


if __name__ == "__main__":
    main()
