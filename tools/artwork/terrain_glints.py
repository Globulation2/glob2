#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise the glints that mark fertile ground.

Every terrain group carries one shared visual mark; fertile ground (loam, moss,
spring meadow) twinkles. The glints are a flat animated overlay drawn over the
ground and under raised decor (the `overlay` block of the material's catalog file),
not an animated ground texture: fertile land covers large areas, and an
overlay never invalidates the cached terrain pages.

Each layout is one mote at its own spot inside a cell, drawn at four
brightness levels: rising, peak (a four-pointed star with short diagonal
rays), falling and fading. A layout's cycle lights those four frames and then
stays dark, so the runtime draws nothing for most steps. The runtime picks the
glinting cells, their layout and their starting step by presentation hashing;
the timing and density live in the catalog and can be tuned without
regenerating frames.

Usage (pinned encoder interpreter, as for terrain_synth.py):

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/terrain_glints.py                  # write frames + HD + provenance
    "$ENC" tools/artwork/terrain_glints.py --write-catalog  # add the overlay blocks
    "$ENC" tools/artwork/terrain_glints.py --check
    "$ENC" tools/artwork/terrain_glints.py --sheet artifacts/terrain/glints.png --gif artifacts/terrain/glints.gif

Frames are 64x64 native, centred on the 32x32 cell like decor so rays may
reach into neighbours. They are rendered at 256x256 with integer arithmetic
only, so every platform produces identical pixels, then box-filtered.
"""
import argparse
import functools
import hashlib
import json
import sys
from pathlib import Path

import PIL
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from material_tiles import ROOT, fnv1a32, pixel_sha256  # noqa: E402
import terrain_catalog  # noqa: E402

NATIVE = 64           # frame size; the cell is the central 32x32, as for decor
SCALE = 4
S = NATIVE * SCALE
CELL = 32 * SCALE
PAD = (S - CELL) // 2
PREFIX = "data/gfx/terrain-glint"            # runtime path, as the catalog names it
FILE_PREFIX = "content/" + PREFIX           # where the checkout stores it
HD_PREFIX = "content/data/highres/v1/terrain-glint"
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural terrain glints v1"
FERTILE = ["loam", "moss", "spring_meadow"]
LAYOUTS = 8
LEVELS = 4            # rising, peak, falling, fading
CYCLE = 12            # steps per cycle; the rest after the lit levels are dark
TICKS = 5             # scene ticks per step (30 per second): a 2 s cycle
DENSITY_Q8 = 64       # a quarter of fertile cells glint at all
CORE = (255, 250, 222)
RAY = (255, 226, 150)
# Per level: core radius, ray length, diagonal ray length, peak alpha (all HD).
LEVEL_SHAPES = [(8, 26, 0, 120), (13, 56, 22, 180), (10, 40, 12, 150), (6, 18, 0, 84)]


def layout_centre(layout):
    """A deterministic spot kept far enough inside the cell for the longest ray."""
    h = fnv1a32(f"glint:{layout}")
    margin = 24
    span = CELL - 2 * margin
    return PAD + margin + h % span, PAD + margin + (h >> 12) % span


def render_hd(layout, level):
    cx, cy = layout_centre(layout)
    core, ray, diagonal, peak = LEVEL_SHAPES[level]
    pixels = []
    for y in range(S):
        dy = y - cy
        for x in range(S):
            dx = x - cx
            ax, ay = abs(dx), abs(dy)
            alpha = 0
            # Soft round core: falls to zero at the radius.
            d2 = dx * dx + dy * dy
            r2 = core * core
            if d2 < r2:
                alpha = peak * (r2 - d2) // r2
            # Axis rays: two pixels wide, tapering linearly to the tip.
            if ay <= 1 and ax < ray:
                alpha = max(alpha, peak * (ray - ax) // ray * (2 - ay) // 2)
            if ax <= 1 and ay < ray:
                alpha = max(alpha, peak * (ray - ay) // ray * (2 - ax) // 2)
            # Short diagonal rays at the brighter levels.
            if diagonal and abs(ax - ay) <= 1 and ax < diagonal:
                alpha = max(alpha, peak * (diagonal - ax) // diagonal * 2 // 3)
            colour = CORE if d2 < r2 else RAY
            pixels.append((*colour, alpha))
    image = Image.new("RGBA", (S, S))
    image.putdata(pixels)
    return image


def frame_number(layout, level):
    return layout * LEVELS + level


def synthesize():
    frames = {}
    for layout in range(LAYOUTS):
        for level in range(LEVELS):
            hd = render_hd(layout, level)
            frames[frame_number(layout, level)] = (hd.resize((NATIVE, NATIVE), Image.Resampling.BOX), hd)
    return frames


def cycles():
    dark = [-1] * (CYCLE - LEVELS)
    return [[frame_number(layout, level) for level in range(LEVELS)] + dark for layout in range(LAYOUTS)]


def overlay_block():
    return {"sprite": PREFIX, "cycles": cycles(), "ticks": TICKS, "density_q8": DENSITY_Q8}


def generator_sha():
    return hashlib.sha256(Path(__file__).read_bytes()).hexdigest()


def provenance_path(root):
    return root / "content-src/gfx/terrain-glint/provenance.json"


def write(frames, root):
    from highres_pack import register_frames, source_record

    hashes, hd_hashes, hd_frames = {}, {}, []
    for n, (native, hd) in sorted(frames.items()):
        relative = f"{FILE_PREFIX}{n}.png"
        (root / relative).parent.mkdir(parents=True, exist_ok=True)
        native.save(root / relative, optimize=False)
        hashes[relative] = pixel_sha256(native)
        hd_hashes[f"{HD_PREFIX}{n}.png"] = pixel_sha256(hd)
        hd_frames.append({"id": f"terrain-glint{n}", "image": hd, "recipe": HD_RECIPE,
                          "sources": [source_record(Path(__file__).resolve(), root)]})
    register_frames(hd_frames, HD_CATEGORY, root)
    document = {
        "generator": "tools/artwork/terrain_glints.py",
        "method": "procedural",
        "generator_sha256": generator_sha(),
        "description": "Twinkling motes over fertile ground: one mote per layout at four brightness levels.",
        "seed": 'fnv1a32("glint:<layout>")',
        "native_size": NATIVE,
        "render_size": S,
        "layouts": LAYOUTS,
        "levels": LEVELS,
        "pillow": PIL.__version__,
        "runtime_sha256": hashes,
        "highres_sha256": hd_hashes,
    }
    path = provenance_path(root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")


def check(frames, root):
    path = provenance_path(root)
    if not path.exists():
        return ["missing content-src/gfx/terrain-glint/provenance.json"]
    provenance = json.loads(path.read_text())
    problems = []
    if provenance.get("generator_sha256") != generator_sha():
        problems.append("generator changed; re-run terrain_glints.py")
    for n, (native, hd) in sorted(frames.items()):
        for relative, image, table in ((f"{FILE_PREFIX}{n}.png", native, provenance["runtime_sha256"]),
                                       (f"{HD_PREFIX}{n}.png", hd, provenance["highres_sha256"])):
            target = root / relative
            if not target.exists():
                problems.append(f"{relative}: missing")
                continue
            with Image.open(target) as committed:
                digest = pixel_sha256(committed)
            if digest != pixel_sha256(image) or table.get(relative) != digest:
                problems.append(f"{relative}: differs from a fresh synthesis")
    for material in catalog_document(root)["materials"]:
        if material["key"] in FERTILE and material.get("overlay") != overlay_block():
            problems.append(f"{material['key']}: overlay block out of date; run --write-catalog")
    return problems


def write_catalog(root):
    """Add or refresh the overlay block of every fertile material in the catalog."""
    document = terrain_catalog.load(root / "content")
    found = set()
    for material in document["materials"]:
        if material["key"] in FERTILE:
            material["overlay"] = overlay_block()
            found.add(material["key"])
    missing = set(FERTILE) - found
    if missing:
        raise SystemExit(f"the terrain catalog has no material for {sorted(missing)}")
    terrain_catalog.save(root / "content", document)


@functools.lru_cache(maxsize=None)
def ground_tile(root, key, x, y):
    """The material's tile at a block position, as the runtime lays its 8x8 grid."""
    material = next(m for m in catalog_document(root)["materials"] if m["key"] == key)
    grid = material.get("variant_grid", 1)
    frame = material["variants"][(y % grid) * grid + x % grid]["frame"]
    with Image.open(root / "content" / f"{material['sprite']}{frame}.png") as tile:
        return tile.convert("RGBA")


@functools.lru_cache(maxsize=None)
def catalog_document(root):
    return terrain_catalog.load(root / "content")


def field(root, frames, key, step, size=8, scale=2):
    """A size x size patch of the material at one step, glints chosen as the runtime would."""
    patch = Image.new("RGBA", (size * 32, size * 32))
    for y in range(size):
        for x in range(size):
            patch.paste(ground_tile(root, key, x, y), (x * 32, y * 32))
    for y in range(size):
        for x in range(size):
            h = fnv1a32(f"{key}:{x}:{y}")
            if h & 255 >= DENSITY_Q8:
                continue
            cycle = cycles()[(h >> 8) % LAYOUTS]
            frame = cycle[(step + (h >> 16)) % CYCLE]
            if frame >= 0:
                patch.alpha_composite(frames[frame][0], (x * 32 - PAD // SCALE, y * 32 - PAD // SCALE))
    return patch.resize((patch.width * scale, patch.height * scale), Image.Resampling.NEAREST)


def sheet(root, frames):
    """Each fertile ground over four steps, plus every frame on a dark tile."""
    steps = [0, 1, 2, 3]
    panels = [[field(root, frames, key, step) for step in steps] for key in FERTILE]
    w, h = panels[0][0].size
    strip = LAYOUTS * LEVELS
    rows = (strip + 15) // 16
    out = Image.new("RGBA", (max(w * len(steps), 16 * 66), (h + 20) * len(FERTILE) + 8 + rows * 66), (30, 32, 30, 255))
    labels = ImageDraw.Draw(out)
    for row, key in enumerate(FERTILE):
        for col, step in enumerate(steps):
            out.paste(panels[row][col], (col * w, row * (h + 20) + 20))
            labels.text((col * w + 6, row * (h + 20) + 4), f"{key}, step {step}", fill="white")
    y0 = len(FERTILE) * (h + 20) + 8
    for n in range(strip):
        tile = Image.new("RGBA", (64, 64), (40, 52, 36, 255))
        tile.alpha_composite(frames[n][0])
        out.paste(tile, ((n % 16) * 66, y0 + (n // 16) * 66))
    return out


def animation(root, frames):
    """An animated GIF of the three fertile fields side by side, one step per frame."""
    images = []
    for step in range(CYCLE * 2):
        row = [field(root, frames, key, step) for key in FERTILE]
        out = Image.new("RGB", (sum(p.width for p in row), row[0].height))
        for i, patch in enumerate(row):
            out.paste(patch.convert("RGB"), (i * patch.width, 0))
        images.append(out)
    return images


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify committed frames, provenance and overlay blocks")
    parser.add_argument("--write-catalog", action="store_true", help="add the overlay block to each fertile material")
    parser.add_argument("--sheet", type=Path, help="write a review sheet instead of assets")
    parser.add_argument("--gif", type=Path, help="write an animated review GIF instead of assets")
    args = parser.parse_args(argv)
    if args.write_catalog:
        write_catalog(ROOT)
        return 0
    frames = synthesize()
    if args.check:
        problems = check(frames, ROOT)
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1 if problems else 0
    if args.sheet or args.gif:
        if args.sheet:
            args.sheet.parent.mkdir(parents=True, exist_ok=True)
            sheet(ROOT, frames).save(args.sheet)
        if args.gif:
            args.gif.parent.mkdir(parents=True, exist_ok=True)
            images = animation(ROOT, frames)
            images[0].save(args.gif, save_all=True, append_images=images[1:], duration=TICKS * 1000 // 30, loop=0)
        return 0
    write(frames, ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
