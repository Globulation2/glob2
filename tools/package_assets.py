#!/usr/bin/env python3
"""Export verified runtime assets without modifying source artwork."""

import argparse
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
import hashlib
import io
import json
import multiprocessing
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
# The runtime tree lives under content/ in a checkout; packages hold its
# directories at their root, so exported paths are relative to content/.
CONTENT = "content"
ASSET_DIRS = ("data", "maps", "campaigns", "scripts")
PILLOW_VERSION = "12.2.0"
BROTLI_VERSION = "1.2.0"
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


# Images whose texels are data the renderer decodes rather than colour it shows:
# terrain stone maps pack stone ids and joint distances into RGBA8, so any lossy
# change moves stone borders and opens holes in laid paths.
DATA_IMAGE_DIRECTORIES = (Path("data/terrain/stones"),)


def is_data_image(relative):
    """Identify images that must survive export bit-exact."""
    return Path(relative).parent in DATA_IMAGE_DIRECTORIES


def is_baked_unit_clip(relative):
    """Identify the authoring and comparison meshes superseded by fitted clips."""
    p = Path(relative)
    return (
        p.parent == Path("data/skins/colony-v1")
        and p.suffix == ".gsk"
        and p.name.startswith(("worker-", "warrior-", "explorer-"))
    )


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
    if p.as_posix() in ("data/highres/v1/manifest.json", "data/highres/v1/README.md",
                        "data/fonts/script-probes.json"):
        return False
    if is_baked_unit_clip(p):
        return False
    if p.parts[:2] == ("data", "screenshots") and platform != "linux":
        return False
    return True


def catalog_sha256(terrain_catalog, content):
    """One digest over the catalog index and every material file it lists."""
    digest = hashlib.sha256()
    for path in terrain_catalog.sources(content):
        digest.update(path.relative_to(content).as_posix().encode() + b"\0" + path.read_bytes())
    return digest.hexdigest()


def source_files(root, platform):
    content = Path(root) / CONTENT
    for directory in ASSET_DIRS:
        for p in sorted((content / directory).rglob("*")):
            if p.is_symlink():
                raise ValueError("Asset symlinks are not supported: " + str(p))
            if p.is_file() and include_asset(p.relative_to(content), platform):
                relative = p.relative_to(content)
                if relative.parts[:2] == ('data', 'zik') and p.suffix == '.ogg':
                    raise ValueError('Convert Vorbis music to .opus before packaging: ' + str(relative))
                yield p


def encoder_ready(require_brotli=False):
    try:
        from PIL import Image, features

        return (
            Image.__version__ == PILLOW_VERSION
            and features.version("webp") == WEBP_VERSION
            and (not require_brotli or __import__("brotli").__version__ == BROTLI_VERSION)
        )
    except ImportError:
        return False


def encoder_probe_command(python, require_brotli=False):
    """Validate external interpreters against the same pins as encoder_ready."""
    return [
        str(python),
        "-c",
        "from PIL import Image, features; "
        f"assert Image.__version__ == {PILLOW_VERSION!r} "
        f"and features.version('webp') == {WEBP_VERSION!r}" +
        (f"; import brotli; assert brotli.__version__ == {BROTLI_VERSION!r}" if require_brotli else ""),
    ]


