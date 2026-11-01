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
import tempfile
import urllib.request
import zipfile
from pathlib import PurePosixPath
from pathlib import Path

from package_steam_windows import stage as stage_windows_files
from tutorials.content import packaged_assets, reject_retired_assets


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ("data", "maps", "campaigns", "scripts")
UBUNTU_COPYRIGHT_ROOT = Path("/usr/share/doc")
COMMON_LICENSE_ROOT = Path("/usr/share/common-licenses")
REQUIRED_ASSETS = (
    "content/data/fonts/sans.ttf", "content/data/gfx/ressource0.png",
    "content/maps/SmallForTwo.map.gz",
) + packaged_assets(ROOT)
VERSION_PATTERN = re.compile(r'^PACKAGE_VERSION = "([0-9]+(?:\.[0-9]+)+)"$', re.MULTILINE)
LIBRARY_PATTERN = re.compile(r"^\s*(\S+)\s+=>\s+(/\S+)", re.MULTILINE)
SYSTEM_LIBRARY_PREFIXES = (
    "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so",
    "libresolv.so", "libnss_", "libanl.so", "libutil.so", "libGL",
    "libEGL", "libX", "libxcb", "libwayland", "libdrm", "libvulkan",
    "libxkb", "libudev", "libpulse", "libasound", "libdbus",
)


# Reviewed public binaries, not a request to rebuild the current source tree.
RELEASES = {
    "v0.11.4.0": {
        "source_commit": "a766230578dede730be43919240c5c80fafb44fe",
        "windows-x86_64": ("glob2-0.11.4.0-windows-x86_64.zip", "19d5149a64654b82c3197d5d9b8515408236dcf21735bb76160687dc4146467c"),
        "linux-x86_64": ("glob2-0.11.4.0-linux-x86_64-ubuntu22.04.tar.gz", "c5a7544ee2d818e4eac501875c64a4721bd9163e5bfdfa54fddcb93aae5fd0db"),
        "macos-arm64": ("Glob2-0.11.4.0-macos-arm64.dmg", "3cba8a18b1dc7b734d9b1db23057592728dc8f0de802a4e0bf4c8d3968661dbf"),
        "macos-x86_64": ("Glob2-0.11.4.0-macos-x86_64.dmg", "80e76085295007329b2d03dd780e0dfcc09864d382bb4b6aa86c56cf2f93fcf5"),
    },
}


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def version():
    match = VERSION_PATTERN.search((ROOT / "scons/build_layout.py").read_text())
    if not match:
        raise ValueError("PACKAGE_VERSION is missing")
    return match.group(1)


