"""Shared asset export and SCons installation graph for release installs."""

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


def prepare_assets(env):
    from SCons.Script import Action, Value, GetOption

    if env.get("RUNTIME_ASSET_STAMP") is not None:
        return env["RUNTIME_ASSET_STAMP"]
    root = Path.cwd()
    sys.path.insert(0, str(root))
    from tools.package_assets import export_assets, source_files

    platform = (
        "macos"
        if sys.platform == "darwin"
        else "windows"
        if sys.platform == "win32"
        else "linux"
    )
    exported = Path(env["BUILDDIR"]).resolve() / "runtime-assets"
    inputs = [str(p) for p in source_files(root, platform)]

    lossy = optimized_install_enabled(env.get("release", True), env.get("optimized_assets", "auto"))

    packing = sprite_packing_profile(env.get("sprite_packing", "auto"), env.get("release", True),
                                     "GLOB2_H264_ASSETS" in env.get("CPPDEFINES", []))
    video = packing == "video"

    def export(target, source, env):
        export_assets(root, exported, platform=platform, lossy=lossy, jobs=GetOption("num_jobs"),
                      terrain_sheets=packing != "legacy", packed_hd_sprites=packing != "legacy",
                      video_sprites=video)
        return 0

    stamp = env.Command(
        str(exported.with_suffix(".json")),
        inputs
        + [
            "tools/package_assets.py",
            "tools/packed_sprite_assets.py",
            "scons/recording_dependencies.py",
            "scons/recording-versions.json",
            "tools/terrain_tileset.py",
            "tools/terrain_catalog.py",
            "tools/asset-requirements.txt",
            "tools/image_encoding.json",
            "scons/runtime_assets.py",
            Value(inputs),
            Value([lossy, packing, video]),
        ],
        Action(export, "Exporting verified runtime assets"),
    )
    env.Precious(stamp)  # SCons must not unlink the previous ownership audit.
    if not (exported / "data").is_dir():
        env.AlwaysBuild(stamp)
    env["RUNTIME_ASSET_STAMP"] = stamp
    return stamp


def install_assets(env):
    from SCons.Script import Action

    stamp = prepare_assets(env)
    exported = Path(env["BUILDDIR"]).resolve() / "runtime-assets"
    destination = Path(env["INSTALLDIR"]) / "glob2"

    def install(target, source, env):
        install_export(exported, destination)
        return 0

    installed = env.Command(
        str(destination / STAMP),
        stamp,
        Action(install, "Installing verified runtime assets"),
    )
    env.Precious(installed)  # Preserve the inventory used to remove obsolete assets.
    env.AlwaysBuild(installed)
    env.Alias("install", installed)