def encoder_python(require_brotli=False):
    """Use a private, pinned build-time environment, never modify system Python."""
    configured = os.environ.get("GLOB2_ASSET_ENCODER_PYTHON")
    if configured:
        subprocess.run(encoder_probe_command(configured), check=True)
        if not require_brotli or subprocess.run(encoder_probe_command(configured, True), capture_output=True).returncode == 0:
            return configured
    if encoder_ready(require_brotli):
        return sys.executable
    sys.path.insert(0, str(ROOT / "scons"))
    from dev_store import cache, Lease

    name = (
        "asset-encoder-"
        + POLICY
        + ("-brotli" if require_brotli else "")
        + "-py"
        + str(sys.version_info.major)
        + "."
        + str(sys.version_info.minor)
    )
    location = cache(ROOT, name, lease=False)
    python = (
        location / "venv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    )
    probe_command = encoder_probe_command(python, require_brotli)
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


def publish_cache_file(temporary, destination):
    """Publish an immutable cache file, tolerating an identical racing writer."""
    try:
        try:
            os.replace(temporary, destination)
        except PermissionError:
            # Windows readers can deny replacement after another exporter has
            # already published this content-addressed entry. Accept only the
            # exact bytes we were about to publish; real conflicts still fail.
            try:
                identical = destination.read_bytes() == temporary.read_bytes()
            except OSError:
                identical = False
            if not identical:
                raise
    finally:
        temporary.unlink(missing_ok=True)


def encode_image(source, relative, cache, lossy, *, sheet_candidate=None):
    """Cache the smallest allowed encoding after validating dimensions and pixels."""
    from PIL import Image, features

    raw = source.read_bytes()
    image = Image.open(io.BytesIO(raw))
    rgba = decoded_rgba(raw)
    expected = rgba.tobytes()
    recipe = image_recipe(lossy)
    recipe.update(policy=POLICY, zlib=features.version("zlib"))
    if sheet_candidate is not None:
        recipe.update(sheet_candidate=sheet_candidate, lossy_quality=75)
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
    candidates = []
    if sheet_candidate != "q75":
        stream = io.BytesIO()
        rgba.save(stream, format="WEBP", lossless=True, quality=75, method=4,
                  exact=True, **profile)
        candidates.append((stream.getvalue(), ".webp", False))
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
    if sheet_candidate is not None:
        from PIL import ImageChops

        if is_lossy:
            visible = rgba.getchannel("A").point(lambda value: 255 if value else 0)
            diff = ImageChops.difference(rgba.convert("RGB"), decoded_rgba(data).convert("RGB"))
            record["visible_rgb_sse"] = sum((index % 256) ** 2 * count
                                            for index, count in enumerate(diff.histogram(mask=visible)))
        else:
            record["visible_rgb_sse"] = 0
    entry.mkdir(parents=True, exist_ok=True)
    # Unique temporary files also permit independent exporters sharing this cache.
    with tempfile.NamedTemporaryFile(dir=entry, delete=False) as f:
        f.write(data)
        temp = Path(f.name)
    blob = entry / ("image" + suffix)
    publish_cache_file(temp, blob)
    with tempfile.NamedTemporaryFile(dir=entry, mode="w", delete=False) as f:
        json.dump(record, f, sort_keys=True)
        temp = Path(f.name)
    publish_cache_file(temp, metadata)
    return blob, record


def sheet_plan(files, root, terrain_sprites=()):
    """{sprite: [sheet]} for the SPRITE_SHEETS frames among files.

    Each sheet holds a run of consecutive frames of one layer and one size, at
    most SHEET_FRAMES of them, so the loader can cut equally sized tiles."""
    from PIL import Image

    plans = {}
    families = {sprite: {} for sprite in (*SPRITE_SHEETS, *sorted(set(terrain_sprites)))}
    for source in files:
        relative = source.relative_to(root).as_posix()
        match = re.fullmatch(r"(.+?)(\d+)(r?)\.png", relative)
        if not match:
            continue
        prefix, digits, layer = match.groups()
        # A declared family can itself end in a digit. Prefer its longest
        # matching prefix while leaving at least one digit for the frame.
        for split in range(len(digits) - 1, -1, -1):
            frames = families.get(prefix + digits[:split])
            if frames is None:
                continue
            key = int(digits[split:]), layer == "r"
            if key in frames:
                raise ValueError("Duplicate sprite frame: " + relative)
            frames[key] = source
            break
    for sprite, frames in families.items():
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
            sheet["name"] = f"{Path(sprite).name}-sheet-{number}.png"
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
    for prefix, border in (("terrain", 64), ("ressource", 32)):
        path = directory / (prefix + "-atlas-mip0.png")
        if not path.is_file():
            continue
        frames = []
        while prefix + str(len(frames)) in entries:
            frames.append(directory / entries[prefix + str(len(frames))])
        if not frames:
            raise ValueError("Atlas has no indexed frames: " + str(path))
        atlas = decoded_rgba(path.read_bytes())
        columns = atlas.width // 256
        if not columns or atlas.width % 256 or atlas.height % 256 or len(frames) > columns * (atlas.height // 256):
            raise ValueError("Invalid legacy atlas dimensions: " + str(path))
        for i, frame_path in enumerate(frames):
            frame = decoded_rgba(frame_path.read_bytes())
            x, y = (i % columns) * 256 + border, (i // columns) * 256 + border
            tile = atlas.crop((x, y, x + frame.width, y + frame.height))
            if tile.tobytes() != frame.tobytes():
                raise ValueError("Source atlas frame placement differs: " + str(frame_path))


def terrain_runtime_fingerprints(compiler, sources, root, stage, sheets, packed_fingerprints=None):
    """Attest to the pixels Sprite will load, including encoded sheet frames.

    Original PNG hashes remain provenance; decoded runtime fingerprints distinguish
    valid lossy exports from stale packs and user artwork overrides at startup.
    """
    from PIL import Image

    fingerprints = dict(packed_fingerprints or {})
    for sprite, plan in sheets.items():
        for sheet in plan:
            relevant = [
                (i, frame.relative_to(root).as_posix())
                for i, frame in enumerate(sheet["frames"])
                if frame.relative_to(root).as_posix() in sources
            ]
            if not relevant:
                continue
            path = stage / Path(sprite).parent / Path(sheet["name"]).with_suffix(".webp")
            with Image.open(path) as decoded:
                image = decoded.convert("RGBA")
            width, height = sheet["size"]
            for index, name in relevant:
                x, y = index % SHEET_COLUMNS * width, index // SHEET_COLUMNS * height
                fingerprints[name] = compiler.pixel_fingerprint(
                    image.crop((x, y, x + width, y + height))
                )
    for name in sources:
        if name not in fingerprints:
            path = stage / name
            encoded = path.with_suffix(".webp")
            with Image.open(encoded if encoded.exists() else path) as image:
                fingerprints[name] = compiler.pixel_fingerprint(image)
    return fingerprints


def compiled_terrain_pack(compiler, document, root, cache, source_hashes, fingerprints):
    """Cache the derived WebP pages so a warm release export performs no encodes."""
    key = hashlib.sha256(json.dumps([
        POLICY, image_recipe(False), hashlib.sha256(Path(compiler.__file__).read_bytes()).hexdigest(),
        document, source_hashes, fingerprints, compiler.material_provenance(document, root),
    ], sort_keys=True).encode()).hexdigest()
    entry = cache / ("compiled-terrain-" + key)
    marker = entry / "checksums.json"
    from dev_store import Lease
    with Lease(entry, exclusive=True, track_use=False):
        try:
            hashes = json.loads(marker.read_text())
            if hashes and all(Path(name).name == name and
                              hashlib.sha256((entry / name).read_bytes()).hexdigest() == digest
                              for name, digest in hashes.items()):
                return [entry / name for name in sorted(hashes)]
        except (OSError, ValueError):
            pass
        entry.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="terrain-pack-", dir=cache) as temporary:
            pack = Path(temporary)
            compiler.compile_tileset(document, pack, root=root, runtime_fingerprints=fingerprints)
            hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in pack.iterdir()}
            for name in hashes:
                os.replace(pack / name, entry / name)
            pending = pack / marker.name
            pending.write_text(json.dumps(hashes, sort_keys=True))
            os.replace(pending, marker)
        return [entry / name for name in sorted(hashes)]


def packaging_jobs(jobs=None):
    """Use the CPUs available to this process unless a caller supplies a budget."""
    if jobs is None:
        if hasattr(os, "sched_getaffinity"):
            jobs = len(os.sched_getaffinity(0))
        else:
            jobs = os.cpu_count() or 1
    if not isinstance(jobs, int) or jobs < 1:
        raise ValueError("Asset packaging jobs must be a positive integer")
    return jobs


def encoding_executor(jobs):
    # Pillow's WebP encoder and our RGBA64 reference decoder hold the GIL.
    # Spawn avoids inheriting SCons threads/leases and works on every host OS.
    if jobs == 1:
        return ThreadPoolExecutor(max_workers=1)
    # Spawned workers start from the parent's current sys.path and unpickle
    # work by module name (tools.package_assets.*). SCons restores sys.path
    # after reading each SConscript, dropping the repository root that
    # scons/runtime_assets.py added, so put it back before any worker starts.
    if __name__ != "__main__" and str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    # Each worker holds one machine-wide CPU token for its lifetime (scons/jobserver.py).
    sys.path.insert(0, str(ROOT / "scons"))
    import jobserver
    return ProcessPoolExecutor(max_workers=jobs, mp_context=multiprocessing.get_context("spawn"),
                               initializer=jobserver.hold_for_process_lifetime)


def encode_sheet(sheet, root, cache, lossy, relative):
    source = pack_sheet(sheet, root, cache)
    blob, record = encode_image(source, relative.as_posix(), cache, lossy)
    verify_sheet(sheet, blob, exact=not record["lossy"])
    return source, blob, record


def encode_terrain_sheet_group(sheets, root, cache, lossy):
    """Choose a single WebP mode for the whole material family."""
    candidates = {}
    sources = [pack_sheet(sheet, root, cache) for sheet in sheets]
    for mode in (("lossless", "q75") if lossy else ("lossless",)):
        pages = []
        for sheet, source in zip(sheets, sources):
            blob, record = encode_image(source, sheet["name"], cache, mode == "q75",
                                        sheet_candidate=mode)
            verify_sheet(sheet, blob, exact=not record["lossy"])
            pages.append((source, blob, record))
        candidates[mode] = pages
    scores = {mode: dict(stored_bytes=sum(blob.stat().st_size for _, blob, _ in pages)
                         + len(sheet_index(sheets)),
                         visible_rgb_sse=sum(record["visible_rgb_sse"] for _, _, record in pages))
              for mode, pages in candidates.items()}
    winner = min(scores, key=lambda mode: scores[mode]["stored_bytes"])
    return candidates[winner], dict(winner=winner, candidates=scores)


def _export_assets(
    root,
    output,
    platform="generic",
    optimized=True,
    lossy=True,
    cache=None,
    worker=False,
    jobs=None,
    *,
    terrain_sheets=False,
    packed_hd_sprites=False,
    sprite_ffmpeg=None,
    video_sprites=False,
):
    root, output = Path(root).resolve(), Path(output).resolve()
    content = root / CONTENT
    catalog = content / "data/terrain/tileset.json"

    if root.is_relative_to(output) or any(
        output == content / d or output.is_relative_to(content / d) for d in ASSET_DIRS
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
    needs_brotli = platform == "web" and optimized and (packed_hd_sprites or video_sprites or sprite_ffmpeg)
    ready = encoder_ready(True) if needs_brotli else encoder_ready()
    if (
        (optimized or catalog.is_file())
        and not ready
        and (catalog.is_file() or any(p.suffix.lower() == ".png" for p in source_files(root, platform)))
    ):
        if worker:
            raise RuntimeError("Packaging requires Pillow 12.2.0 with libwebp 1.6.0")
        command = [
            encoder_python(True) if needs_brotli else encoder_python(),
            str(Path(__file__).resolve()),
            "--source",
            str(root),
            "--output",
            str(output),
            "--platform",
            platform,
            "--worker",
            "--jobs",
            str(jobs),
        ]
        if not optimized:
            command.append("--original")
        if terrain_sheets:
            command.append("--terrain-sheets")
        if packed_hd_sprites:
            command.append("--packed-hd-sprites")
        if video_sprites:
            command.append("--video-sprites")
        if sprite_ffmpeg:
            command.extend(("--sprite-ffmpeg", str(Path(sprite_ffmpeg).resolve())))
        command.append("--lossy-images" if lossy else "--lossless-images")
        if cache:
            command += ["--cache", str(cache)]
        subprocess.run(command, check=True)
        return json.loads(output.with_suffix(".json").read_text())
    # Catalog validation and compilation also use Pillow. Import them only after
    # handing off to the pinned encoder, including original-byte exports.
    if catalog.is_file():
        import importlib.util
        spec = importlib.util.spec_from_file_location("terrain_tileset", root / "tools/terrain_tileset.py")
        compiler = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(compiler)
        terrain_document = compiler.terrain_catalog.load(content)
        compiler.validate(terrain_document, root)
    else:
        terrain_document = None

    if cache is None:
        cache = root / "build/asset-cache"
    cache = Path(cache).resolve()
    if (
        cache == output
        or cache.is_relative_to(output)
        or any(cache == content / d or cache.is_relative_to(content / d) for d in ASSET_DIRS)
    ):
        raise ValueError("Cache must be outside exported and source assets")
    output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    destinations = set()
    files = list(source_files(root, platform))
    if optimized:
        verify_highres_atlases(content)
    terrain_sprites = {
        material["sprite"] for material in terrain_document["materials"]
        if material["sprite"].startswith("data/gfx/terrain-")
    } if (terrain_sheets or video_sprites or sprite_ffmpeg) and terrain_document else set()
    sheets = sheet_plan(files, content, terrain_sprites) if optimized else {}
    terrain_selections = {}
    hd_selections = {}
    native_selections = {}
    hd_rows, hd_groups, hd_references, hd_index_groups = [], {}, {}, {}
    if optimized and (packed_hd_sprites or video_sprites or sprite_ffmpeg):
        if str(ROOT) not in sys.path:
            sys.path.insert(0, str(ROOT))
        from tools import packed_sprite_assets
        hd_rows, hd_groups = packed_sprite_assets.plan(content, terrain_document)
        if video_sprites and not sprite_ffmpeg and lossy:
            sys.path.insert(0, str(ROOT / "scons"))
            from dev_store import Lease
            from recording_dependencies import build
            directory = cache / "host-sprite-encoder"
            with Lease(directory, exclusive=True, track_use=False):
                prefix = build(directory / "prefix", directory / "sources", jobs=jobs, sprite_tool=True)
            sprite_ffmpeg = prefix / "bin" / ("ffmpeg.exe" if os.name == "nt" else "ffmpeg")
        encoder = packed_sprite_assets.encoder_identity(sprite_ffmpeg) if sprite_ffmpeg else None
    native_groups = {}
    if optimized and (packed_hd_sprites or video_sprites or sprite_ffmpeg):
        materials = {m["sprite"]: m for m in (terrain_document or {}).get("materials", [])}
        for sprite, pages in sheets.items():
            if sprite not in terrain_sprites and sprite != "data/gfx/unit":
                continue
            for page in pages:
                layer = 5 if page["rotated"] else 4
                for offset, path in enumerate(page["frames"]):
                    index = page["first"] + offset
                    stem = Path(sprite).name
                    if sprite == "data/gfx/unit":
                        clip, measured = packed_sprite_assets.unit_clip(index)
                        stem += "-" + clip
                        crf = 26 if layer == 5 and measured is not None else None
                    else:
                        crf = 26
                    name = stem + ("-team" if layer == 5 else "-base")
                    group = native_groups.setdefault(sprite, {}).setdefault(name, dict(
                        material=materials.get(sprite), crf=crf, native=True, fingerprints=sprite in terrain_sprites, frames=[]))
                    group["frames"].append((index, sprite, layer, path))
    packed = {f for plan in sheets.values() for sheet in plan for f in sheet["frames"]}
    packed_fingerprints = {}
    with tempfile.TemporaryDirectory(
        prefix=output.name + "-staging-", dir=output.parent
    ) as temporary, encoding_executor(jobs) as encoders:
        stage = Path(temporary)
        # Submit independent work before consuming it in source order. Workers
        # never publish the export or mutate its ordered audit/destination set.
        hd_futures = {
            name: encoders.submit(packed_sprite_assets.encode_group, name, group, cache, platform, encoder, lossy)
            for name, group in hd_groups.items()
        }
        native_futures = {
            sprite: {name: encoders.submit(packed_sprite_assets.encode_group, name, group, cache, platform,
                                          encoder if platform != "web" else None, lossy)
                     for name, group in groups.items()}
            for sprite, groups in native_groups.items()
        }
        hd_sources = {path for group in hd_groups.values() for _, _, _, path in group["frames"]}
        encoded = {}
        for source in files:
            relative = source.relative_to(content)
            public_png = platform == "linux" and relative.parts[:2] == ("data", "screenshots")
            if optimized and source not in packed and source not in hd_sources and source.suffix.lower() == ".png" and not public_png:
                encoded[source] = encoders.submit(encode_image, source, relative.as_posix(), cache,
                                                  lossy and not is_data_image(relative))
        encoded_sheets = {}
        encoded_terrain = {}
        for sprite, plan in sheets.items():
            if sprite in native_futures:
                continue
            if sprite in terrain_sprites:
                encoded_terrain[sprite] = encoders.submit(encode_terrain_sheet_group, plan, content, cache, lossy)
                continue
            for sheet in plan:
                relative = Path(sprite).parent / sheet["name"]
                encoded_sheets[relative] = encoders.submit(encode_sheet, sheet, content, cache, lossy, relative)
        for name, future in hd_futures.items():
            directory, result, cache_hit = future.result()
            hd_selections[name] = {key: value for key, value in result.items() if key not in ("refs", "hashes", "fingerprints")}
            hd_selections[name]["cache_hit"] = cache_hit
            if result["winner"] is None:
                continue
            group = hd_groups[name]
            if platform == "web":
                frames = sorted(group["frames"])
                family = Path(group["material"]["sprite"]).name if group["material"] else None
                hd_index_groups.setdefault(family, []).append(
                    [frames[0][2], [ident for _, ident, _, _ in frames]])
            for index, ident, layer, path in group["frames"]:
                hd_references[ident, layer] = result["refs"].get(index, result["refs"].get(str(index)))
                packed.add(path)
            source_bytes = sum(path.stat().st_size for _, _, _, path in group["frames"])
            for number, (filename, digest) in enumerate(sorted(result["hashes"].items())):
                destination = packed_sprite_assets.HD / filename
                if destination in destinations:
                    raise ValueError("Duplicate exported path: " + str(destination))
                destinations.add(destination)
                target = stage / destination
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(directory / filename, target)
                records.append(dict(source="data/highres/v1/frames.txt", output=destination.as_posix(),
                                    source_sha256=directory.name.removeprefix("sprite-pack-"),
                                    output_sha256=digest, source_bytes=source_bytes if number == 0 else 0,
                                    output_bytes=target.stat().st_size, lossy=result["winner"] != "lossless",
                                    recipe=packed_sprite_assets.PACK_RECIPE, sprite_group="hd/" + name))
        for source in hd_sources - packed:
            relative = source.relative_to(content)
            encoded[source] = encoders.submit(encode_image, source, relative.as_posix(), cache, lossy)
        for source in files:
            if source in packed:
                continue
            relative = source.relative_to(content)
            blob = source
            record = dict(lossy=False)
            audit_fields = {}
            # AppStream screenshot references are public PNG URLs, not image-loader
            # logical names. Keep their original representation on Linux.
            public_png = platform == "linux" and relative.parts[:2] == (
                "data",
                "screenshots",
            )
            if optimized and source.suffix.lower() == ".png" and not public_png:
                blob, record = encoded[source].result()
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
                if hd_references:
                    runtime_rows = {None: ["GLOB2_HIGHRES 2"]}
                    family_names = sorted((Path(material["sprite"]).name
                                           for material in (terrain_document or {}).get("materials", [])
                                           if material["sprite"].startswith("data/gfx/terrain-")),
                                          key=lambda name: (-len(name), name))
                    for row in hd_rows:
                        layers = [hd_references.get((row[0], layer), re.sub(r"\.png$", ".webp", row[layer]))
                                  for layer in (4, 5)]
                        family = packed_sprite_assets.terrain_family(row[0], family_names) if platform == "web" else None
                        runtime_rows.setdefault(family, ["GLOB2_HIGHRES 2"]).append(" ".join(row[:4] + layers))
                    target.write_text("\n".join(runtime_rows.pop(None)) + "\n")
                    for family, rows in sorted(runtime_rows.items()):
                        index_path = relative.with_name(f"{family}.frames")
                        if index_path in destinations:
                            raise ValueError("Duplicate exported path: " + str(index_path))
                        destinations.add(index_path)
                        blob = ("\n".join(rows) + "\n").encode()
                        (stage / index_path).write_bytes(blob)
                        records.append(dict(source=relative.as_posix(), output=index_path.as_posix(),
                            source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                            output_sha256=hashlib.sha256(blob).hexdigest(), source_bytes=0,
                            output_bytes=len(blob), lossy=False, recipe=packed_sprite_assets.PACK_RECIPE,
                            sprite_hd_index_groups=hd_index_groups.get(family, [])))
                    if platform == "web":
                        audit_fields["sprite_hd_index_groups"] = hd_index_groups.get(None, [])
                else:
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
                    **audit_fields,
                )
            )
        for sprite, plan in sheets.items():
            if sprite in native_futures:
                lines = ["GLOB2_SPRITES 2"]
                index_groups = []
                index_path = Path(sprite + ".sheet")
                for name, future in native_futures[sprite].items():
                    directory, result, cache_hit = future.result()
                    if result["winner"] is None:
                        raise ValueError("Cannot pack native sprite group: " + name)
                    audit = {key: value for key, value in result.items() if key not in ("refs", "hashes", "fingerprints")}
                    audit["cache_hit"] = cache_hit
                    native_selections[name] = audit
                    if sprite in terrain_sprites:
                        terrain_selections[sprite] = audit
                    group = native_groups[sprite][name]
                    index_groups.append(len(group["frames"]))
                    for index, _, layer, path in group["frames"]:
                        key = str(index) if cache_hit else index
                        lines.append(f"{result['refs'][key]} {'rotated' if layer == 5 else 'image'} {index}")
                        if group["fingerprints"]:
                            packed_fingerprints[path.relative_to(content).as_posix()] = result["fingerprints"][key]
                    source_bytes = sum(path.stat().st_size for _, _, _, path in group["frames"])
                    for filename, digest in sorted(result["hashes"].items()):
                        destination = Path(sprite).parent / filename
                        if destination in destinations:
                            raise ValueError("Duplicate exported path: " + str(destination))
                        destinations.add(destination)
                        target = stage / destination
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(directory / filename, target)
                        records.append(dict(source=index_path.as_posix(), output=destination.as_posix(),
                            source_sha256=directory.name.removeprefix("sprite-pack-"),
                            output_sha256=digest, source_bytes=source_bytes,
                            output_bytes=target.stat().st_size, lossy=result["winner"] != "lossless",
                            recipe=packed_sprite_assets.PACK_RECIPE, sprite_group="native/" + name))
                        source_bytes = 0
                if index_path in destinations:
                    raise ValueError("Duplicate exported path: " + str(index_path))
                destinations.add(index_path)
                blob = ("\n".join(lines) + "\n").encode()
                (stage / index_path).write_bytes(blob)
                digest = hashlib.sha256(blob).hexdigest()
                records.append(dict(source=index_path.as_posix(), output=index_path.as_posix(),
                    source_sha256=digest, output_sha256=digest, source_bytes=0,
                    output_bytes=len(blob), lossy=False, recipe=packed_sprite_assets.PACK_RECIPE,
                    sprite_index_groups=index_groups))
                continue
            if sprite in encoded_terrain:
                pages, terrain_selections[sprite] = encoded_terrain[sprite].result()
            else:
                pages = [encoded_sheets[Path(sprite).parent / sheet["name"]].result() for sheet in plan]
            for sheet, (source, blob, record) in zip(plan, pages):
                relative = Path(sprite).parent / sheet["name"]
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
                                source=f.relative_to(content).as_posix(),
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
        if terrain_document and (compiled := terrain_document.get("compiled_pack")):
            source_hashes = compiler.validate(terrain_document, root)
            fingerprints = terrain_runtime_fingerprints(
                compiler, source_hashes, content, stage,
                {name: plan for name, plan in sheets.items() if name not in native_groups}, packed_fingerprints
            )
            pages = compiled_terrain_pack(compiler, terrain_document, root, cache, source_hashes, fingerprints)
            catalog_digest = catalog_sha256(compiler.terrain_catalog, content)
            for generated in pages:
                destination = Path(compiled).parent / generated.name
                if destination in destinations:
                    raise ValueError("Duplicate compiled terrain path: " + str(destination))
                destinations.add(destination)
                target = stage / destination
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(generated, target)
                records.append(dict(source="data/terrain/tileset.json", output=destination.as_posix(),
                    source_sha256=catalog_digest, output_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                    source_bytes=0, output_bytes=target.stat().st_size, lossy=False,
                    recipe=None, generated_by="tools/terrain_tileset.py"))
        audit = dict(
            policy=POLICY,
            platform=platform,
            optimized=optimized,
            lossy_images=lossy,
            image_recipe=image_recipe(lossy) if optimized else None,
            files=records,
            terrain_sheet_selections=terrain_selections,
            hd_sprite_selections=hd_selections,
            native_sprite_selections=native_selections,
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
    jobs=None,
    *,
    terrain_sheets=False,
    packed_hd_sprites=False,
    sprite_ffmpeg=None,
    video_sprites=False,
):
    """Export a complete runtime tree and return its source/output audit.

    Source artwork stays untouched. The adjacent JSON audit owns the generated
    output directory and is excluded from shipped resources. An encoder worker
    acquires the output lease itself, avoiding a recursive lock in its parent.
    """
    jobs = packaging_jobs(jobs)
    needs_brotli = platform == "web" and optimized and (packed_hd_sprites or video_sprites or sprite_ffmpeg)
    ready = encoder_ready(True) if needs_brotli else encoder_ready()
    if (optimized or (Path(root) / CONTENT / "data/terrain/tileset.json").is_file()) and not ready and not worker:
        return _export_assets(root, output, platform, optimized, lossy, cache, worker, jobs,
                              terrain_sheets=terrain_sheets, packed_hd_sprites=packed_hd_sprites,
                              sprite_ffmpeg=sprite_ffmpeg, video_sprites=video_sprites)
    sys.path.insert(0, str(ROOT / "scons"))
    from dev_store import Lease

    with Lease(Path(output).resolve(), exclusive=True, track_use=False):
        return _export_assets(root, output, platform, optimized, lossy, cache, worker, jobs,
                              terrain_sheets=terrain_sheets, packed_hd_sprites=packed_hd_sprites,
                              sprite_ffmpeg=sprite_ffmpeg, video_sprites=video_sprites)


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
    parser.add_argument("--terrain-sheets", action="store_true",
                        help="Pilot catalog terrain sheets using lossless or Q75 WebP")
    parser.add_argument("--packed-hd-sprites", action="store_true",
                        help="Pilot verified packed HD terrain and stock unit sprites")
    parser.add_argument("--video-sprites", action="store_true",
                        help="Try RGB H.264 for HD groups with the pinned host encoder")
    parser.add_argument("--sprite-ffmpeg", type=Path,
                        help="Try RGB H.264 for HD groups using this pinned host FFmpeg executable")
    parser.add_argument("--jobs", type=int, help="Concurrent image/sheet encoders (default: available CPUs)")
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.encoder_python:
        print(encoder_python(args.platform == "web"))
        return
    if args.jobs is not None and args.jobs < 1:
        parser.error("--jobs must be a positive integer")
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
        args.jobs,
        terrain_sheets=args.terrain_sheets,
        packed_hd_sprites=args.packed_hd_sprites,
        sprite_ffmpeg=args.sprite_ffmpeg,
        video_sprites=args.video_sprites,
    )
    print(
        "Runtime assets: %d -> %d bytes (%d files)"
        % (result["source_bytes"], result["output_bytes"], len(result["files"]))
    )


if __name__ == "__main__":
    main()
