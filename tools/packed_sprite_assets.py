# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded candidate selection for native and HD sprite packs."""

import hashlib
from itertools import groupby
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

from PIL import Image

from tools import package_assets as images

HD = Path("data/highres/v1")
# Frozen HD profiles from the terrain and stock unit probes.
TERRAIN_CRF = {
    "terrain-grass": 20.25,
    "terrain-water": 24.5,
    "terrain-ember_field": 26.5,
    "terrain-lava": 29.5,
    "terrain-molten_lava_pool": 29.5,
}
UNIT_CRF = (31, 31, 30.25, 31.25, 30, 29.5, 29.75)
PACK_RECIPE = "packed-sprites-v9"
# How video groups store alpha: one .za block per chunk. Part of the encode cache key
# only, so cached groups re-encode without invalidating the curves (group_identity);
# curves measured with per-frame .z blocks slightly overstate video sizes.
ALPHA_LAYOUT = "chunk-za"
# Perceptual gate for video candidates: frames per side of each scored mosaic,
# and the dark and light backgrounds that frames with partial alpha are scored over.
QUALITY_CELLS = 16
QUALITY_BACKGROUNDS = (0.1, 0.9)
# Every other HD frame (terrain decor, buildings, resources, icons...) packs into
# WebP-only groups of at most GROUP_PIXEL_BYTES of decoded RGBA, so one worker
# holds one group at a time: decor by frame number, the rest ordered by size so
# that equally sized frames share pages.
DECOR = "terrain-decor"
GROUP_PIXEL_BYTES = 64 << 20


def encoder_identity(ffmpeg):
    path = Path(ffmpeg).resolve()
    version = subprocess.check_output([path, "-version"], text=True)
    lock = json.loads((images.ROOT / "buildsys/recording-versions.json").read_text())
    version_number = lock["ffmpeg"]["directory"].removeprefix("ffmpeg-")
    if not re.match(rf"ffmpeg version {re.escape(version_number)}(?:\s|[-+])", version):
        raise ValueError("Sprite video encoding requires the pinned FFmpeg " + version_number)
    if "--enable-libx264" not in version:
        raise ValueError("Sprite video encoding requires libx264rgb")
    return str(path), hashlib.sha256(path.read_bytes()).hexdigest()


def unit_clip(frame):
    if frame < 1792:
        clip = frame // 256
        return f"movement-{clip}", UNIT_CRF[clip]
    if frame < 2016:
        return f"turn-{(frame - 1792) // 32}", 26
    return "extras", None


def terrain_family(frame_id, families):
    return next((name for name in families
                 if frame_id.startswith(name) and frame_id[len(name):].isdigit()), None)