def preflight(tag, upstream_ref, source_root=None, reuse_released=False, mode="build"):
    if reuse_released and (mode != "build" or tag not in RELEASES):
        raise ValueError("reviewed released payloads are build-only")
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)+", tag):
        raise ValueError("tag must be vVERSION")
    if tag != f"v{version()}":
        raise ValueError("tag does not match PACKAGE_VERSION")
    head = command("git", "rev-parse", "HEAD")
    upstream = command("git", "rev-parse", f"{upstream_ref}^{{commit}}")
    if reuse_released and RELEASES[tag]["source_commit"] != upstream:
        raise ValueError("public tag differs from reviewed released source")
    subprocess.run(["git", "merge-base", "--is-ancestor", upstream, head], cwd=ROOT, check=True)
    if source_root is not None:
        source_root = Path(source_root).resolve()
        selected = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source_root, text=True).strip()
        if selected != upstream:
            raise ValueError("source checkout does not match public tag")
        changed = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"],
                                          cwd=source_root, text=True).strip()
        if changed:
            raise ValueError("source checkout contains tracked changes")
        match = VERSION_PATTERN.search((source_root / "scons/build_layout.py").read_text())
        if not match or tag != f"v{match.group(1)}":
            raise ValueError("source PACKAGE_VERSION does not match tag")
        return {"tag": tag, "version": match.group(1), "source_commit": upstream,
                "workflow_commit": head}
    # The mirror may carry release workflows, but game source must be identical
    # to the public tag selected for this release.
    game_paths = ("SConstruct", "scons", "src", "libs", "third_party", "content",
                  "platforms/macos", "vcpkg.json")
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
    source_root = (depot / "Glob2.app/Contents/Resources"
                   if platform == "macos" and not info.get("released_payload") else depot)
    source_offer = source_root / "SOURCE.txt"
    source_commit = info.get("source_commit")
    if not source_offer.is_file() or not re.fullmatch(r"[0-9a-f]{40}", source_commit or ""):
        raise ValueError("missing exact Corresponding Source offer")
    if f"https://github.com/Globulation2/glob2/tree/{source_commit}" not in source_offer.read_text():
        raise ValueError("Corresponding Source offer does not match build manifest")
    root = depot / "Glob2.app/Contents/Resources" if platform == "macos" else depot
    if platform == "linux":
        root = depot / "share/glob2"
    reject_retired_assets(root)
    for asset in REQUIRED_ASSETS:
        if not (root / asset).is_file() and not (
                asset.startswith("content/data/") and asset.endswith(".png") and
                (root / Path(asset).with_suffix(".webp")).is_file()):
            raise ValueError(f"missing asset: {asset}")
    executable = {"windows": depot / "glob2.exe",
                  "linux": depot / "start.sh",
                  "macos": depot / "Glob2.app/Contents/MacOS/glob2"}[platform]
    if not executable.is_file():
        raise ValueError(f"missing launch file: {executable}")
    if platform != "windows" and not executable.stat().st_mode & stat.S_IXUSR:
        raise ValueError(f"launch file is not executable: {executable}")
    if info.get("released_payload"):
        inventory_file = depot / "RELEASE-INVENTORY.json"
        if not inventory_file.is_file() or sha256(inventory_file) != info.get("release_inventory_sha256"):
            raise ValueError("released payload inventory digest mismatch")
        original = json.loads(inventory_file.read_text())
        if platform == "macos":
            if inventory(depot / "Glob2.app") != original:
                raise ValueError("released Mac bundle inventory changed")
        elif platform == "windows":
            actual_payload = inventory(depot)
            if any(actual_payload.get(path) != entry for path, entry in original.items()):
                raise ValueError("released Windows payload inventory changed")
        else:
            current = inventory(depot)
            for path, entry in original.items():
                mapped = ("bin/glob2" if path == "usr/bin/glob2" else
                          path[4:] if path.startswith("usr/share/glob2/") else
                          "lib/" + path[len("usr/lib/glob2/"):] if path.startswith("usr/lib/glob2/") else None)
                if mapped and (not path.startswith("usr/lib/glob2/") or (mapped in current and "sha256" in entry)):
                    if current.get(mapped) != entry:
                        raise ValueError("released Linux payload inventory changed")
        release = RELEASES.get(info.get("release_tag"), {})
        if (release.get("source_commit") != source_commit or info.get("version") != info.get("release_tag", "v")[1:]
                or tuple(info.get("release_asset", [])) != release.get(platform + "-" + info.get("architecture", ""))):
            raise ValueError("unreviewed released payload provenance")
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
    stage_windows_files(ROOT, args.exe.resolve(), args.runtime.resolve(), args.output.resolve(),
                        args.sdl_runtime.resolve() if args.sdl_runtime else None)
    write_source_offer(args.output, args.source_commit)
    write_manifest(args.output, metadata(args, "windows"))
    verify_manifest(args.output, "windows")


