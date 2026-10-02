#!/usr/bin/env python3
"""Export verified runtime assets without modifying source artwork."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRS = ("data", "maps", "campaigns", "scripts")
PILLOW_VERSION = "12.2.0"
WEBP_VERSION = "1.6.0"
POLICY = "runtime-assets-v1"
# Only this illustration may use lossy RGB. Wordmarks and game sprites stay exact.
LOSSY_BACKGROUND = "data/gfx/menu-colony.png"


def include_asset(relative, platform="generic"):
    p = Path(relative)
    if any(x.startswith(".") or x in ("CVS", "__pycache__") for x in p.parts):
        return False
    if (
        p.name == "SConscript"
        or p.suffix in (".py", ".pyc", ".pyo", ".sh", ".perl", ".scc")
        or p.name.endswith("~")
    ):
        return False
    if p.as_posix() in ("data/highres/v1/manifest.json", "data/highres/v1/README.md"):
        return False
    if p.parts[:2] == ("data", "screenshots") and platform != "linux":
        return False
    return True


def source_files(root, platform):
    for directory in ASSET_DIRS:
        for p in sorted((root / directory).rglob("*")):
            if p.is_symlink():
                raise ValueError("Asset symlinks are not supported: " + str(p))
            if p.is_file() and include_asset(p.relative_to(root), platform):
                yield p


def encoder_ready():
    try:
        from PIL import Image, features

        return (
            Image.__version__ == PILLOW_VERSION
            and features.version("webp") == WEBP_VERSION
        )
    except ImportError:
        return False


def encoder_python():
    """Use a private, pinned build-time environment, never modify system Python."""
    configured = os.environ.get("GLOB2_ASSET_ENCODER_PYTHON")
    if configured:
        subprocess.run(
            [
                configured,
                "-c",
                'from PIL import Image,features; assert Image.__version__=="12.2.0" and features.version("webp")=="1.6.0"',
            ],
            check=True,
        )
        return configured
    if encoder_ready():
        return sys.executable
    sys.path.insert(0, str(ROOT / "scons"))
    from dev_store import cache, Lease

    name = (
        "asset-encoder-"
        + POLICY
        + "-py"
        + str(sys.version_info.major)
        + "."
        + str(sys.version_info.minor)
    )
    location = cache(ROOT, name, lease=False)
    python = (
        location / "venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    )
    probe_command = [
        str(python),
        "-c",
        'from PIL import Image,features; assert Image.__version__=="12.2.0" and features.version("webp")=="1.6.0"',
    ]
    with Lease(location):
        ready = (
            python.exists()
            and subprocess.run(probe_command, capture_output=True).returncode == 0
        )
    if ready:
        from dev_store import hold

        hold(location)
        return str(python)
    with Lease(location, exclusive=True):
        if not python.exists():
            subprocess.run(
                [sys.executable, "-m", "venv", str(location / "venv")], check=True
            )
        probe = subprocess.run(probe_command, capture_output=True)
        if probe.returncode:
            subprocess.run(
                [
                    str(python),
                    "-m",
                    "pip",
                    "install",
                    "--only-binary=:all:",
                    "-r",
                    str(ROOT / "tools/asset-requirements.txt"),
                ],
                check=True,
                stdout=sys.stderr,
            )
        subprocess.run(probe_command, check=True)
    from dev_store import hold

    hold(location)
    return str(python)


def encode_image(source, relative, cache, lossy):
    from PIL import Image, features

    raw = source.read_bytes()
    image = Image.open(io.BytesIO(raw))
    rgba = image.convert("RGBA")
    expected = rgba.tobytes()
    recipe = dict(
        policy=POLICY,
        pillow=Image.__version__,
        webp=features.version("webp"),
        zlib=features.version("zlib"),
        lossless_quality=75,
        method=4,
        lossy=lossy and relative == LOSSY_BACKGROUND,
    )
    if recipe["lossy"]:
        recipe.update(lossy_quality=85, lossy_method=6)
    if raw[24] > 8:
        recipe["preserve_png_depth"] = raw[24]
    key = hashlib.sha256(raw + json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    entry = cache / key
    metadata = entry / "selection.json"
    if metadata.is_file():
        try:
            record = json.loads(metadata.read_text())
            if record["suffix"] not in (".png", ".webp") or record["recipe"] != recipe:
                raise ValueError("Invalid cache recipe")
            blob = entry / ("image" + record["suffix"])
            if (
                blob.is_file()
                and hashlib.sha256(blob.read_bytes()).hexdigest() == record["sha256"]
            ):
                decoded = Image.open(blob).convert("RGBA")
                exact_alpha = (
                    decoded.getchannel("A").tobytes() == rgba.getchannel("A").tobytes()
                )
                allowed = record["lossy"] is False or (
                    record["lossy"] is True and recipe["lossy"]
                )
                if (
                    allowed
                    and exact_alpha
                    and decoded.size == rgba.size
                    and (record["lossy"] or decoded.tobytes() == expected)
                ):
                    return blob, record
        except (OSError, ValueError, KeyError, TypeError):
            pass  # A damaged cache is regenerable from the source artwork.
    candidates = [(raw, ".png", False)]
    # WebP is 8-bit. Preserve higher-depth PNGs verbatim rather than silently
    # quantizing artwork through Pillow's RGBA conversion.
    if raw[24] <= 8:
        profile = {
            k: image.info[k] for k in ("icc_profile", "exif", "dpi") if k in image.info
        }
        stream = io.BytesIO()
        image.save(stream, format="PNG", optimize=True, compress_level=9, **profile)
        candidates.append((stream.getvalue(), ".png", False))
        stream = io.BytesIO()
        rgba.save(
            stream,
            format="WEBP",
            lossless=True,
            quality=75,
            method=4,
            exact=True,
            **{k: v for k, v in profile.items() if k != "dpi"},
        )
        candidates.append((stream.getvalue(), ".webp", False))
        if recipe["lossy"]:
            if rgba.getchannel("A").getextrema() != (255, 255):
                raise ValueError("Lossy background must be opaque")
            stream = io.BytesIO()
            rgba.save(
                stream,
                format="WEBP",
                quality=recipe["lossy_quality"],
                method=recipe["lossy_method"],
                exact=True,
                **{k: v for k, v in profile.items() if k != "dpi"},
            )
            candidates.append((stream.getvalue(), ".webp", True))
    data, suffix, is_lossy = min(candidates, key=lambda item: len(item[0]))
    decoded = Image.open(io.BytesIO(data)).convert("RGBA")
    if (
        decoded.size != rgba.size
        or decoded.getchannel("A").tobytes() != rgba.getchannel("A").tobytes()
    ):
        raise ValueError("Dimensions/alpha changed: " + relative)
    if not is_lossy and decoded.tobytes() != expected:
        raise ValueError("Lossless pixel mismatch: " + relative)
    record = dict(
        suffix=suffix,
        sha256=hashlib.sha256(data).hexdigest(),
        lossy=is_lossy,
        recipe=recipe,
    )
    entry.mkdir(parents=True, exist_ok=True)
    # Unique temporary files also permit independent exporters sharing this cache.
    with tempfile.NamedTemporaryFile(dir=entry, delete=False) as f:
        f.write(data)
        temp = Path(f.name)
    blob = entry / ("image" + suffix)
    os.replace(temp, blob)
    with tempfile.NamedTemporaryFile(dir=entry, mode="w", delete=False) as f:
        json.dump(record, f, sort_keys=True)
        temp = Path(f.name)
    os.replace(temp, metadata)
    return blob, record


def _export_assets(
    root,
    output,
    platform="generic",
    optimized=True,
    lossy=True,
    cache=None,
    worker=False,
):
    root, output = Path(root).resolve(), Path(output).resolve()
    if root.is_relative_to(output) or any(
        output == root / d or output.is_relative_to(root / d) for d in ASSET_DIRS
    ):
        raise ValueError("Export output must not overlap source assets")
    if platform not in (
        "generic",
        "macos",
        "windows",
        "linux",
        "android",
        "ios",
        "web",
    ):
        raise ValueError("Unknown asset platform: " + platform)
    if (
        optimized
        and not encoder_ready()
        and any(p.suffix.lower() == ".png" for p in source_files(root, platform))
    ):
        if worker:
            raise RuntimeError("Packaging requires Pillow 12.2.0 with libwebp 1.6.0")
        command = [
            encoder_python(),
            str(Path(__file__).resolve()),
            "--source",
            str(root),
            "--output",
            str(output),
            "--platform",
            platform,
            "--worker",
        ]
        command.append("--lossy-background" if lossy else "--lossless-background")
        if cache:
            command += ["--cache", str(cache)]
        subprocess.run(command, check=True)
        return json.loads(output.with_suffix(".json").read_text())
    if cache is None:
        cache = root / "build/asset-cache"
    cache = Path(cache).resolve()
    if (
        cache == output
        or cache.is_relative_to(output)
        or any(cache == root / d or cache.is_relative_to(root / d) for d in ASSET_DIRS)
    ):
        raise ValueError("Cache must be outside exported and source assets")
    output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    destinations = set()
    with tempfile.TemporaryDirectory(
        prefix=output.name + "-staging-", dir=output.parent
    ) as temporary:
        stage = Path(temporary)
        for source in source_files(root, platform):
            relative = source.relative_to(root)
            blob = source
            record = dict(lossy=False)
            # AppStream screenshot references are public PNG URLs, not image-loader
            # logical names. Keep their original representation on Linux.
            public_png = platform == "linux" and relative.parts[:2] == (
                "data",
                "screenshots",
            )
            if optimized and source.suffix.lower() == ".png" and not public_png:
                blob, record = encode_image(source, relative.as_posix(), cache, lossy)
                destination = relative.with_suffix(record["suffix"])
            else:
                destination = relative
            if destination in destinations:
                raise ValueError("Duplicate exported path: " + str(destination))
            destinations.add(destination)
            target = stage / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(blob, target)
            records.append(
                dict(
                    source=relative.as_posix(),
                    output=destination.as_posix(),
                    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                    output_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                    source_bytes=source.stat().st_size,
                    output_bytes=target.stat().st_size,
                    lossy=record["lossy"],
                    recipe=record.get("recipe"),
                )
            )
        audit = dict(
            policy=POLICY,
            platform=platform,
            optimized=optimized,
            lossy_background=lossy,
            files=records,
            source_bytes=sum(x["source_bytes"] for x in records),
            output_bytes=sum(x["output_bytes"] for x in records),
        )
        # Only replace output directories previously owned by this exporter.
        marker = output.with_suffix(".json")
        if output.exists():
            try:
                owned = json.loads(marker.read_text()).get("policy") == POLICY
            except (OSError, ValueError, AttributeError):
                owned = False
            if not owned:
                raise ValueError(
                    "Refusing to replace an unowned asset directory: " + str(output)
                )
        backup = output.with_name(output.name + "-previous")
        if backup.exists():
            raise ValueError("Unresolved previous asset export: " + str(backup))
        # Write the audit before replacing the directory so a failed write
        # cannot leave a new export without its ownership record.
        with tempfile.NamedTemporaryFile(
            dir=marker.parent, mode="w", delete=False
        ) as f:
            temp = Path(f.name)
            try:
                json.dump(audit, f, indent=2, sort_keys=True)
                f.write("\n")
            except BaseException:
                temp.unlink(missing_ok=True)
                raise
        try:
            if output.exists():
                output.rename(backup)
            try:
                stage.rename(output)
                os.replace(temp, marker)
            except BaseException:
                if output.exists():
                    shutil.rmtree(output)
                if backup.exists():
                    backup.rename(output)
                raise
        finally:
            temp.unlink(missing_ok=True)
        if backup.exists():
            shutil.rmtree(backup)
    return audit


def export_assets(
    root,
    output,
    platform="generic",
    optimized=True,
    lossy=True,
    cache=None,
    worker=False,
):
    if optimized and not encoder_ready() and not worker:
        return _export_assets(root, output, platform, optimized, lossy, cache, worker)
    sys.path.insert(0, str(ROOT / "scons"))
    from dev_store import Lease

    with Lease(Path(output).resolve(), exclusive=True, track_use=False):
        return _export_assets(root, output, platform, optimized, lossy, cache, worker)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--encoder-python",
        action="store_true",
        help="Print the private pinned encoder interpreter",
    )
    parser.add_argument("--platform", default="generic")
    parser.add_argument("--original", action="store_true")
    background = parser.add_mutually_exclusive_group()
    background.add_argument(
        "--lossy-background", dest="lossy_background", action="store_true", default=True
    )
    background.add_argument(
        "--lossless-background", dest="lossy_background", action="store_false"
    )
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.encoder_python:
        print(encoder_python())
        return
    if args.output is None:
        parser.error("--output is required")
    result = export_assets(
        args.source,
        args.output,
        args.platform,
        not args.original,
        args.lossy_background,
        args.cache,
        args.worker,
    )
    print(
        "Runtime assets: %d -> %d bytes (%d files)"
        % (result["source_bytes"], result["output_bytes"], len(result["files"]))
    )


if __name__ == "__main__":
    main()