def plan(content, catalog):
    """Return original rows and independently selectable layer groups."""
    index = content / HD / "frames.txt"
    if not index.is_file():
        return [], {}
    header, *lines = index.read_text().splitlines()
    if header != "GLOB2_HIGHRES 1":
        raise ValueError("Unknown source HD sprite index")
    rows = [line.split() for line in lines if line.strip()]
    if any(len(row) != 6 for row in rows) or len({row[0] for row in rows}) != len(rows):
        raise ValueError("Invalid source HD sprite index")
    # Exports leave out the legacy terrain frames nothing draws (package_assets).
    rows = [row for row in rows if not images.legacy_terrain_unused(f"{HD.as_posix()}/{row[4]}")]
    owners = {}
    for row in rows:
        for path in row[4:]:
            owners[path] = owners.get(path, 0) + 1
    materials = {Path(m["sprite"]).name: m for m in (catalog or {}).get("materials", [])
                 if m["sprite"].startswith("data/gfx/terrain-")}
    groups, pooled = {}, {}
    families = sorted(materials, key=lambda name: (-len(name), name))

    def usable(path):
        return path != "-" and owners[path] == 1 and Path(path).name == path and path.endswith(".png")

    def pixel_size(row):
        """The largest layer's PNG dimensions (IHDR), without decoding it."""
        sizes = []
        for path in row[4:]:
            if usable(path):
                with (content / HD / path).open("rb") as png:
                    sizes.append(struct.unpack(">II", png.read(24)[16:24]))
        return max(sizes, default=(0, 0))

    def add(family, spec, crf, material, frame, row):
        for layer in (4, 5):
            path = row[layer]
            if not usable(path):
                continue
            name = family + ("-base" if layer == 4 else "-team")
            group = groups.setdefault(name, dict(material=spec, crf=crf if material or layer == 5 else None,
                                                 frames=[], index_rows={}))
            group["index_rows"][row[0]] = row[:4]
            group["frames"].append((frame, row[0], layer, content / HD / path))

    for row in rows:
        match = re.fullmatch(r"(.+?)(\d+)", row[0])
        if not match:
            pooled.setdefault("sprites", []).append((0, row))
            continue
        prefix, digits = match.groups()
        if family := terrain_family(row[0], families):
            prefix, digits = family, row[0][len(family):]
        material = materials.get(prefix)
        frame = int(digits)
        if material:
            family, spec, crf = prefix, material, TERRAIN_CRF.get(prefix, 26)
        elif prefix == "unit":
            clip, crf = unit_clip(frame)
            family, spec = "unit-" + clip, None
        else:
            pooled.setdefault(DECOR if prefix == DECOR else "sprites", []).append((frame, row))
            continue
        add(family, spec, crf, material, frame, row)
    for pool, entries in sorted(pooled.items()):
        if pool == DECOR:
            entries.sort(key=lambda entry: entry[0])
        else:
            entries.sort(key=lambda entry: (pixel_size(entry[1]), entry[1][0]))
            entries = [(index, row) for index, (_, row) in enumerate(entries)]
        chunk, used = 0, 0
        for frame, row in entries:
            width, height = pixel_size(row)
            size = width * height * 4 * sum(1 for path in row[4:] if usable(path))
            if used and used + size > GROUP_PIXEL_BYTES:
                chunk, used = chunk + 1, 0
            used += size
            add(f"{pool}-{chunk}", None, None, None, frame, row)
    return rows, groups


def quality_metric():
    """The perceptual metric this encoder can run: SSIMULACRA2 from the pinned
    asset requirements, else SSIM on numpy alone (build pipelines that provide
    their own encoder), else None, which rejects video."""
    try:
        import ssimulacra2
        if ssimulacra2.__version__ == images.SSIMULACRA2_VERSION:
            return "ssimulacra2"
    except ImportError:
        pass
    try:
        import numpy  # noqa: F401
        return "ssim"
    except ImportError:
        return None


def _ssimulacra2(reference, distorted):
    import io
    from ssimulacra2 import compute_ssimulacra2

    streams = []
    for image in (reference, distorted):
        stream = io.BytesIO()
        image.save(stream, format="PNG", compress_level=0)
        stream.seek(0)
        streams.append(stream)
    return compute_ssimulacra2(*streams)


def _libjxl_ssimulacra2(reference, distorted):
    """libjxl's reference ssimulacra2 tool: several times faster than the numpy port
    and a third of its memory. Only the curve tool needs it (CURVE_METRIC)."""
    with tempfile.TemporaryDirectory(prefix="ssimulacra2-") as temp:
        paths = [Path(temp) / name for name in ("reference.png", "distorted.png")]
        for image, path in zip((reference, distorted), paths):
            image.save(path, compress_level=1)
        output = subprocess.run(["ssimulacra2", *map(str, paths)], capture_output=True, text=True, check=True)
    return float(output.stdout.split()[-1])


def libjxl_version():
    """The installed libjxl tools' version, or None."""
    try:
        output = subprocess.run(["cjxl", "--version"], capture_output=True, text=True, check=True).stdout
        return output.split()[1].lstrip("v")
    except (OSError, subprocess.CalledProcessError, IndexError):
        return None


def _ssim(reference, distorted):
    """Mean SSIM (Wang et al. 2004: 11-tap Gaussian, sigma 1.5) over RGB channels."""
    import numpy as np

    taps = np.exp(-0.5 * (np.arange(-5, 6) / 1.5) ** 2)
    taps /= taps.sum()

    def blur(plane):
        if min(plane.shape) < len(taps):
            return np.full((1, 1), plane.mean())
        rows = sum(w * plane[:, i:plane.shape[1] - len(taps) + 1 + i] for i, w in enumerate(taps))
        return sum(w * rows[i:rows.shape[0] - len(taps) + 1 + i, :] for i, w in enumerate(taps))

    a = np.asarray(reference, dtype=np.float64)
    b = np.asarray(distorted, dtype=np.float64)
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    scores = []
    for channel in range(3):
        x, y = a[:, :, channel], b[:, :, channel]
        mx, my = blur(x), blur(y)
        vx, vy, cov = blur(x * x) - mx * mx, blur(y * y) - my * my, blur(x * y) - mx * my
        scores.append((((2 * mx * my + c1) * (2 * cov + c2)) /
                       ((mx * mx + my * my + c1) * (vx + vy + c2))).mean())
    return float(sum(scores) / 3)