def dependencies(binary, library_path=None):
    env = os.environ.copy()
    if library_path is not None:
        previous = env.get("LD_LIBRARY_PATH")
        env["LD_LIBRARY_PATH"] = str(library_path) + (":" + previous if previous else "")
    result = subprocess.run(["ldd", str(binary)], capture_output=True, text=True, check=True, env=env)
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
    shutil.copytree(game_data, depot / "share/glob2", symlinks=True)
    for name in ("COPYING", "docs/assets/source-attribution.md"):
        source_root = Path(getattr(args, "source_root", None) or ROOT)
        shutil.copy2(source_root / name, depot / Path(name).name)
    write_source_offer(depot, args.source_commit)
    launcher = depot / "start.sh"
    launcher.write_text('#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
                        'export GLOB2_ASSET_DIR="$root/share/glob2"\n'
                        'export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                        'exec "$root/bin/glob2" "$@"\n')
    launcher.chmod(0o755)
    # Resolve the installed binary's original RPATH before copying dependencies
    # into the depot's flat lib directory used by the launcher.
    queue = [executable]
    seen = set()
    while queue:
        binary = queue.pop()
        for name, path in dependencies(binary, stage / "usr/lib/glob2"):
            if name in seen or name.startswith(SYSTEM_LIBRARY_PREFIXES):
                continue
            if not path.is_file():
                raise ValueError(f"missing linked library: {path}")
            seen.add(name)
            target = depot / "lib" / name
            shutil.copy2(path, target)
            queue.append(path)
    if not any((depot / "lib").glob("libSDL3.so*")):
        raise ValueError("SDL3 runtime was not bundled")
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


def inventory(root):
    """Record bytes, links and executable modes without dereferencing links."""
    result = {}
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            path.resolve(strict=True).relative_to(root.resolve())
            result[relative] = {"link": os.readlink(path)}
        elif path.is_file():
            result[relative] = {"sha256": sha256(path), "mode": stat.S_IMODE(path.stat().st_mode)}
        elif path.is_dir():
            result[relative] = {"directory_mode": stat.S_IMODE(path.stat().st_mode)}
        else:
            raise ValueError("unsupported release payload entry")
    return result


def assert_preserved(original, copied):
    if inventory(original) != inventory(copied):
        raise ValueError("released payload bytes, links or modes changed")


def unpack_release(archive, destination, expected_digest, windows=False):
    if sha256(archive) != expected_digest:
        raise ValueError("public release SHA256 mismatch")
    if destination.exists():
        raise ValueError("release extraction output exists")
    destination.mkdir(parents=True)
    if windows:
        with zipfile.ZipFile(archive) as packed:
            occupied = set()
            for entry in packed.infolist():
                path = PurePosixPath(entry.filename)
                key = path.as_posix().casefold()
                if (not path.parts or path.parts[0] != "Globulation2" or path.is_absolute()
                        or ".." in path.parts or "\\" in entry.filename
                        or any(":" in part for part in path.parts) or key in occupied
                        or stat.S_ISLNK(entry.external_attr >> 16) or entry.flag_bits & 1):
                    raise ValueError("unsafe or duplicate released ZIP member")
                occupied.add(key)
            packed.extractall(destination)
        return destination / "Globulation2"
    with tarfile.open(archive, "r:gz") as packed:
        # Python's data filter permits in-root symlinks/hardlinks, rejects traversal,
        # special files and links escaping the root. Never unpack unverified bytes.
        packed.extractall(destination, filter="data")
    inventory(destination)
    return destination


def preserve_source_offer(depot, source_commit):
    path = depot / "SOURCE.txt"
    if path.exists():
        if source_commit not in path.read_text():
            raise ValueError("released source offer differs; do not overwrite payload")
    else:
        write_source_offer(depot, source_commit)


def ubuntu_runtime_host():
    text = Path("/etc/os-release").read_text()
    values = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    if values.get("ID", "").strip('"') != "ubuntu" or values.get("VERSION_ID", "").strip('"') != "22.04":
        raise ValueError("released Linux closure must use Ubuntu 22.04 runtime provenance")
    return {"os_release": text, "github_image_version": os.environ.get("ImageVersion", "")}


def record_host_library(path, depot, records):
    """Keep actual Ubuntu package notices and versions; never infer licence rights."""
    try:
        owners = subprocess.check_output(["dpkg-query", "-S", str(path)], text=True)
    except subprocess.CalledProcessError:
        owners = subprocess.check_output(["dpkg-query", "-S", str(path.resolve())], text=True)
    owner = owners.splitlines()[0].split(": ", 1)[0]
    details = subprocess.check_output(["dpkg-query", "-W", "-f=${Package}\t${Version}\t${source:Package}\t${source:Version}", owner], text=True).split("\t")
    package = details[0]
    notice = UBUNTU_COPYRIGHT_ROOT / package / "copyright"
    if not notice.is_file():
        raise ValueError("missing actual copyright notice for bundled Ubuntu runtime: " + package)
    target = depot / "licenses/ubuntu-runtime" / package / "copyright"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(notice, target)
    common = []
    references = re.findall(r"/usr/share/common-licenses/([A-Za-z0-9.+-]+)", notice.read_text(errors="replace"))
    # Notices often finish a sentence with the path. Keep version dots (LGPL-2.1),
    # remove only terminal prose periods, and still require the actual file.
    for name in sorted({reference.rstrip(".") for reference in references}):
        actual = COMMON_LICENSE_ROOT / name
        if not actual.is_file():
            raise ValueError("referenced common licence is missing: " + name)
        copied = depot / "licenses/ubuntu-runtime/common-licenses" / name
        copied.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(actual, copied)
        common.append({"name": name, "sha256": sha256(actual)})
    records[path.name] = {"sha256": sha256(path), "package": package, "version": details[1],
                          "source_package": details[2], "source_version": details[3],
                          "copyright_sha256": sha256(notice), "referenced_common_licences": common}


def stage_released(args):
    release = RELEASES.get(args.tag)
    key = args.platform + "-" + args.architecture
    if (not release or key not in release or release["source_commit"] != args.source_commit
            or args.version != args.tag[1:]):
        raise ValueError("release tag/source/architecture lacks reviewed binary pins")
    if args.output.exists():
        raise ValueError("depot output exists")
    asset, digest = release[key]
    with tempfile.TemporaryDirectory(prefix="glob2-gog-released-") as temporary:
        temp = Path(temporary)
        archive = args.archive or temp / asset
        if args.archive is None:
            urllib.request.urlretrieve(f"https://github.com/Globulation2/glob2/releases/download/{args.tag}/{asset}", archive)
        if sha256(archive) != digest:
            raise ValueError("public release SHA256 mismatch")
        original_inventory = None
        host_libraries = {}
        if args.platform == "macos":
            mount = temp / "mount"; mount.mkdir()
            subprocess.run(["codesign", "--verify", "--strict", str(archive)], check=True)
            subprocess.run(["xcrun", "stapler", "validate", str(archive)], check=True)
            subprocess.run(["hdiutil", "attach", str(archive.resolve()), "-readonly", "-nobrowse", "-mountpoint", str(mount)], check=True)
            try:
                app = mount / "Glob2.app"
                original_inventory = inventory(app)
                args.output.mkdir(parents=True)
                subprocess.run(["ditto", str(app), str(args.output / "Glob2.app")], check=True)
                assert_preserved(app, args.output / "Glob2.app")
            finally:
                subprocess.run(["hdiutil", "detach", str(mount)], check=True)
            copied = args.output / "Glob2.app"
            arch = subprocess.check_output(["lipo", "-archs", str(copied / "Contents/MacOS/glob2")], text=True).strip()
            if arch != args.architecture:
                raise ValueError("released Mac architecture differs from candidate label")
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(copied)], check=True)
            subprocess.run(["xcrun", "stapler", "validate", str(copied)], check=True)
            subprocess.run(["spctl", "--assess", "--type", "execute", str(copied)], check=True)
        else:
            payload = unpack_release(archive, temp / "extracted", digest, args.platform == "windows")
            original_inventory = inventory(payload)
            if args.platform == "windows":
                shutil.copytree(payload, args.output)
                assert_preserved(payload, args.output)
            else:
                host = ubuntu_runtime_host()
                # Existing staging handles closure with the released private libs
                # preferred over the host. It never recompiles or patches the ELF.
                stage_linux(argparse.Namespace(stage=payload, output=args.output, version=args.version,
                    source_commit=args.source_commit, workflow_commit=args.workflow_commit, source_root=args.source_root))
                if sha256(payload / "usr/bin/glob2") != sha256(args.output / "bin/glob2"):
                    raise ValueError("released Linux executable changed")
                assert_preserved(payload / "usr/share/glob2", args.output / "share/glob2")
                private = payload / "usr/lib/glob2"
                for bundled in (args.output / "lib").iterdir():
                    matching = private / bundled.name
                    if matching.is_file():
                        if sha256(matching) != sha256(bundled):
                            raise ValueError("released private library was replaced")
                    else:
                        linked = dict(dependencies(payload / "usr/bin/glob2", private))
                        # Transitive entries are also returned by ldd.
                        if bundled.name not in linked:
                            raise ValueError("cannot establish bundled runtime provenance")
                        record_host_library(linked[bundled.name], args.output, host_libraries)
        # Preserve released payload bytes: additions must never replace them.
        for reserved in ("RELEASE-INVENTORY.json", "gog-build.json", "SHA256SUMS.txt"):
            if args.platform == "windows" and (payload / reserved).exists():
                raise ValueError("depot metadata would overwrite released payload")
        preserve_source_offer(args.output, args.source_commit)
        inv = args.output / "RELEASE-INVENTORY.json"
        inv.write_text(json.dumps(original_inventory, sort_keys=True, indent=2) + "\n")
        info = metadata(args, args.platform)
        info.update(released_payload=True, release_tag=args.tag, release_asset=[asset, digest],
                    architecture=args.architecture, release_inventory_sha256=sha256(inv),
                    ubuntu_runtime=host_libraries, runtime_licence_review_required=bool(host_libraries))
        if args.platform == "linux":
            info["runtime_host"] = host
        write_manifest(args.output, info)
        verify_manifest(args.output, args.platform)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("preflight")
    check.add_argument("--tag", required=True)
    check.add_argument("--upstream-ref", required=True)
    check.add_argument("--github-output", type=Path)
    check.add_argument("--source-root", type=Path)
    check.add_argument("--reuse-released", action="store_true")
    check.add_argument("--mode", choices=("build", "stage"), default="build")
    for platform in ("windows", "linux", "macos"):
        part = sub.add_parser(f"stage-{platform}")
        part.add_argument("--source-root", type=Path)
        part.add_argument("--output", type=Path, required=True)
        part.add_argument("--version", required=True)
        part.add_argument("--source-commit", required=True)
        part.add_argument("--workflow-commit", required=True)
        if platform == "windows":
            part.add_argument("--exe", type=Path, required=True)
            part.add_argument("--runtime", type=Path, required=True)
            part.add_argument("--sdl-runtime", type=Path)
        elif platform == "linux":
            part.add_argument("--stage", type=Path, required=True)
        else:
            part.add_argument("--app", type=Path, required=True)
    released = sub.add_parser("stage-released")
    released.add_argument("--tag", choices=RELEASES, required=True)
    released.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    released.add_argument("--architecture", choices=("x86_64", "arm64"), required=True)
    released.add_argument("--archive", type=Path)
    released.add_argument("--output", type=Path, required=True)
    released.add_argument("--version", required=True)
    released.add_argument("--source-commit", required=True)
    released.add_argument("--workflow-commit", required=True)
    released.add_argument("--source-root", type=Path, required=True)
    verify = sub.add_parser("verify")
    verify.add_argument("--depot", type=Path, required=True)
    verify.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    extract = sub.add_parser("extract")
    extract.add_argument("--archive", type=Path, required=True)
    extract.add_argument("--depot", type=Path, required=True)
    extract.add_argument("--platform", choices=("windows", "linux", "macos"), required=True)
    args = parser.parse_args()
    if args.command == "preflight":
        result = preflight(args.tag, args.upstream_ref, args.source_root, args.reuse_released, args.mode)
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
        if args.source_root:
            global ROOT
            ROOT = args.source_root.resolve()
            selected = command("git", "rev-parse", "HEAD")
            if selected != args.source_commit:
                raise ValueError("staging source checkout does not match source commit")
            changed = command("git", "status", "--porcelain", "--untracked-files=no")
            if changed:
                raise ValueError(f"staging source checkout contains tracked changes:\n{changed}")
        globals()[args.command.replace("-", "_")](args)


if __name__ == "__main__":
    main()
