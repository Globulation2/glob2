"""Repackage a digest-pinned public Windows release without rebuilding it."""

import argparse
import hashlib
import importlib.util
import json
import re
import shutil
import stat
import tempfile
import urllib.request
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

spec = importlib.util.spec_from_file_location("store", Path(__file__).with_name("windows_store_release.py"))
store = importlib.util.module_from_spec(spec)
spec.loader.exec_module(store)

RELEASES = {
    "v0.11.4.0": {
        "commit": "a766230578dede730be43919240c5c80fafb44fe",
        "asset": "glob2-0.11.4.0-windows-x86_64.zip",
        "sha256": "19d5149a64654b82c3197d5d9b8515408236dcf21735bb76160687dc4146467c",
    },
}


def stage_archive(archive, destination, expected_digest):
    """Validate the complete archive before replacing the destination."""
    with open(archive, "rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != expected_digest:
        raise RuntimeError("Public portable release SHA256 mismatch")
    with ZipFile(archive) as bundle:
        members = []
        occupied = set()
        for entry in bundle.infolist():
            path = PurePosixPath(entry.filename)
            if (not path.parts or path.parts[0] != "Globulation2"
                    or path.is_absolute() or ".." in path.parts or "\\" in entry.filename
                    or any(":" in part for part in path.parts)
                    or stat.S_ISLNK(entry.external_attr >> 16) or entry.flag_bits & 1):
                raise RuntimeError(f"Unsafe portable archive member: {entry.filename}")
            relative = Path(*path.parts[1:])
            if entry.is_dir():
                continue
            key = relative.as_posix().casefold()
            if key in occupied:
                raise RuntimeError(f"Case-insensitive portable filename collision: {relative}")
            occupied.add(key)
            members.append((entry, relative))
        required = {"glob2.exe", "copying", "data/icons/glob2-icon-128x128.webp"}
        if not required <= occupied:
            raise RuntimeError("Portable release is missing executable, license or icon")
        # Read every member (including CRC checks) before replacing any output.
        with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
            staged = Path(temporary) / "content"
            staged.mkdir()
            for entry, relative in members:
                target = staged / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with bundle.open(entry) as source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
            store.normalize_store_license_filenames(staged)
            if destination.exists():
                shutil.rmtree(destination)
            shutil.move(str(staged), destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", choices=RELEASES, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--identity-name", required=True)
    parser.add_argument("--publisher", required=True)
    parser.add_argument("--publisher-display-name", required=True)
    parser.add_argument("--store-id", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    if (not re.fullmatch(r"[1-9]\d*(?:\.\d+){2}\.0", args.version)
            or any(int(part) > 65535 for part in args.version.split("."))):
        parser.error("invalid four-part Store version")
    if not re.fullmatch(r"[A-Z0-9]{12}", args.store_id):
        parser.error("invalid Partner Center Store ID")
    release = RELEASES[args.tag]
    args.dest = args.dest.resolve()
    args.dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        archive = args.archive or Path(temporary) / release["asset"]
        if args.archive is None:
            url = f"https://github.com/Globulation2/glob2/releases/download/{args.tag}/{release['asset']}"
            urllib.request.urlretrieve(url, archive)
        stage_archive(archive, args.dest, release["sha256"])
    store.write_game_config(args.dest, args)
    store.write_shell_images(args.dest / "data/icons/glob2-icon-128x128.webp", args.dest)
    (args.dest / "SOURCE.txt").write_text(
        "Globulation 2 is licensed under GPL version 3.\nCorresponding payload source:\n"
        f"https://github.com/Globulation2/glob2/archive/{release['commit']}.zip\n", encoding="utf-8")
    print(json.dumps({"payload_tag": args.tag, **release}, sort_keys=True))


if __name__ == "__main__":
    main()
