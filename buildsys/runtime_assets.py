"""Shared runtime-asset export and release installation."""

import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

STAMP = ".runtime-assets.json.gz"


def optimized_install_enabled(release, choice="auto"):
    """Let distro builds select optimized assets independently of compiler flags."""
    if str(choice).lower() == "auto":
        return bool(release)
    if str(choice).lower() not in ("0", "1", "false", "true"):
        raise ValueError("optimized_assets must be auto, 0, or 1")
    return str(choice).lower() in ("1", "true")


def sprite_packing_profile(choice, release, video_supported=True):
    if choice == "auto":
        choice = "video" if release else "legacy"
    if choice not in ("legacy", "webp", "video"):
        raise ValueError("sprite_packing must be auto, legacy, webp, or video")
    return "webp" if choice == "video" and not video_supported else choice


def install_export(exported, destination):
    """Merge owned runtime files while preserving unrelated installed content."""
    exported, destination = Path(exported), Path(destination)
    audit = json.loads(exported.with_suffix(".json").read_text())
    current = {item["output"]: item["output_sha256"] for item in audit["files"]}
    marker = destination / STAMP
    legacy_install = not marker.is_file()
    previous = {}
    if marker.is_file():
        record = json.loads(gzip.decompress(marker.read_bytes()))
        if record["policy"] != "runtime-assets-v1":
            raise ValueError("Unknown installed asset policy")
        previous = record["files"]
    obsolete = set(previous) - set(current)
    # A legacy install has no hashes for its shipped artwork. Replace PNGs at
    # current managed image paths even when an older release had different art;
    # obsolete bundled PNG copies would waste space. User-profile
    # overrides live outside this installation tree and keep their precedence.
    for item in audit["files"]:
        if item["source"] != item["output"]:
            old = destination / item["source"]
            if old.is_file() and (
                legacy_install
                or hashlib.sha256(old.read_bytes()).hexdigest() == item["source_sha256"]
            ):
                obsolete.add(item["source"])
        # Frames now packed into a sheet: the loader prefers the sheet, so
        # shipped copies left by an older install are dead weight.
        for frame in item.get("packed_from", ()):
            old = destination / frame["source"]
            if old.is_file() and (
                legacy_install
                or hashlib.sha256(old.read_bytes()).hexdigest() == frame["sha256"]
            ):
                obsolete.add(frame["source"])
    for relative in obsolete:
        path = destination / relative
        if not path.resolve().is_relative_to(destination.resolve()):
            raise ValueError("Invalid installed asset path: " + relative)
        if path.is_file() and (
            relative not in previous
            or hashlib.sha256(path.read_bytes()).hexdigest() == previous[relative]
        ):
            path.unlink()
    for relative in ("data/highres/v1/manifest.json", "data/highres/v1/README.md"):
        (destination / relative).unlink(missing_ok=True)
    for relative in current:
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(exported / relative, target)
    marker.parent.mkdir(parents=True, exist_ok=True)
    record = dict(policy=audit["policy"], files=current)
    marker.write_bytes(
        gzip.compress(json.dumps(record, sort_keys=True).encode(), mtime=0)
    )


# Export tools whose content is part of the shared export key.
EXPORT_TOOLS = (
    "tools/package_assets.py",
    "tools/packed_sprite_assets.py",
    "buildsys/recording_dependencies.py",
    "buildsys/recording-versions.json",
    "tools/terrain_tileset.py",
    "tools/terrain_catalog.py",
    "tools/asset-requirements.txt",
    "tools/image_encoding.json",
    "buildsys/runtime_assets.py",
    "buildsys/shared_assets.py",
)


def host_platform():
    return (
        "macos"
        if sys.platform == "darwin"
        else "windows"
        if sys.platform == "win32"
        else "linux"
    )


def export_inputs(root, platform):
    """The asset source files the export reads."""
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    from tools.package_assets import source_files

    return [str(p) for p in source_files(root, platform)]


def export_tree(root, exported, platform, inputs, lossy, packing, video, jobs):
    """Fill `exported` (and its sibling audit JSON), from the shared store when possible.

    The CMake build exports through here (buildsys/cmake_generate.py runtime-assets)."""
    # Importable as buildsys.runtime_assets too (test/build_system), so name the sibling by path.
    if str(Path(__file__).resolve().parent) not in sys.path:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
    import shared_assets
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    from tools.package_assets import export_assets

    def produce(output):
        export_assets(root, output, platform=platform, lossy=lossy, jobs=jobs,
                      terrain_sheets=packing != "legacy", packed_hd_sprites=packing != "legacy",
                      video_sprites=video)

    # Checkouts with the same asset inputs share one export (buildsys/shared_assets.py).
    key = shared_assets.content_key(root, inputs + [root / path for path in EXPORT_TOOLS],
                                    ["runtime-assets-v1", platform, lossy, packing, video, sys.version_info[:2]])
    shared_assets.export(root, exported, key, produce)

