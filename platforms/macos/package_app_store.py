#!/usr/bin/env python3
"""Stage and sign a built macOS bundle (tools/dev_build.py release=1 bundle) for App Store sandbox testing.

With an Apple Distribution identity and installer identity, also make the .pkg
that App Store Connect accepts. The default ad hoc signature is for local testing.
"""

import argparse
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENTITLEMENTS = ROOT / "platforms/macos/AppStore.entitlements"
# Product copyright line decided by the project owner on 2026-11-25.
APP_COPYRIGHT = (
    "© 2006–2026 Bradley Arsenault. © 2001–2008 Stephane Magnenat. "
    "© the Globulation 2 contributors."
)
# Declared Age Range (src/app/AgeGate.h, platforms/apple/AgeSignal.swift) is a
# restricted entitlement: only a profile whose App ID has the capability may
# carry it, so ad hoc test builds leave it out (the game then asks its own question).
AGE_RANGE_ENTITLEMENT = "com.apple.developer.declared-age-range"
ICONS = ROOT / "content/data/icons"
ICON_MASTER = ROOT / "content-src/icons/app/glob2-icon-1024.png"
sys.path.insert(0, str(ROOT / "buildsys"))
sys.path.insert(0, str(ROOT / "platforms/mobile"))
from build_layout import PACKAGE_VERSION
from ios_release import marketing_version


def run(*args):
    print("+", *map(str, args), flush=True)
    subprocess.run([str(arg) for arg in args], check=True)


def icon_source(size):
    """The desktop icon drawn for this size, or the high-resolution master to reduce."""
    exact = ICONS / f"glob2-icon-{size}x{size}.png"
    return exact if exact.is_file() else ICON_MASTER


def write_icon(destination):
    """Write every Retina and App Store size from the checked-in icon sources."""
    sizes = {
        "icon_16x16.png": 16, "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32, "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128, "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256, "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512, "icon_512x512@2x.png": 1024,
    }
    with tempfile.TemporaryDirectory() as directory:
        iconset = Path(directory) / "Glob2.iconset"
        iconset.mkdir()
        for name, size in sizes.items():
            run("sips", "-z", size, size, icon_source(size),
                "--out", iconset / name)
        run("iconutil", "-c", "icns", iconset, "-o", destination)


