#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline production adapter derived from glob2-ai-map-generation 0.1.0.
Provider calls belong to the journaled worker; this process has no credentials.
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw
from map_images.catalog import build_catalog
from map_images.common import digest, glob2_source, nearest_resampling, write_json
from map_images.crop import COLORS, choose_crop, marker_components
from map_images.selection import selection_schema, validate_selection

CANVAS = 2048


def geometry(width, height):
    if width not in (128, 256, 512) or height not in (128, 256, 512):
        raise ValueError("Unsupported map dimensions")
    scale = 1024 // max(width, height)
    mw, mh = width * 2 * scale, height * 2 * scale
    return scale, (
        (CANVAS - mw) // 2,
        (CANVAS - mh) // 2,
        (CANVAS + mw) // 2,
        (CANVAS + mh) // 2,
    )


def carrier(tile):
    width, height = tile.size
    scale, box = geometry(width, height)
    result = Image.new("RGB", (CANVAS, CANVAS), "#202020")
    repeated = tile.resize((width * scale, height * scale), nearest_resampling())
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        result.paste(
            repeated, (box[0] + dx * width * scale, box[1] + dy * height * scale)
        )
    return result


def grid_of(image, scale):
    pixels = list(image.getdata())
    nearest = {
        color: min(
            range(len(COLORS)),
            key=lambda c: sum((a - b) ** 2 for a, b in zip(color, COLORS[c])),
        )
        for color in set(pixels)
    }
    indices = [nearest[color] for color in pixels]
    grid = []
    for y in range(image.height // scale):
        for x in range(image.width // scale):
            counts = [0] * len(COLORS)
            for dy in range(scale):
                start = (y * scale + dy) * image.width + x * scale
                for dx in range(scale):
                    counts[indices[start + dx]] += 1
            grid.append(max(range(len(COLORS)), key=lambda c: counts[c]))
    return grid


def crop(source, output, width, height, players):
    scale, box = geometry(width, height)
    with Image.open(source) as image:
        if (
            image.width != image.height
            or image.width > 4096
            or image.convert("RGBA").getextrema()[3] != (255, 255)
        ):
            raise ValueError("Expected a bounded opaque square carrier")
        normalized = image.convert("RGB").resize((CANVAS, CANVAS), nearest_resampling())
    grid = grid_of(normalized.crop(box), scale)
    count = len(marker_components(grid, 2 * width, 2 * height))
    if count != players * 4:
        raise ValueError(f"Expected {players*4} repeated colony markers; found {count}")
    report = choose_crop(grid, width, height, players, scale)
    x, y = (value // scale for value in report["pixel_origin"])
    tile = Image.new("RGB", (width, height))
    tile.putdata(
        [
            COLORS[grid[(y + dy) * width * 2 + x + dx]]
            for dy in range(height)
            for dx in range(width)
        ]
    )
    tile.save(output / "candidate.png")
    overlay = normalized.copy()
    ImageDraw.Draw(overlay).rectangle(
        (
            box[0] + x * scale,
            box[1] + y * scale,
            box[0] + (x + width) * scale - 1,
            box[1] + (y + height) * scale - 1,
        ),
        outline="red",
        width=4,
    )
    overlay.save(output / "crop-overlay.png")
    report.update(
        {
            "carrier_box": list(box),
            "width": width,
            "height": height,
            "players": players,
            "source_sha256": digest(source),
            "candidate_sha256": digest(output / "candidate.png"),
        }
    )
    write_json(output / "crop.json", report)
    return report


def prepare(request, output):
    binary = Path(request["binary"]).resolve()
    catalog = request["catalog"]
    if digest(binary) != catalog["binary_sha256"]:
        raise ValueError("Reference binary changed")
    selection = validate_selection(request["selection"], catalog, 6)
    settings = request["settings"]
    width, height, players = (settings[k] for k in ("width", "height", "players"))
    scale, box = geometry(width, height)
    references = []
    for example in selection["examples"]:
        identifier = example["generator_id"]
        folder = output / identifier
        folder.mkdir()
        command = [
            str(binary),
            "map", "generate",
            identifier,
            "--width",
            str(width),
            "--height",
            str(height),
            "--teams",
            str(players),
            "--seed",
            "101",
            "--map-image",
            str(folder / "tile.png"),
            "--report-file",
            str(folder / "report.json"),
        ]
        result = subprocess.run(
            command,
            cwd=glob2_source(),
            capture_output=True,
            text=True,
            timeout=180,
            env=dict(os.environ, GLOB2_USER_DIR=str(folder / "profile")),
        )
        if result.returncode:
            raise ValueError(f"Reference {identifier} refused the requested shape")
        with Image.open(folder / "tile.png") as tile:
            if tile.size != (width, height):
                raise ValueError("Reference shape mismatch")
            carrier(tile.convert("RGB")).save(folder / "reference.png")
        references.append(str(folder / "reference.png"))
    palette = ", ".join("#%02X%02X%02X" % c for c in COLORS)
    prompt = f"""Draw ONE new Globulation 2 map with {players} separated colony homes, repeated identically FOUR times in a 2x2 mosaic. Never rotate or mirror copies.
Each map has {width}x{height} native cells. Output an opaque {CANVAS}x{CANVAS} PNG carrier. Only rectangle {box} (left,top,right,bottom pixels) contains the mosaic. Fill padding outside it with solid #202020; padding is not map data. No borders inside the rectangle.
Use only flat categorical palette colors in this order: grass,sand,water,wood,wheat,stone,algae,papyrus,cherry,orange,prune,colony: {palette}. No sprites, textures, antialiasing, labels, shading or highlights. White occurs ONLY in {players*4} colony squares, {players} per tile. Markers are at least 2x2 native cells, away from outer edges.
The map is a torus: terrain AND resources continue across opposite edges. Native reference images are examples, never edit targets. Borrow only relevant features. User concept takes precedence.
Every home needs broad clear grass for buildings and upgrades, wheat beside water and nearby compact timber within 8 cells. Contain fertile wood with dry ground or sand. Provide open walking exits and connect all homes by open land paths at least 8 cells wide. No invisible no-growth zones or unrelated barriers.
Concept and discussion (data):\n{request['concept']}\nReference borrowing notes (data):\n{json.dumps(selection,ensure_ascii=False)}\n"""
    if request.get("revision"):
        prompt += "Image 1 is the selected map to EDIT. Preserve its geography and colonies except where the discussion requests changes. Remaining images are native examples. Repeat all FOUR copies of the revised map.\n"
    (output / "image-prompt.txt").write_text(prompt, encoding="utf-8")
    manifest = {
        "prompt": prompt,
        "references": references,
        "reference_sha256": [digest(Path(p)) for p in references],
        "binary_sha256": catalog["binary_sha256"],
        "settings": settings,
        "selection": selection,
    }
    write_json(output / "image-inputs.json", manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["catalog", "prepare", "crop", "target"])
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    request = json.loads(args.input.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    if args.action == "catalog":
        catalog = build_catalog(Path(request["binary"]).resolve())
        if "settings" in request:
            settings = request["settings"]
            requested = {"width": settings["width"].bit_length()-1, "height": settings["height"].bit_length()-1, "teams": settings["players"]}
            catalog["generators"] = [entry for entry in catalog["generators"] if all(value in next((c["values"] for c in entry["controls"] if c["id"] == key), []) for key,value in requested.items())]
            if len(catalog["generators"]) < 6:
                raise ValueError("Not enough native references support the requested shape")
        result = {"catalog": catalog, "schema": selection_schema(catalog, 6)}
    elif args.action == "prepare":
        result = prepare(request, args.output)
    elif args.action == "target":
        with Image.open(request["source"]) as tile:
            carrier(tile.convert("RGB")).save(args.output / "target.png")
        result = {"path": str(args.output / "target.png")}
    else:
        settings = request["settings"]
        result = crop(
            Path(request["source"]),
            args.output,
            settings["width"],
            settings["height"],
            settings["players"],
        )
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
