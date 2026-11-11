# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded candidate selection for native and HD sprite packs."""

import hashlib
from itertools import groupby
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zlib

from PIL import Image, ImageChops

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
PACK_RECIPE = "packed-sprites-v8"


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
    owners = {}
    for row in rows:
        for path in row[4:]:
            owners[path] = owners.get(path, 0) + 1
    materials = {Path(m["sprite"]).name: m for m in (catalog or {}).get("materials", [])
                 if m["sprite"].startswith("data/gfx/terrain-")}
    groups = {}
    families = sorted(materials, key=lambda name: (-len(name), name))
    for row in rows:
        match = re.fullmatch(r"(.+?)(\d+)", row[0])
        if not match:
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
            continue
        for layer in (4, 5):
            path = row[layer]
            if path == "-" or owners[path] != 1 or Path(path).name != path or not path.endswith(".png"):
                continue
            name = family + ("-base" if layer == 4 else "-team")
            group = groups.setdefault(name, dict(material=spec, crf=crf if material or layer == 5 else None,
                                                 frames=[], index_rows={}))
            group["index_rows"][row[0]] = row[:4]
            group["frames"].append((frame, row[0], layer, content / HD / path))
    return rows, groups


def rgb_error(reference, decoded):
    visible = reference.getchannel("A").point(lambda value: 255 if value else 0)
    diff = ImageChops.difference(reference.convert("RGB"), decoded.convert("RGB"))
    return sum((index % 256) ** 2 * count
               for index, count in enumerate(diff.histogram(mask=visible)))


def write_webp_page_run(frames, size, name, mode, directory, fingerprint):
    width, height = size
    columns = min(images.SHEET_COLUMNS, 2048 // width)
    capacity = min(images.SHEET_FRAMES, columns * (2048 // height))
    refs, files, error, fingerprints = {}, [], 0, {}
    for start in range(0, len(frames), capacity):
        batch = frames[start:start + capacity]
        page = Image.new("RGBA", (columns * width, -(-len(batch) // columns) * height))
        for offset, (index, image) in enumerate(batch):
            page.paste(image, (offset % columns * width, offset // columns * height))
        filename = f"pack-{name}-{start:04}.webp"
        lossless = mode == "lossless"
        page.save(directory / filename, format="WEBP", lossless=lossless,
                  quality=75, method=4 if lossless else images.ENCODING["method"], exact=True)
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
            if not lossless:
                error += rgb_error(reference, crop)
            if fingerprint:
                fingerprints[index] = fingerprint(crop)
            refs[index] = f"@{filename},{x},{y},{width},{height}"
    return dict(files=files, refs=refs, visible_rgb_sse=error, fingerprints=fingerprints)


def write_webp(frames, name, mode, directory, fingerprint):
    result = dict(files=[], refs={}, visible_rgb_sse=0, fingerprints={})
    for run, (size, frames_with_size) in enumerate(groupby(frames, key=lambda item: item[1].size)):
        page = write_webp_page_run(list(frames_with_size), size, f"{name}-{run:03}", mode, directory, fingerprint)
        result["files"].extend(page["files"])
        result["refs"].update(page["refs"])
        result["fingerprints"].update(page["fingerprints"])
        result["visible_rgb_sse"] += page["visible_rgb_sse"]
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
    refs, files, error, fingerprints = {}, [], 0, {}
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
        for frame, ids in enumerate(layout):
            image = Image.frombytes("RGB", canvas, decoded[frame * rgb_bytes:(frame + 1) * rgb_bytes]).convert("RGBA")
            alpha_file = "-"
            if alpha[frame].getextrema() != (255, 255):
                alpha_file = f"{Path(filename).stem}-{frame}.z"
                compressed = zlib.compress(alpha[frame].tobytes(), 9)
                (directory / alpha_file).write_bytes(compressed)
                files.append(alpha_file)
                image.putalpha(Image.frombytes("L", canvas, zlib.decompress(compressed)))
            for offset, index in enumerate(ids):
                x, y = offset % columns * width, offset // columns * height
                reference = lookup[index]
                crop_width, crop_height = reference.size
                crop = image.crop((x, y, x + crop_width, y + crop_height))
                if crop.getchannel("A").tobytes() != reference.getchannel("A").tobytes():
                    return dict(rejected="video changed source alpha")
                error += rgb_error(lookup[index], crop)
                if fingerprint:
                    fingerprints[index] = fingerprint(crop)
                ref = f"@{filename},{alpha_file},{canvas[0]},{canvas[1]},{count},{frame}"
                if (x, y, crop_width, crop_height) != (0, 0, *canvas):
                    ref += f",{x},{y},{crop_width},{crop_height}"
                refs[index] = ref
    if set(refs) != set(lookup):
        return dict(rejected="video layout does not cover logical frames")
    return dict(files=files, refs=refs, visible_rgb_sse=error, fingerprints=fingerprints)


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


def encode_group(name, group, cache, platform, encoder, lossy):
    fingerprint = None
    if group.get("fingerprints"):
        from tools.terrain_tileset import pixel_fingerprint
        fingerprint = pixel_fingerprint
    size_policy = None
    if platform == "web":
        from buildsys.web_assets import PART_BYTES, DATA_BROTLI_QUALITY, BROTLI_WINDOW
        size_policy = (images.BROTLI_VERSION, PART_BYTES, DATA_BROTLI_QUALITY, BROTLI_WINDOW)
    sources = sorted(group["frames"])
    key = hashlib.sha256(json.dumps([PACK_RECIPE, name, group["crf"], group["material"],
        platform, encoder, lossy, bool(fingerprint), images.image_recipe(), zlib.ZLIB_RUNTIME_VERSION,
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
        modes = ["lossless", "q75"] if lossy else ["lossless"]
        for mode in modes:
            target = temp / mode
            target.mkdir()
            candidates[mode] = write_webp(frames, name, mode, target, fingerprint)
        if encoder and lossy and group["crf"] is not None:
            target = temp / "video"
            target.mkdir()
            try:
                video = write_video(frames, size, name, group, encoder[0], target, fingerprint)
            except subprocess.CalledProcessError as error:
                video = dict(rejected="video codec failed", detail=error.stderr.decode(errors="replace").strip()[:512])
            if "rejected" not in video and video["visible_rgb_sse"] > candidates["q75"]["visible_rgb_sse"]:
                video["rejected"] = "above Q75 squared RGB error ceiling"
            candidates["video"] = video
        for mode, candidate in candidates.items():
            if "files" in candidate:
                candidate.update(score(candidate, temp / mode, platform, sources, group))
        metric = "transfer_bytes" if platform == "web" else "stored_bytes"
        winner = min((mode for mode in candidates if "rejected" not in candidates[mode]),
                     key=lambda mode: candidates[mode][metric])
        chosen = candidates[winner]
        result = dict(winner=winner, refs=chosen["refs"], fingerprints=chosen["fingerprints"],
                      candidates={mode: {key: value for key, value in candidate.items()
                                         if key not in ("files", "refs", "fingerprints")}
                                  for mode, candidate in candidates.items()},
                      hashes={filename: hashlib.sha256((temp / winner / filename).read_bytes()).hexdigest()
                              for filename in chosen["files"]})
        for mode, candidate in candidates.items():
            if mode == winner:
                target = directory
            else:
                target = directory / mode
                target.mkdir(exist_ok=True)
            for filename in candidate.get("files", []):
                os.replace(temp / mode / filename, target / filename)
        (temp / "result.json").write_text(json.dumps(result, sort_keys=True))
        os.replace(temp / "result.json", marker)
        return directory, result, False