def distribution_entitlements(profile_path, bundle_id):
    """Bind the sandbox claims to the App ID authorized by Apple's profile."""
    profile = plistlib.loads(subprocess.check_output(
        ["security", "cms", "-D", "-i", str(profile_path)]))
    team_ids = profile.get("TeamIdentifier", [])
    claims = profile.get("Entitlements", {})
    app_id = claims.get("com.apple.application-identifier")
    if len(team_ids) != 1 or app_id != f"{team_ids[0]}.{bundle_id}":
        raise ValueError("provisioning profile App ID does not match --bundle-id and team")
    with ENTITLEMENTS.open("rb") as file:
        entitlements = plistlib.load(file)
    entitlements["com.apple.application-identifier"] = app_id
    entitlements["com.apple.developer.team-identifier"] = team_ids[0]
    if not claims.get(AGE_RANGE_ENTITLEMENT):
        raise ValueError("provisioning profile lacks Declared Age Range: enable the capability on the "
                         "App ID and download the profile again (docs/releases/ios-app-store.md)")
    entitlements[AGE_RANGE_ENTITLEMENT] = True
    return entitlements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/darwin/client/release/Glob2.app")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/mac-app-store/Glob2.app")
    parser.add_argument("--identity", default="-", help="Apple Distribution identity; '-' makes a local test build")
    parser.add_argument("--installer-identity", help="Mac Installer Distribution identity for a submission .pkg")
    parser.add_argument("--profile", type=Path, help="App Store provisioning profile (required for .pkg)")
    parser.add_argument("--bundle-id", default="org.globulation2.glob2")
    # The iOS encoding (0.11.6.5 -> 0.11.605) gives every patch release its own
    # App Store version; the same app record carries both platforms.
    parser.add_argument("--version", default=marketing_version(PACKAGE_VERSION))
    parser.add_argument("--build", default="1", help="App Store build number (increase for every upload)")
    args = parser.parse_args()

    if args.installer_identity and args.identity == "-":
        parser.error("--installer-identity requires an Apple Distribution --identity")
    if args.installer_identity and not args.profile:
        parser.error("--installer-identity requires an App Store --profile")
    if args.profile and not args.profile.is_file():
        parser.error(f"provisioning profile not found: {args.profile}")
    if not args.build.isdecimal() or int(args.build) < 1:
        parser.error("--build must be a positive integer")
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("--version must contain three period-separated integers")
    if not args.bundle_id or "." not in args.bundle_id:
        parser.error("--bundle-id must be a reverse-DNS identifier")
    if args.output.is_symlink():
        parser.error("--output must not be a symlink")
    source = args.source.resolve()
    output = args.output.resolve()
    if output.suffix != ".app":
        parser.error("--output must end in .app")
    if not source.is_dir() or not (source / "Contents/MacOS/glob2").is_file():
        parser.error(f"not a built Glob2.app: {source}; run python3 tools/dev_build.py release=1 bundle")
    if source == output or source in output.parents or output in source.parents:
        parser.error("source and output must be separate directories")
    package = output.with_suffix(".pkg")
    package.unlink(missing_ok=True)
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output, symlinks=True)

    info_path = output / "Contents/Info.plist"
    with info_path.open("rb") as file:
        info = plistlib.load(file)
    info["CFBundleIdentifier"] = args.bundle_id
    info["CFBundleShortVersionString"] = args.version
    info["CFBundleVersion"] = args.build
    # The arm64 release and its bundled Homebrew libraries are built on macos-26.
    info["LSMinimumSystemVersion"] = "26.0"
    info["LSApplicationCategoryType"] = "public.app-category.strategy-games"
    info["NSHumanReadableCopyright"] = APP_COPYRIGHT
    with info_path.open("wb") as file:
        plistlib.dump(info, file)
    run("plutil", "-lint", info_path)
    write_icon(output / "Contents/Resources/Glob2.icns")
    if args.profile:
        embedded_profile = output / "Contents/embedded.provisionprofile"
        shutil.copyfile(args.profile, embedded_profile)
        embedded_profile.chmod(0o644)

    frameworks = output / "Contents/Frameworks"
    for code in [*sorted(frameworks.glob("*.dylib")), *sorted(frameworks.glob("*.framework"))]:
        run("codesign", "--force", "--sign", args.identity, code)
    for code in sorted((output / "Contents/MacOS").iterdir()):
        if code.is_file():
            run("codesign", "--force", "--sign", args.identity, code)
    if args.profile:
        entitlements = distribution_entitlements(args.profile, args.bundle_id)
        with tempfile.TemporaryDirectory() as directory:
            entitlements_path = Path(directory) / "AppStore.entitlements"
            with entitlements_path.open("wb") as file:
                plistlib.dump(entitlements, file)
            run("codesign", "--force", "--sign", args.identity,
                "--entitlements", entitlements_path, output)
    else:
        run("codesign", "--force", "--sign", args.identity,
            "--entitlements", ENTITLEMENTS, output)
    run("codesign", "--verify", "--deep", "--strict", "--verbose=1", output)
    signed = subprocess.check_output(
        ["codesign", "-d", "--entitlements", ":-", str(output)],
        stderr=subprocess.DEVNULL)
    signed_claims = plistlib.loads(signed)
    if not signed_claims.get("com.apple.security.app-sandbox"):
        raise RuntimeError("signed app is missing the App Sandbox entitlement")
    if args.profile and signed_claims.get("com.apple.application-identifier") != entitlements["com.apple.application-identifier"]:
        raise RuntimeError("signed app is missing the profile's App ID entitlement")

    if args.installer_identity:
        run("productbuild", "--sign", args.installer_identity,
            "--component", output, "/Applications", package)
        run("pkgutil", "--check-signature", package)
    print(f"App Store candidate: {output}")


if __name__ == "__main__":
    main()
