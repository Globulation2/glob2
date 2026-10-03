#!/usr/bin/env python3
"""Stage and validate GOG depots without requiring GOG credentials."""

import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import tarfile
from pathlib import Path

from package_steam_windows import stage as stage_windows_files


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ("data", "maps", "campaigns", "scripts")
REQUIRED_ASSETS = (
    "data/fonts/sans.ttf", "data/gfx/ressource0.png",
    "maps/SmallForTwo.map.gz", "campaigns/Tutorial_Campaign.txt",
    "scripts/tutorial_part1.sgsl",
)
VERSION_PATTERN = re.compile(r'^PACKAGE_VERSION = "([0-9]+(?:\.[0-9]+)+)"$', re.MULTILINE)
LIBRARY_PATTERN = re.compile(r"^\s*(\S+)\s+=>\s+(/\S+)", re.MULTILINE)
SYSTEM_LIBRARY_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so",
    "libresolv.so", "libnss_", "libanl.so", "libutil.so", "libGL",
    "libEGL", "libX", "libxcb", "libwayland", "libdrm", "libvulkan",
    "libxkb", "libudev", "libpulse", "libasound", "libdbus",
)


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def version():
    match = VERSION_PATTERN.search((ROOT / "scons/build_layout.py").read_text())
    if not match:
        raise ValueError("PACKAGE_VERSION is missing")
    return match.group(1)


def preflight(tag, upstream_ref):
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)+", tag):
        raise ValueError("tag must be vVERSION")
    if tag != f"v{version()}":
        raise ValueError("tag does not match PACKAGE_VERSION")
    head = command("git", "rev-parse", "HEAD")
    upstream = command("git", "rev-parse", f"{upstream_ref}^{{commit}}")
    subprocess.run(["git", "merge-base", "--is-ancestor", upstream, head], cwd=ROOT, check=True)
    # The mirror may carry release workflows, but game source must be identical
    # to the public tag selected for this release.
    game_paths = ("SConstruct", "scons", "src", "libgag", "libusl", "data",
                  "maps", "campaigns", "scripts", "darwin/Info.plist",
                  "darwin/Glob2.icns", "vcpkg.json")
    changed = command("git", "diff", "--name-only", upstream, head, "--", *game_paths)
    if changed:
        raise ValueError(f"game source differs from public tag:\n{changed}")
    return {"tag": tag, "version": version(), "source_commit": upstream,
            "workflow_commit": head}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def files_in(root):
    for path in sorted(root.rglob("*")):
        if path.is_file():
            yield path


def write_manifest(depot, metadata):
    manifest = depot / "SHA256SUMS.txt"
    info = depot / "gog-build.json"
    info.write_text(json.dumps(metadata, sort_keys=True, indent=2) + "\n")
    with manifest.open("w", encoding="utf-8") as out:
        for path in files_in(depot):
            if path != manifest:
                out.write(f"{sha256(path)}  {path.relative_to(depot).as_posix()}\n")


def verify_manifest(depot, platform):
    manifest = depot / "SHA256SUMS.txt"
    if not manifest.is_file():
        raise ValueError(f"missing manifest: {manifest}")
    listed = set()
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError(f"malformed manifest line: {line}")
        relative = Path(match.group(2))
        if relative.is_absolute() or ".." in relative.parts or relative.as_posix() in listed:
            raise ValueError(f"unsafe manifest path: {relative}")
        listed.add(relative.as_posix())
        path = depot / relative
        try:
            path.resolve(strict=True).relative_to(depot.resolve())
        except (FileNotFoundError, ValueError):
            raise ValueError(f"depot path escapes its root: {relative}") from None
        if not path.is_file() or sha256(path) != match.group(1):
            raise ValueError(f"missing or modified depot file: {relative}")
    actual = {p.relative_to(depot).as_posix() for p in files_in(depot) if p != manifest}
    if actual != listed:
        raise ValueError(f"depot inventory differs: {sorted(actual ^ listed)}")
    info = json.loads((depot / "gog-build.json").read_text())
    if info.get("platform") != platform:
        raise ValueError("platform manifest mismatch")
    source_root = depot / "Glob2.app/Contents/Resources" if platform == "macos" else depot
    source_offer = source_root / "SOURCE.txt"
    source_commit = info.get("source_commit")
    if not source_offer.is_file() or not re.fullmatch(r"[0-9a-f]{40}", source_commit or ""):
        raise ValueError("missing exact Corresponding Source offer")
    if f"https://github.com/Globulation2/glob2/tree/{source_commit}" not in source_offer.read_text():
        raise ValueError("Corresponding Source offer does not match build manifest")
    root = depot / "Glob2.app/Contents/Resources" if platform == "macos" else depot
    if platform == "linux":
        root = depot / "share/glob2"
    for asset in REQUIRED_ASSETS:
        if not (root / asset).is_file():
            raise ValueError(f"missing asset: {asset}")
    executable = {"windows": depot / "glob2.exe",
                  "linux": depot / "start.sh",
                  "macos": depot / "Glob2.app/Contents/MacOS/glob2"}[platform]
    if not executable.is_file():
        raise ValueError(f"missing launch file: {executable}")
    if platform != "windows" and not executable.stat().st_mode & stat.S_IXUSR:
        raise ValueError(f"launch file is not executable: {executable}")
    return info


