#!/usr/bin/env python3
"""Validate a release checkout and create its source archive."""

import argparse
import gzip
import hashlib
import json
import re
import struct
import subprocess
import tarfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION_FILE = ROOT / "scons" / "build_layout.py"


def version():
    match = re.search(r'^PACKAGE_VERSION = "([0-9]+(?:\.[0-9]+)+)"$',
                      VERSION_FILE.read_text(), re.MULTILINE)
    if not match:
        raise SystemExit("PACKAGE_VERSION is missing or invalid")
    return match.group(1)


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True,
                                       stderr=subprocess.PIPE).strip()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"git {' '.join(args)} failed: {error.stderr.strip()}") from None


def validate(tag=None):
    current = version()
    spec = (ROOT / "fedora" / "glob2.spec").read_text()
    spec_version = re.search(r"^Version:\s*(\S+)\s*$", spec, re.MULTILINE)
    if spec_version is None or spec_version.group(1) != current:
        raise SystemExit(f"Fedora spec Version must match PACKAGE_VERSION {current}")
    if json.loads((ROOT / "vcpkg.json").read_text())["version"] != current:
        raise SystemExit(f"vcpkg.json version must match PACKAGE_VERSION {current}")
    if tag is not None:
        if tag != f"v{current}":
            raise SystemExit(f"tag {tag!r} does not match PACKAGE_VERSION {current!r}")
        if git("rev-parse", "HEAD") != git("rev-parse", f"refs/tags/{tag}^{{commit}}"):
            raise SystemExit(f"HEAD is not the commit tagged {tag}")
        metainfo = ET.parse(ROOT / "data" / "org.globulation2.Globulation2.metainfo.xml").getroot()
        if metainfo.find(f"./releases/release[@version='{current}']") is None:
            raise SystemExit(f"AppStream release notes for {current} are required before publishing")
        screenshot = metainfo.find("./screenshots/screenshot/image")
        screenshot_url = (screenshot.text or "").strip() if screenshot is not None else ""
        if not screenshot_url.startswith("https://"):
            raise SystemExit("a hosted gameplay screenshot is required before publishing")
        screenshot_path = ROOT / "data" / "screenshots" / "globulation2-gameplay.png"
        try:
            with urllib.request.urlopen(screenshot_url, timeout=15) as response:
                hosted_screenshot = response.read()
        except (OSError, urllib.error.URLError) as error:
            raise SystemExit(f"gameplay screenshot URL is unavailable: {error}") from None
        if hashlib.sha256(hosted_screenshot).digest() != hashlib.sha256(screenshot_path.read_bytes()).digest():
            raise SystemExit("hosted gameplay screenshot does not match the release checkout")
        icon = ROOT / "data" / "icons" / "glob2-icon-256x256.png"
        png = icon.read_bytes() if icon.is_file() else b""
        if len(png) < 24 or png[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", png[16:24]) != (256, 256):
            raise SystemExit("a 256x256 PNG application icon is required before publishing")
    return current


def archive(destination, tag=None):
    current = validate(tag)
    destination.mkdir(parents=True, exist_ok=True)
    prefix = f"glob2-{current}/"
    raw = subprocess.check_output(
        ["git", "archive", "--format=tar", f"--prefix={prefix}", "HEAD"], cwd=ROOT)
    target = destination / f"glob2-{current}.tar.gz"
    # A fixed gzip header makes repeat archives of the same commit byte-identical.
    with target.open("wb") as output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as packed:
            packed.write(raw)
    with tarfile.open(target, "r:gz") as packed:
        names = set(packed.getnames())
        required = {prefix + name for name in (
            "SConstruct", "scons/build_layout.py", "data/glob2.desktop",
            "data/org.globulation2.Globulation2.metainfo.xml",
            "data/screenshots/globulation2-gameplay.png",
            "data/usl/Language/Runtime/Control.usl",
            "data/fonts/sans.ttf", "maps/SmallForTwo.map.gz")}
        missing = required - names
        if missing:
            raise SystemExit(f"source archive is missing {sorted(missing)}")
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    (destination / "SHA256SUMS").write_text(f"{digest}  {target.name}\n")
    return target


def verify_install(stage):
    required = (
        "usr/bin/glob2", "usr/share/applications/org.globulation2.Globulation2.desktop",
        "usr/share/metainfo/org.globulation2.Globulation2.metainfo.xml",
        "usr/share/icons/hicolor/128x128/apps/glob2.png",
        "usr/share/icons/hicolor/256x256/apps/glob2.png",
        "usr/share/glob2/data/fonts/sans.ttf",
        "usr/share/glob2/data/maxima/duel.strategy",
        "usr/share/glob2/data/usl/Language/Runtime/Control.usl",
        "usr/share/glob2/data/gfx/ressource0.png",
        "usr/share/glob2/maps/SmallForTwo.map.gz",
        "usr/share/glob2/campaigns/Tutorial_Campaign.txt",
        "usr/share/glob2/scripts/tutorial_part1.sgsl",
    )
    missing = [name for name in required if not (stage / name).is_file()
               and not (name.startswith('usr/share/glob2/data/') and name.endswith('.png')
                        and (stage / Path(name).with_suffix('.webp')).is_file())]
    if missing:
        raise SystemExit(f"incomplete installation: {missing}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("check", "archive", "verify-install"))
    parser.add_argument("--tag", help="require HEAD to match this release tag")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "release")
    parser.add_argument("--stage", type=Path)
    args = parser.parse_args()
    if args.command == "check":
        print(validate(args.tag))
    elif args.command == "archive":
        print(archive(args.output, args.tag))
    else:
        if args.stage is None:
            parser.error("verify-install requires --stage")
        verify_install(args.stage)
        print(f"verified {args.stage}")


if __name__ == "__main__":
    main()
