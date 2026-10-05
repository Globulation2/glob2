#!/usr/bin/env python3
"""Export verified runtime assets without modifying source artwork."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import zlib
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRS = ("data", "maps", "campaigns", "scripts")
PILLOW_VERSION = "12.2.0"
ENCODING = json.loads((ROOT / "tools/image_encoding.json").read_text())
WEBP_VERSION = ENCODING["webpVersion"]
POLICY = "runtime-assets-v1"
IMAGE_RECIPE = ENCODING["recipe"]


def image_recipe(lossy=True):
    """Stable encoding identity, also used by browser derivative provenance."""
    return dict(image_recipe=IMAGE_RECIPE, pillow=PILLOW_VERSION, webp=WEBP_VERSION,
                lossless_quality=ENCODING["losslessQuality"], method=ENCODING["losslessMethod"], lossy=lossy,
                lossy_quality=ENCODING["quality"], lossy_method=ENCODING["method"], exact=ENCODING["exact"],
                conversion="16-bit-rgba-to-renderer-8-bit; reject-other-high-depth")


# Sprites whose frames optimized exports pack into sheets. Opening thousands of
# small files dominated loading them; GAGCore::Sprite::load reads <name>.sheet
# in place of <name><i>.webp and <name><i>r.webp when it exists.
SPRITE_SHEETS = ("data/gfx/unit",)
SHEET_COLUMNS = 16
SHEET_FRAMES = 256
SHEET_HEADER = """\
# Sprite sheet index, written by tools/package_assets.py, read by GAGCore::Sprite::load.
# One line per sheet: <file> <image|rotated> <first frame> <frames> <tile width> <tile height>
# Tiles are packed row-major; the column count follows from the sheet's width.
"""


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
                relative = p.relative_to(root)
                if relative.parts[:2] == ('data', 'zik') and p.suffix == '.ogg':
                    raise ValueError('Convert Vorbis music to .opus before packaging: ' + str(relative))
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


def encoder_probe_command(python):
    """Validate external interpreters against the same pins as encoder_ready."""
    return [
        str(python),
        "-c",
        "from PIL import Image, features; "
        f"assert Image.__version__ == {PILLOW_VERSION!r} "
        f"and features.version('webp') == {WEBP_VERSION!r}",
    ]


def encoder_python():
    """Use a private, pinned build-time environment, never modify system Python."""
    configured = os.environ.get("GLOB2_ASSET_ENCODER_PYTHON")
    if configured:
        subprocess.run(encoder_probe_command(configured), check=True)
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
    probe_command = encoder_probe_command(python)
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


def decoded_rgba(raw):
    """Decode the renderer reference, including SDL's rounded RGBA64 conversion.

    Pillow truncates 16-bit RGBA to high bytes. SDL_image retains RGBA64 and
    SDL_ConvertSurface rounds normalized channels to 8-bit instead. Decode PNG
    scanlines here to keep the exporter portable without native dependencies.
    """
    from PIL import Image
    image = Image.open(io.BytesIO(raw))
    if raw[:8] != b"\x89PNG\r\n\x1a\n" or raw[24:26] != bytes((16, 6)):
        return image.convert("RGBA")
    width, height = image.size
    chunks = []
    offset = 8
    while offset < len(raw):
        length = struct.unpack_from(">I", raw, offset)[0]
        if raw[offset + 4:offset + 8] == b"IDAT":
            chunks.append(raw[offset + 8:offset + 8 + length])
        offset += length + 12
    scanlines = zlib.decompress(b"".join(chunks))
    pixels = bytearray(width * height * 4)
    passes = ((0, 0, 1, 1),) if raw[28] == 0 else (
        (0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
        (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2))
    offset = 0
    for start_x, start_y, step_x, step_y in passes:
        columns = len(range(start_x, width, step_x))
        if not columns:
            continue
        previous = bytearray(columns * 8)
        for y in range(start_y, height, step_y):
            filter_type = scanlines[offset]
            offset += 1
            row = bytearray(scanlines[offset:offset + columns * 8])
            offset += columns * 8
            if len(row) != columns * 8 or filter_type > 4:
                raise ValueError("Invalid 16-bit RGBA PNG scanline")
            for i in range(len(row)):
                left = row[i - 8] if i >= 8 else 0
                above = previous[i]
                upper_left = previous[i - 8] if i >= 8 else 0
                if filter_type == 1:
                    prediction = left
                elif filter_type == 2:
                    prediction = above
                elif filter_type == 3:
                    prediction = (left + above) // 2
                elif filter_type == 4:
                    p = left + above - upper_left
                    distances = (abs(p - left), abs(p - above), abs(p - upper_left))
                    prediction = (left, above, upper_left)[distances.index(min(distances))]
                else:
                    prediction = 0
                row[i] = (row[i] + prediction) & 255
            for column, x in enumerate(range(start_x, width, step_x)):
                for channel in range(4):
                    i = column * 8 + channel * 2
                    value = row[i] * 256 + row[i + 1]
                    pixels[(y * width + x) * 4 + channel] = (value + 128) // 257
            previous = row
    if offset != len(scanlines):
        raise ValueError("Invalid 16-bit RGBA PNG scanline length")
    return Image.frombytes("RGBA", image.size, bytes(pixels))


def encode_image(source, relative, cache, lossy):
    """Cache the smallest allowed encoding after validating dimensions and pixels."""
    from PIL import Image, features

    raw = source.read_bytes()
    image = Image.open(io.BytesIO(raw))
    rgba = decoded_rgba(raw)
    expected = rgba.tobytes()
    recipe = image_recipe(lossy)
    recipe.update(policy=POLICY, zlib=features.version("zlib"))
    high_depth = raw[24] > 8
    # PNG IHDR color type 6 is the supported renderer RGBA64 conversion.
    convert_depth = high_depth and raw[25] == 6
    if high_depth and not convert_depth:
        raise ValueError("Unsupported higher-depth PNG mode for WebP export: " + relative)
    if high_depth:
        recipe["png_depth"] = raw[24]
        recipe["depth_action"] = "renderer-rgba8-round-nearest-div257"
    key = hashlib.sha256(raw + json.dumps(recipe, sort_keys=True).encode()).hexdigest()
    entry = cache / key
    metadata = entry / "selection.json"
    if metadata.is_file():
        try:
            record = json.loads(metadata.read_text())
            if record["suffix"] != ".webp" or record["recipe"] != recipe:
                raise ValueError("Invalid cache recipe")
            blob = entry / ("image" + record["suffix"])
            if (
                blob.is_file()
                and blob.read_bytes()[:4] == b"RIFF"
                and blob.read_bytes()[8:12] == b"WEBP"
                and hashlib.sha256(blob.read_bytes()).hexdigest() == record["sha256"]
            ):
                decoded = decoded_rgba(blob.read_bytes())
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
    # Both encodings use the same renderer-visible reference, including RGBA64.
    profile = {k: image.info[k] for k in ("icc_profile", "exif") if k in image.info}
    stream = io.BytesIO()
    rgba.save(stream, format="WEBP", lossless=True, quality=75, method=4,
              exact=True, **profile)
    candidates = [(stream.getvalue(), ".webp", False)]
    if recipe["lossy"] and (not high_depth or convert_depth):
        profile = {k: image.info[k] for k in ("icc_profile", "exif") if k in image.info}
        stream = io.BytesIO()
        rgba.save(
            stream,
            format="WEBP",
            lossless=False,
            quality=recipe["lossy_quality"],
            method=recipe["lossy_method"],
            exact=True,
            **{k: v for k, v in profile.items() if k != "dpi"},
        )
        candidates.append((stream.getvalue(), ".webp", True))
    # Validate every candidate, not just the eventual winner.
    for data, suffix, is_lossy in candidates:
        decoded = decoded_rgba(data)
        if (decoded.size != rgba.size or
                decoded.getchannel("A").tobytes() != rgba.getchannel("A").tobytes()):
            raise ValueError("Dimensions/alpha changed: " + relative)
        if not is_lossy and decoded.tobytes() != expected:
            raise ValueError("Lossless pixel mismatch: " + relative)
    data, suffix, is_lossy = min(candidates, key=lambda item: len(item[0]))
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


def sheet_plan(files, root):
    """{sprite: [sheet]} for the SPRITE_SHEETS frames among files.

    Each sheet holds a run of consecutive frames of one layer and one size, at
    most SHEET_FRAMES of them, so the loader can cut equally sized tiles."""
    from PIL import Image

    plans = {}
    for sprite in SPRITE_SHEETS:
        directory, name = sprite.rsplit("/", 1)
        pattern = re.compile(re.escape(name) + r"(\d+)(r?)\.png")
        frames = {}
        for source in files:
            relative = source.relative_to(root)
            match = pattern.fullmatch(relative.name)
            if relative.parent.as_posix() == directory and match:
                frames[(int(match.group(1)), match.group(2) == "r")] = source
        if not frames:
            continue
        count = 1 + max(index for index, _ in frames)
        # Sprite::load stops at the first frame with neither layer; a sheet
        # would make frames behind such a gap appear.
        if any((i, False) not in frames and (i, True) not in frames for i in range(count)):
            raise ValueError("Sprite frames are not consecutive: " + sprite)
        sheets = []
        for rotated in (False, True):
            run = None
            for index in range(count):
                source = frames.get((index, rotated))
                if source is None:
                    run = None
                    continue
                raw = source.read_bytes()
                if raw[24] > 8:
                    raise ValueError("Sprite sheets are 8-bit: " + str(source))
                size = Image.open(io.BytesIO(raw)).size
                if run is None or run["size"] != size or len(run["frames"]) == SHEET_FRAMES:
                    run = dict(rotated=rotated, first=index, size=size, frames=[])
                    sheets.append(run)
                run["frames"].append(source)
        for number, sheet in enumerate(sheets):
            sheet["name"] = "%s-sheet-%d.png" % (name, number)
        plans[sprite] = sheets
    return plans


def pack_sheet(sheet, root, cache):
    """Return a cached PNG of the sheet, every tile verified against its frame."""
    from PIL import Image

    width, height = sheet["size"]
    layout = dict(
        policy=POLICY,
        pillow=Image.__version__,
        columns=SHEET_COLUMNS,
        frames=[
            [f.relative_to(root).as_posix(), hashlib.sha256(f.read_bytes()).hexdigest()]
            for f in sheet["frames"]
        ],
    )
    key = hashlib.sha256(json.dumps(layout, sort_keys=True).encode()).hexdigest()
    packed = cache / ("sheet-" + key) / sheet["name"]
    if packed.is_file():
        try:
            verify_sheet(sheet, packed, exact=True)
            return packed
        except (OSError, ValueError):
            pass
    rows = -(-len(sheet["frames"]) // SHEET_COLUMNS)
    image = Image.new("RGBA", (SHEET_COLUMNS * width, rows * height), (0, 0, 0, 0))
    for index, source in enumerate(sheet["frames"]):
        frame = Image.open(source).convert("RGBA")
        box = ((index % SHEET_COLUMNS) * width, (index // SHEET_COLUMNS) * height)
        # Pasting without a mask replaces alpha too, keeping the frame's exact pixels.
        image.paste(frame, box)
        if image.crop(box + (box[0] + width, box[1] + height)).tobytes() != frame.tobytes():
            raise ValueError("Sheet tile differs from its frame: " + str(source))
    packed.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=packed.parent, delete=False) as f:
        image.save(f, format="PNG")
        temp = Path(f.name)
    os.replace(temp, packed)
    return packed


def verify_sheet(sheet, path, exact=False):
    """Verify geometry and each frame's alpha against its source after decoding."""
    from PIL import Image
    width, height = sheet["size"]
    with Image.open(path) as decoded:
        image = decoded.convert("RGBA")
    if image.size != (SHEET_COLUMNS * width, -(-len(sheet["frames"]) // SHEET_COLUMNS) * height):
        raise ValueError("Sheet dimensions changed")
    for index, source in enumerate(sheet["frames"]):
        with Image.open(source) as decoded:
            frame = decoded.convert("RGBA")
        x, y = (index % SHEET_COLUMNS) * width, (index // SHEET_COLUMNS) * height
        tile = image.crop((x, y, x + width, y + height))
        if tile.getchannel("A").tobytes() != frame.getchannel("A").tobytes() or (
                exact and tile.tobytes() != frame.tobytes()):
            raise ValueError("Sheet tile differs from its frame: " + str(source))


def sheet_index(sheets):
    lines = [
        "%s %s %d %d %d %d"
        % (
            str(Path(sheet["name"]).with_suffix(".webp")),
            "rotated" if sheet["rotated"] else "image",
            sheet["first"],
            len(sheet["frames"]),
            sheet["size"][0],
            sheet["size"][1],
        )
        for sheet in sheets
    ]
    return (SHEET_HEADER + "\n".join(lines) + "\n").encode()


def verify_highres_atlases(root):
    """Check source atlas placement before lossy encodings can differ in RGB."""
    directory = root / "data/highres/v1"
    index = directory / "frames.txt"
    if not index.is_file():
        return
    entries = {}
    for line in index.read_text().splitlines()[1:]:
        row = line.split()
        if len(row) == 6:
            entries[row[0]] = row[4]
    for prefix, columns, border in (("terrain", 16, 64), ("ressource", 8, 32)):
        path = directory / (prefix + "-atlas-mip0.png")
        if not path.is_file():
            continue
        frames = []
        while prefix + str(len(frames)) in entries:
            frames.append(directory / entries[prefix + str(len(frames))])
        if not frames:
            raise ValueError("Atlas has no indexed frames: " + str(path))
        if prefix == "terrain" and len(frames) == 16:
            columns = 4
        atlas = decoded_rgba(path.read_bytes())
        for i, frame_path in enumerate(frames):
            frame = decoded_rgba(frame_path.read_bytes())
            x, y = (i % columns) * 256 + border, (i // columns) * 256 + border
            tile = atlas.crop((x, y, x + frame.width, y + frame.height))
            if tile.tobytes() != frame.tobytes():
                raise ValueError("Source atlas frame placement differs: " + str(frame_path))


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
        command.append("--lossy-images" if lossy else "--lossless-images")
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
    files = list(source_files(root, platform))
    if optimized:
        verify_highres_atlases(root)
    sheets = sheet_plan(files, root) if optimized else {}
    packed = {f for plan in sheets.values() for sheet in plan for f in sheet["frames"]}
    with tempfile.TemporaryDirectory(
        prefix=output.name + "-staging-", dir=output.parent
    ) as temporary:
        stage = Path(temporary)
        for source in files:
            if source in packed:
                continue
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
            if optimized and relative.as_posix() == "data/highres/v1/frames.txt":
                # Source indices continue to describe source PNGs; runtime indices
                # name only the generated artwork. Frame IDs and geometry stay intact.
                target.write_bytes(re.sub(rb"\.png(?=\s|$)", b".webp", blob.read_bytes()))
            else:
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
        for sprite, plan in sheets.items():
            for sheet in plan:
                relative = Path(sprite).parent / sheet["name"]
                source = pack_sheet(sheet, root, cache)
                blob, record = encode_image(source, relative.as_posix(), cache, lossy)
                verify_sheet(sheet, blob, exact=not record["lossy"])
                destination = relative.with_suffix(record["suffix"])
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
                        source_bytes=sum(f.stat().st_size for f in sheet["frames"]),
                        output_bytes=target.stat().st_size,
                        lossy=record["lossy"],
                        recipe=record.get("recipe"),
                        packed_from=[
                            dict(
                                source=f.relative_to(root).as_posix(),
                                sha256=hashlib.sha256(f.read_bytes()).hexdigest(),
                            )
                            for f in sheet["frames"]
                        ],
                    )
                )
            index = Path(sprite + ".sheet")
            if index in destinations:
                raise ValueError("Duplicate exported path: " + str(index))
            destinations.add(index)
            data = sheet_index(plan)
            (stage / index).write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            records.append(
                dict(
                    source=index.as_posix(),
                    output=index.as_posix(),
                    source_sha256=digest,
                    output_sha256=digest,
                    source_bytes=0,
                    output_bytes=len(data),
                    lossy=False,
                    recipe=None,
                )
            )
        audit = dict(
            policy=POLICY,
            platform=platform,
            optimized=optimized,
            lossy_images=lossy,
            image_recipe=image_recipe(lossy) if optimized else None,
            files=records,
            source_bytes=sum(x["source_bytes"] for x in records),
            output_bytes=sum(x["output_bytes"] for x in records),
        )
        # Only replace output directories previously owned by this exporter.
        marker = output.with_suffix(".json")
        if output.exists():
            try:
                previous_audit = json.loads(marker.read_text())
                owned = previous_audit.get("policy") == POLICY
            except (OSError, ValueError, AttributeError):
                owned = False
            if not owned:
                raise ValueError(
                    "Refusing to replace an unowned asset directory: " + str(output)
                )
        if output.exists():
            managed = {entry["output"]: entry["output_sha256"] for entry in previous_audit["files"]}
            for old in output.rglob("*"):
                if not old.is_file():
                    continue
                relative = old.relative_to(output)
                target = stage / relative
                # Current shipped files are replaced; only obsolete, unchanged
                # managed files are removed. Preserve unrelated or edited content.
                if target.exists():
                    continue
                if managed.get(relative.as_posix()) == hashlib.sha256(old.read_bytes()).hexdigest():
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(old, target)
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
    """Export a complete runtime tree and return its source/output audit.

    Source artwork stays untouched. The adjacent JSON audit owns the generated
    output directory and is excluded from shipped resources. An encoder worker
    acquires the output lease itself, avoiding a recursive lock in its parent.
    """
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
    parser.add_argument("--original", action="store_true",
                        help="Export original source bytes for offline comparisons (not a playable artwork tree)")
    images = parser.add_mutually_exclusive_group()
    images.add_argument("--lossy-images", "--lossy-background", dest="lossy_images",
                        action="store_true", default=True,
                        help="Offer Q90 WebP for all images (default); --lossy-background is a deprecated alias")
    images.add_argument("--lossless-images", "--lossless-background", dest="lossy_images",
                        action="store_false",
                        help="Require exact RGBA; --lossless-background is a deprecated alias")
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
        args.lossy_images,
        args.cache,
        args.worker,
    )
    print(
        "Runtime assets: %d -> %d bytes (%d files)"
        % (result["source_bytes"], result["output_bytes"], len(result["files"]))
    )


if __name__ == "__main__":
    main()