def extract_archive(archive, depot, platform):
    if depot.exists():
        raise ValueError(f"output exists: {depot}")
    depot.mkdir(parents=True)
    with tarfile.open(archive, "r:gz") as packed:
        packed.extractall(depot, filter="data")
    return verify_manifest(depot, platform)


def metadata(args, platform):
    return {"platform": platform, "version": args.version,
            "source_commit": args.source_commit,
            "workflow_commit": args.workflow_commit}


def write_source_offer(root, source_commit):
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source commit must be a full Git SHA")
    (root / "SOURCE.txt").write_text(
        "Globulation 2 Corresponding Source\n\n"
        "The source code and build scripts for this exact release are available at:\n"
        f"https://github.com/Globulation2/glob2/tree/{source_commit}\n\n"
        "Download the source archive at:\n"
        f"https://github.com/Globulation2/glob2/archive/{source_commit}.tar.gz\n"
    )


def stage_windows(args):
    stage_windows_files(ROOT, args.exe.resolve(), args.runtime.resolve(), args.output.resolve())
    write_source_offer(args.output, args.source_commit)
    write_manifest(args.output, metadata(args, "windows"))
    verify_manifest(args.output, "windows")


def dependencies(binary):
    result = subprocess.run(["ldd", str(binary)], capture_output=True, text=True, check=True)
    if "not found" in result.stdout:
        raise ValueError(f"unresolved library in {binary}: {result.stdout}")
    return [(name, Path(path)) for name, path in LIBRARY_PATTERN.findall(result.stdout)]


def stage_linux(args):
    depot = args.output.resolve()
    if depot.exists():
        raise ValueError(f"output exists: {depot}")
    stage = args.stage.resolve()
    executable = stage / "usr/bin/glob2"
    game_data = stage / "usr/share/glob2"
    if not executable.is_file() or not game_data.is_dir():
        raise ValueError("incomplete staged Linux install")
    (depot / "bin").mkdir(parents=True)
    (depot / "share").mkdir()
    (depot / "lib").mkdir()
    shutil.copy2(executable, depot / "bin/glob2")
    shutil.copytree(game_data, depot / "share/glob2")
    for name in ("COPYING", "docs/assets/source-attribution.md"):
        shutil.copy2(ROOT / name, depot / Path(name).name)
    write_source_offer(depot, args.source_commit)
    launcher = depot / "start.sh"
    launcher.write_text('#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                        'export GLOB2_ASSET_DIR="$root/share/glob2"\n'
                        'export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                        'exec "$root/bin/glob2" "$@"\n')
    launcher.chmod(0o755)
    queue = [depot / "bin/glob2"]
    seen = set()
    while queue:
        binary = queue.pop()
        for name, path in dependencies(binary):
            if name in seen or name.startswith(SYSTEM_LIBRARY_PREFIXES):
                continue
            if not path.is_file():
                raise ValueError(f"missing linked library: {path}")
            seen.add(name)
            target = depot / "lib" / name
            shutil.copy2(path, target)
            queue.append(target)
    if not any((depot / "lib").glob("libSDL2-*.so*")):
        raise ValueError("SDL2 runtime was not bundled")
    write_manifest(depot, metadata(args, "linux"))
    verify_manifest(depot, "linux")


def stage_macos(args):
    depot = args.output.resolve()
    if depot.exists():
        raise ValueError(f"output exists: {depot}")
    depot.mkdir(parents=True)
    shutil.copytree(args.app.resolve(), depot / "Glob2.app", symlinks=True)
    if not (depot / "Glob2.app/Contents/Resources/COPYING").is_file():
        raise ValueError("macOS bundle lacks COPYING")
    write_source_offer(depot / "Glob2.app/Contents/Resources", args.source_commit)
    subprocess.run(["codesign", "--force", "--sign", "-", str(depot / "Glob2.app")], check=True)
    write_manifest(depot, metadata(args, "macos"))
    verify_manifest(depot, "macos")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("preflight")
    check.add_argument("--tag", required=True)
    check.add_argument("--upstream-ref", required=True)
    check.add_argument("--github-output", type=Path)
    for platform in ("windows", "linux", "macos"):
        part = sub.add_parser(f"stage-{platform}")
        part.add_argument("--output", type=Path, required=True)
        part.add_argument("--version", required=True)
        part.add_argument("--source-commit", required=True)
        part.add_argument("--workflow-commit", required=True)
        if platform == "windows":
            part.add_argument("--exe", type=Path, required=True)
            part.add_argument("--runtime", type=Path, required=True)
        elif platform == "linux":
            part.add_argument("--stage", type=Path, required=True)
        else:
            part.add_argument("--app", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--depot", type=Path, required=True)
    verify.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    extract = sub.add_parser("extract")
    extract.add_argument("--archive", type=Path, required=True)
    extract.add_argument("--depot", type=Path, required=True)
    extract.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    args = parser.parse_args()
    if args.command == "preflight":
        result = preflight(args.tag, args.upstream_ref)
        if args.github_output:
            with args.github_output.open("a") as out:
                for key, value in result.items():
                    print(f"{key}={value}", file=out)
        print(json.dumps(result, sort_keys=True))
    elif args.command == "verify":
        print(json.dumps(verify_manifest(args.depot, args.platform), sort_keys=True))
    elif args.command == "extract":
        print(json.dumps(extract_archive(args.archive, args.depot, args.platform), sort_keys=True))
    else:
        globals()[args.command.replace("-", "_")](args)


if __name__ == "__main__":
    main()