def perceptual_score(references, decoded, metric):
    """`metric` ("ssimulacra2", "libjxl" or "ssim") of decoded frames against their sources.

    Single tiles are too small for multi-scale metrics, so frames are laid out
    in index order on mosaics of up to QUALITY_CELLS x QUALITY_CELLS cells and
    the mosaic scores are averaged by pixel count. As libjxl's ssimulacra2
    does, groups with partial alpha are flattened over a dark and a light
    background and the worse score counts; opaque groups use mid grey."""
    scorer = {"ssimulacra2": _ssimulacra2, "libjxl": _libjxl_ssimulacra2, "ssim": _ssim}[metric]
    order = sorted(references)
    width = max(references[index].width for index in order)
    height = max(references[index].height for index in order)
    opaque = all(references[index].getchannel("A").getextrema() == (255, 255) for index in order)
    backgrounds = (0.5,) if opaque else QUALITY_BACKGROUNDS
    total = pixels = 0
    for start in range(0, len(order), QUALITY_CELLS * QUALITY_CELLS):
        batch = order[start:start + QUALITY_CELLS * QUALITY_CELLS]
        columns = min(QUALITY_CELLS, len(batch))
        size = (columns * width, -(-len(batch) // columns) * height)
        pages = []
        for frames in (references, decoded):
            page = Image.new("RGBA", size, (0, 0, 0, 0))
            for offset, index in enumerate(batch):
                page.paste(frames[index], (offset % columns * width, offset // columns * height))
            pages.append(page)
        scores = []
        for background in backgrounds:
            level = round(background * 255)
            flat = [Image.alpha_composite(Image.new("RGBA", size, (level, level, level, 255)), page).convert("RGB")
                    for page in pages]
            scores.append(scorer(*flat))
        total += min(scores) * size[0] * size[1]
        pixels += size[0] * size[1]
    return round(total / pixels, 5 if metric == "ssim" else 2)


def write_webp_page_run(frames, size, name, mode, directory, fingerprint, quality=None, keep_decoded=False,
                        setting=None):
    """`setting`: an exact lossy quality chosen from a quality curve, in place of
    images.group_lossy_quality(quality)."""
    width, height = size
    columns = min(images.SHEET_COLUMNS, 2048 // width)
    capacity = min(images.SHEET_FRAMES, columns * (2048 // height))
    refs, files, decoded_frames, fingerprints = {}, [], {}, {}
    for start in range(0, len(frames), capacity):
        batch = frames[start:start + capacity]
        page = Image.new("RGBA", (columns * width, -(-len(batch) // columns) * height))
        for offset, (index, image) in enumerate(batch):
            page.paste(image, (offset % columns * width, offset // columns * height))
        filename = f"pack-{name}-{start:04}.webp"
        lossless = mode == "lossless"
        page.save(directory / filename, format="WEBP",
                  **(images.lossless_webp() if lossless else dict(
                      lossless=False, quality=setting if setting is not None else images.group_lossy_quality(quality),
                      method=images.ENCODING["method"], exact=True)))
        decoded = Image.open(directory / filename).convert("RGBA")
        if decoded.size != page.size:
            raise ValueError("Packed WebP changed dimensions")
        files.append(filename)
        for offset, (index, reference) in enumerate(batch):
            x, y = offset % columns * width, offset // columns * height
            crop = decoded.crop((x, y, x + width, y + height))
            if crop.getchannel("A").tobytes() != reference.getchannel("A").tobytes() or (
                lossless and crop.tobytes() != reference.tobytes()
            ):
                raise ValueError("Packed WebP changed required pixels")
            if keep_decoded:
                decoded_frames[index] = crop
            if fingerprint:
                fingerprints[index] = fingerprint(crop)
            refs[index] = f"@{filename},{x},{y},{width},{height}"
    return dict(files=files, refs=refs, decoded=decoded_frames, fingerprints=fingerprints)


def write_webp(frames, name, mode, directory, fingerprint, quality=None, keep_decoded=False, setting=None):
    result = dict(files=[], refs={}, decoded={}, fingerprints={})
    for run, (size, frames_with_size) in enumerate(groupby(frames, key=lambda item: item[1].size)):
        page = write_webp_page_run(list(frames_with_size), size, f"{name}-{run:03}", mode, directory, fingerprint,
                                   quality, keep_decoded, setting)
        result["files"].extend(page["files"])
        result["refs"].update(page["refs"])
        result["decoded"].update(page["decoded"])
        result["fingerprints"].update(page["fingerprints"])
    return result


def video_layout(frames, material, size):
    """Yield independent sequences, each a list of frames with matching tiles."""
    by_index = dict(frames)
    if not material:
        # Direction boundaries are multiples of 32 in stock clips.
        if not all(start + i in by_index for start in by_index if start % 8 == 0 for i in range(8)):
            return
        for start in sorted(index for index in by_index if index % 8 == 0):
            yield [[start + i] for i in range(8)], 1
        return
    phases = material.get("animation_frames", 1)
    blocks = material.get("variant_blocks", 1)
    stride = material.get("animation_stride", len(material["variants"]))
    if phases == 1:
        if blocks != 4:
            return
        phases, blocks, stride = blocks, 1, stride // blocks
    tiles = stride // blocks
    if not tiles or stride % blocks:
        return
    width, height = size
    side = math.isqrt(tiles)
    columns = min(side if side * side == tiles else 16, 2048 // width, tiles)
    for block in range(blocks):
        for phase in range(0, phases, 8):
            count = min(phases - phase, 8)
            rows = min(2048 // height, (64 << 20) // (count * 4 * columns * width * height))
            capacity = columns * rows
            if not capacity:
                return
            for start in range(0, tiles, capacity):
                layout = [[p * stride + block * tiles + index
                           for index in range(start, min(start + capacity, tiles))]
                          for p in range(phase, phase + count)]
                yield layout, min(columns, len(layout[0]))


def write_video(frames, size, name, group, ffmpeg, directory, fingerprint):
    lookup = dict(frames)
    refs, files, decoded_frames, fingerprints = {}, [], {}, {}
    common = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-threads", "1",
              "-filter_threads", "1"]
    for chunk, (layout, columns) in enumerate(video_layout(frames, group["material"], size)):
        if any(index not in lookup or index in refs for ids in layout for index in ids):
            return dict(rejected="incomplete or overlapping video layout")
        width = max(lookup[index].width for ids in layout for index in ids)
        height = max(lookup[index].height for ids in layout for index in ids)
        count = len(layout)
        canvas = (columns * width, -(-len(layout[0]) // columns) * height)
        # Keep the complete decoded group within the runtime's RGBA budget.
        if max(canvas) > 2048 or canvas[0] * canvas[1] * 4 * count > 64 << 20:
            return dict(rejected="video group exceeds decoded size limit")
        data = bytearray()
        alpha = []
        for ids in layout:
            page = Image.new("RGBA", canvas, (0, 0, 0, 255))
            for offset, index in enumerate(ids):
                page.paste(lookup[index], (offset % columns * width, offset // columns * height))
            data.extend(page.convert("RGB").tobytes())
            alpha.append(page.getchannel("A"))
        filename = f"pack-{name}-{chunk:04}.h264"
        result = subprocess.run(common + ["-f", "rawvideo", "-pixel_format", "rgb24", "-framerate", "30",
            "-video_size", f"{canvas[0]}x{canvas[1]}", "-i", "pipe:0", "-an", "-c:v", "libx264rgb",
            "-preset", "veryslow", "-tune", "psnr", "-crf", str(group["crf"]), "-refs", "1",
            "-bf", "0", "-g", str(count), "-sc_threshold", "0", "-threads", "1",
            "-x264-params", "aud=1:repeat-headers=1:open-gop=0", "-f", "h264", "pipe:1"],
            input=data, capture_output=True, check=True)
        if not result.stdout or len(result.stdout) > 16 << 20:
            return dict(rejected="video group exceeds encoded byte limit")
        (directory / filename).write_bytes(result.stdout)
        files.append(filename)
        decoded = subprocess.run(common + ["-f", "h264", "-i", "pipe:0", "-f", "rawvideo",
                                 "-pix_fmt", "rgb24", "-threads", "1", "pipe:1"],
                                 input=result.stdout, capture_output=True, check=True).stdout
        rgb_bytes = canvas[0] * canvas[1] * 3
        if len(decoded) != count * rgb_bytes:
            return dict(rejected="video decoder did not produce every frame")
        # One alpha block per chunk: every frame's plane in frame order, so a
        # chunk's frames cost one file (PackedSpriteFrame slices it by frame).
        alpha_file, planes = "-", None
        if any(plane.getextrema() != (255, 255) for plane in alpha):
            alpha_file = f"{Path(filename).stem}.za"
            compressed = zlib.compress(b"".join(plane.tobytes() for plane in alpha), 9)
            (directory / alpha_file).write_bytes(compressed)
            files.append(alpha_file)
            planes = zlib.decompress(compressed)
        plane_bytes = canvas[0] * canvas[1]
        for frame, ids in enumerate(layout):
            image = Image.frombytes("RGB", canvas, decoded[frame * rgb_bytes:(frame + 1) * rgb_bytes]).convert("RGBA")
            if planes is not None:
                image.putalpha(Image.frombytes("L", canvas, planes[frame * plane_bytes:(frame + 1) * plane_bytes]))
            for offset, index in enumerate(ids):
                x, y = offset % columns * width, offset // columns * height
                reference = lookup[index]
                crop_width, crop_height = reference.size
                crop = image.crop((x, y, x + crop_width, y + crop_height))
                if crop.getchannel("A").tobytes() != reference.getchannel("A").tobytes():
                    return dict(rejected="video changed source alpha")
                decoded_frames[index] = crop
                if fingerprint:
                    fingerprints[index] = fingerprint(crop)
                ref = f"@{filename},{alpha_file},{canvas[0]},{canvas[1]},{count},{frame}"
                if (x, y, crop_width, crop_height) != (0, 0, *canvas):
                    ref += f",{x},{y},{crop_width},{crop_height}"
                refs[index] = ref
    if set(refs) != set(lookup):
        return dict(rejected="video layout does not cover logical frames")
    return dict(files=files, refs=refs, decoded=decoded_frames, fingerprints=fingerprints)


def score(candidate, directory, platform, sources, group):
    paths = sorted(candidate["files"])
    if group.get("native"):
        rows = [f"{candidate['refs'][index]} {'rotated' if layer == 5 else 'image'} {index}\n"
                for index, _, layer, _ in sources]
    else:
        rows = []
        for index, ident, layer, _ in sources:
            fields = group.get("index_rows", {}).get(ident, [ident])
            ref = candidate["refs"][index]
            fields = fields + ([ref, "-"] if layer == 4 else ["-", ref])
            rows.append(" ".join(fields) + "\n")
        if group["material"] and platform == "web":
            rows.insert(0, "GLOB2_HIGHRES 2\n")
    index = "".join(rows).encode()
    sizes = {name: (directory / name).stat().st_size for name in paths}
    result = dict(stored_bytes=sum(sizes.values()) + len(index))
    if platform == "web":
        from buildsys.web_assets import split, PART_BYTES
        result["transfer_bytes"] = 0
        for part in split(paths, sizes, PART_BYTES):
            payload = b"".join((directory / name).read_bytes() for name in part)
            result["transfer_bytes"] += brotli_size(payload)
        # The browser installs these independently compressed rows as one index.
        result["transfer_bytes"] += brotli_size(index)
    return result


def brotli_size(payload):
    import brotli
    from buildsys.web_assets import DATA_BROTLI_QUALITY, BROTLI_WINDOW
    return len(brotli.compress(payload, quality=DATA_BROTLI_QUALITY, lgwin=BROTLI_WINDOW))


# Precomputed quality curves (tools/sprite_quality_curves.py): for every group,
# stored bytes, browser transfer bytes and perceptual score at each grid setting
# of every codec. The export picks the cheapest point meeting its target among
# the codecs that build has, then encodes only that point.
CURVE_FILE = images.ROOT / "tools/sprite-quality-curves.json"
CURVE_VERSION = 1
# Dense enough that exports pick measured points only: neighbouring settings differ
# by encoder noise (quantiser steps), which no fitted or interpolated curve predicts.
WEBP_GRID = tuple(range(20, 101))                     # every integer quality
CRF_GRID = tuple(crf / 2 for crf in range(28, 81))    # CRF 14 to 40 in half steps
# Ties prefer lossless, then WebP, then video; then the higher quality.
CODEC_ORDER = ("lossless", "webp", "h264")


def crf_key(crf):
    """A CRF as a curve key: "24" or "24.5"."""
    return f"{crf:g}"


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def group_identity(group):
    """What a group's curve was measured from: its frames, spec and pack recipe."""
    return _digest([PACK_RECIPE, CURVE_VERSION, group["material"], bool(group.get("native")),
                    group.get("index_rows"), group["crf"] is not None,
                    [[index, ident, layer, hashlib.sha256(Path(path).read_bytes()).hexdigest()]
                     for index, ident, layer, path in sorted(group["frames"])]])


def webp_identity():
    return _digest([images.PILLOW_VERSION, images.WEBP_VERSION, images.ENCODING["method"], images.lossless_webp()])


def video_identity():
    """The pinned sprite encoder's sources, not its binary, which differs between
    machines building the same pinned FFmpeg and x264."""
    lock = json.loads((images.ROOT / "buildsys/recording-versions.json").read_text())
    return _digest([lock["ffmpeg"]["sha256"], lock["x264"]["sha256"]])


# Curves are scored with libjxl's reference ssimulacra2 at this version (the curve tool
# checks the installed one); exports only compare this tag and never run libjxl.
LIBJXL_VERSION = "0.13.0"


def metric_identity():
    return "libjxl ssimulacra2 " + LIBJXL_VERSION


def load_curves(path=CURVE_FILE):
    try:
        document = json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {}
    return document.get("groups", {}) if document.get("version") == CURVE_VERSION else {}


def curve_points(group):
    """Every setting a group's curve measures: lossless, each WebP quality and,
    where the group admits video, each CRF."""
    points = [("lossless", None)] + [("webp", q) for q in WEBP_GRID]
    return points + ([("h264", crf) for crf in CRF_GRID] if group["crf"] is not None else [])


def curve_header(group):
    """What a group's curve is measured with, so a stale one can be told apart."""
    return dict(group=group_identity(group), webp_encoder=webp_identity(), metric=metric_identity(),
                h264_encoder=video_identity() if group["crf"] is not None else None)


def find_curve(curves, scope, name, group):
    """The curve measured for exactly this group: "<scope>/<name>" for the standard
    export, or an edition's own "<scope>/<name>@<edition>" where its frames differ
    (lite's trimmed terrain). Falls back to the standard one, which select_point
    then reports as stale."""
    plain = curves.get(f"{scope}/{name}")
    variants = [curve for key, curve in curves.items() if key.startswith(f"{scope}/{name}@")]
    if not variants:
        return plain
    identity = group_identity(group)
    return next((curve for curve in [plain, *variants] if curve and curve.get("group") == identity), plain)


def measure_points(name, group, ffmpeg, points):
    """Measure some of a group's curve points (see curve_points): {"lossless": [stored,
    transfer]} and {"webp"|"h264": {key: [stored bytes, transfer bytes, libjxl
    SSIMULACRA2]}} for the points given, so one group can be split across workers."""
    sources = sorted(group["frames"])
    frames = []
    for index, _, _, path in sources:
        with Image.open(path) as image:
            frames.append((index, image.convert("RGBA")))
    references = dict(frames)
    size = tuple(max(dimensions) for dimensions in zip(*{image.size for _, image in frames}))
    measured = dict(webp={}, h264={})

    def sizes(candidate, directory):
        stored = score(candidate, directory, "generic", sources, group)["stored_bytes"]
        transfer = score(candidate, directory, "web", sources, group)["transfer_bytes"]
        return [stored, transfer]

    with tempfile.TemporaryDirectory(prefix="sprite-curve-") as temp:
        temp = Path(temp)
        for codec, setting in points:
            target = temp / f"{codec}-{setting}"
            target.mkdir()
            if codec == "lossless":
                measured["lossless"] = sizes(write_webp(frames, name, "lossless", target, None), target)
            elif codec == "webp":
                candidate = write_webp(frames, name, images.GROUP_LOSSY_MODE, target, None,
                                       keep_decoded=True, setting=setting)
                measured["webp"][str(setting)] = sizes(candidate, target) + [
                    perceptual_score(references, candidate["decoded"], "libjxl")]
            else:
                try:
                    candidate = write_video(frames, size, name, dict(group, crf=setting), ffmpeg, target, None)
                except subprocess.CalledProcessError:
                    candidate = dict(rejected="video codec failed")
                measured["h264"][crf_key(setting)] = None if "rejected" in candidate else sizes(candidate, target) + [
                    perceptual_score(references, candidate["decoded"], "libjxl")]
    return measured


def measure_group(name, group, ffmpeg):
    """A group's whole curve entry (tools/sprite_quality_curves.py splits this across workers)."""
    return dict(curve_header(group), **measure_points(name, group, ffmpeg, curve_points(group)))


def select_point(curve, group, target, lossy, video, platform):
    """The cheapest curve point scoring at least `target` among the codecs this
    build has, as {codec, setting, predicted_bytes, predicted_score}, or
    {fallback: reason} when the curve does not describe this group and encoder."""
    if curve is None:
        return dict(fallback="no curve")
    if curve.get("group") != group_identity(group):
        return dict(fallback="sources changed")
    if curve.get("metric") != metric_identity() or curve.get("webp_encoder") != webp_identity():
        return dict(fallback="encoder or metric changed")
    use_video = video and curve.get("h264_encoder") == video_identity()
    column = 1 if platform == "web" else 0
    points = [("lossless", None, curve["lossless"][column], 100.0)]
    if lossy:
        points += [("webp", int(q), point[column], point[2]) for q, point in curve["webp"].items()]
    if lossy and use_video:
        points += [("h264", float(c), point[column], point[2]) for c, point in curve["h264"].items() if point]
    eligible = [point for point in points if point[3] >= target]
    codec, setting, size, value = min(eligible, key=lambda p: (
        p[2], CODEC_ORDER.index(p[0]), -p[3]))
    selection = dict(codec=codec, setting=setting, predicted_bytes=size, predicted_score=value, target=target)
    if video and not use_video:
        selection["note"] = "video curve measured with another encoder"
    return selection


def encode_group(name, group, cache, platform, encoder, lossy, quality=None, curve=None, target=None):
    """Pack a sprite group.

    With a quality `curve` that matches the group and a `target` score, encode
    only the cheapest curve point meeting the target among the available codecs
    (video needs an encoder and a CRF-eligible group). Otherwise compare
    lossless, lossy at images.group_lossy_quality(quality) (`quality`: the
    edition's lossy quality, None: the standard one) and, with an encoder and a
    CRF, video gated on looking at least as good as the lossy WebP; the smallest
    admissible candidate wins."""
    fingerprint = None
    if group.get("fingerprints"):
        from tools.terrain_tileset import pixel_fingerprint
        fingerprint = pixel_fingerprint
    size_policy = None
    if platform == "web":
        from buildsys.web_assets import PART_BYTES, DATA_BROTLI_QUALITY, BROTLI_WINDOW
        size_policy = (images.BROTLI_VERSION, PART_BYTES, DATA_BROTLI_QUALITY, BROTLI_WINDOW)
    sources = sorted(group["frames"])
    video_eligible = bool(encoder and lossy and group["crf"] is not None)
    selection = (select_point(curve, group, target, lossy, video_eligible, platform)
                 if target is not None else dict(fallback="no target"))
    curved = "codec" in selection
    # The gate's metric is part of the key: an SSIM selection is redone once SSIMULACRA2 is available.
    perceptual = None if curved else quality_metric() if video_eligible else None
    key = hashlib.sha256(json.dumps([PACK_RECIPE, ALPHA_LAYOUT, name, group["crf"], group["material"],
        platform, encoder, lossy, bool(fingerprint), images.image_recipe(quality=quality), zlib.ZLIB_RUNTIME_VERSION,
        perceptual, selection,
        size_policy, bool(group.get("native")), group.get("index_rows"),
        [[index, ident, layer, hashlib.sha256(path.read_bytes()).hexdigest()]
         for index, ident, layer, path in sources]], sort_keys=True).encode()).hexdigest()
    directory = cache / ("sprite-pack-" + key)
    marker = directory / "result.json"
    try:
        result = json.loads(marker.read_text())
        if all(hashlib.sha256((directory / name).read_bytes()).hexdigest() == digest
               for name, digest in result["hashes"].items()):
            return directory, result, True
    except (OSError, ValueError, KeyError):
        pass
    frames = []
    for index, _, _, path in sources:
        with Image.open(path) as image:
            frames.append((index, image.convert("RGBA")))
    sizes = {image.size for _, image in frames}
    size = tuple(max(dimensions) for dimensions in zip(*sizes))
    if any(min(dimensions) < 1 or max(dimensions) > 2048 for dimensions in sizes):
        return directory, dict(winner=None, candidates={}, reason="unsupported dimensions"), False
    cache.mkdir(parents=True, exist_ok=True)
    directory.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sprite-candidates-", dir=cache) as temp:
        temp = Path(temp)
        candidates = {}
        if curved and selection["codec"] == "h264":
            target_dir = temp / "video"
            target_dir.mkdir()
            try:
                video = write_video(frames, size, name, dict(group, crf=selection["setting"]), encoder[0],
                                    target_dir, fingerprint)
            except subprocess.CalledProcessError as error:
                video = dict(rejected="video codec failed", detail=error.stderr.decode(errors="replace").strip()[:512])
            candidates["video"] = video
            if "rejected" in video:
                # The curve's video point did not encode here: take its best point without video.
                selection = dict(select_point(curve, group, target, lossy, False, platform),
                                 note="video point rejected: " + video["rejected"])
        if curved and selection["codec"] != "h264":
            mode = "lossless" if selection["codec"] == "lossless" else images.GROUP_LOSSY_MODE
            target_dir = temp / mode
            target_dir.mkdir()
            candidates[mode] = write_webp(frames, name, mode, target_dir, fingerprint, quality,
                                          setting=selection["setting"])
        if not curved:
            modes = ["lossless", images.GROUP_LOSSY_MODE] if lossy else ["lossless"]
            for mode in modes:
                target_dir = temp / mode
                target_dir.mkdir()
                candidates[mode] = write_webp(frames, name, mode, target_dir, fingerprint, quality,
                                              keep_decoded=video_eligible and mode == images.GROUP_LOSSY_MODE)
            if video_eligible:
                target_dir = temp / "video"
                target_dir.mkdir()
                try:
                    video = write_video(frames, size, name, group, encoder[0], target_dir, fingerprint)
                except subprocess.CalledProcessError as error:
                    video = dict(rejected="video codec failed", detail=error.stderr.decode(errors="replace").strip()[:512])
                if "rejected" not in video and perceptual is None:
                    video["rejected"] = "no perceptual quality metric available"
                elif "rejected" not in video:
                    # Video only ships when it looks at least as good as Q75 WebP.
                    references = dict(frames)
                    webp = candidates[images.GROUP_LOSSY_MODE]
                    webp[perceptual] = perceptual_score(references, webp["decoded"], perceptual)
                    video[perceptual] = perceptual_score(references, video["decoded"], perceptual)
                    if video[perceptual] < webp[perceptual]:
                        video["rejected"] = "below Q75 " + {"ssimulacra2": "SSIMULACRA2", "ssim": "SSIM"}[perceptual]
                candidates["video"] = video
        for candidate in candidates.values():
            candidate.pop("decoded", None)
        for mode, candidate in candidates.items():
            if "files" in candidate:
                candidate.update(score(candidate, temp / mode, platform, sources, group))
        metric = "transfer_bytes" if platform == "web" else "stored_bytes"
        winner = min((mode for mode in candidates if "rejected" not in candidates[mode]),
                     key=lambda mode: candidates[mode][metric])
        chosen = candidates[winner]
        result = dict(winner=winner, refs=chosen["refs"], fingerprints=chosen["fingerprints"],
                      quality_metric=metric_identity() if curved else perceptual, curve=selection,
                      candidates={mode: {key: value for key, value in candidate.items()
                                         if key not in ("files", "refs", "fingerprints")}
                                  for mode, candidate in candidates.items()},
                      hashes={filename: hashlib.sha256((temp / winner / filename).read_bytes()).hexdigest()
                              for filename in chosen["files"]})
        for mode, candidate in candidates.items():
            if mode == winner:
                target_dir = directory
            else:
                target_dir = directory / mode
                target_dir.mkdir(exist_ok=True)
            for filename in candidate.get("files", []):
                os.replace(temp / mode / filename, target_dir / filename)
        (temp / "result.json").write_text(json.dumps(result, sort_keys=True))
        os.replace(temp / "result.json", marker)
        return directory, result, False
