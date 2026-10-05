#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a terrain catalog and compile portable texture pages / mask previews.

Usage: python3 tools/terrain_tileset.py --check
       python3 tools/terrain_tileset.py --output artifacts/terrain/tileset
Textures and masks are separate: no terrain-pair Cartesian product is baked.
"""

import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BINDINGS = ("water", "sand", "grass", "ice", "road")


def validate(document, root=ROOT):
    if document.get("version") != 1:
        raise ValueError("Unsupported terrain catalog version")
    compiled = document.get("compiled_pack", "")
    if compiled:
        path = Path(compiled)
        if (
            path.is_absolute()
            or ".." in path.parts
            or len(path.parts) < 4
            or path.parts[0] != "data"
            or path.name != "atlas.json"
        ):
            raise ValueError(
                "Compiled pack must name atlas.json in its own data subdirectory"
            )
    profiles = {}
    for p in document["profiles"]:
        if (
            not p["key"]
            or p["key"] in profiles
            or type(p["roughness_q8"]) is not int
            or not 0 <= p["roughness_q8"] <= 512
        ):
            raise ValueError("Invalid or duplicate profile")
        curves = p["contours_q12"]
        if len(curves) != 4 or any(
            len(c) != 5
            or c[0] != 0
            or c[-1] != 0
            or any(type(n) is not int or abs(n) > 256 for n in c)
            for c in curves
        ):
            raise ValueError(
                "Profiles require four five-point curves with shared zero endpoints"
            )
        profiles[p["key"]] = p
    materials = {}
    sources = {}
    for m in document["materials"]:
        if not m["key"] or m["key"] in materials or m["profile"] not in profiles:
            raise ValueError("Invalid material key or profile")
        sprite = Path(m["sprite"])
        if sprite.is_absolute() or ".." in sprite.parts or sprite.parts[0] != "data":
            raise ValueError("Sprite must be a data-relative path")
        if len(m["preview"]) != 3 or any(
            type(n) is not int or not 0 <= n <= 255 for n in m["preview"]
        ):
            raise ValueError("Invalid preview color")
        phases, stride, ticks = (
            m.get("animation_frames", 1),
            m.get("animation_stride", 0),
            m.get("animation_ticks", 1),
        )
        if (
            any(type(v) is not int for v in (phases, stride, ticks))
            or not 1 <= phases <= 256
            or not 1 <= ticks <= 2147483647
            or not 0 <= stride <= 65535
            or (phases > 1 and stride == 0)
        ):
            raise ValueError("Invalid animation")
        if not m["variants"] or sum(v["weight"] for v in m["variants"]) > 1_000_000_000:
            raise ValueError("Invalid variant weights")
        for v in m["variants"]:
            if type(v["weight"]) is not int or not 1 <= v["weight"] <= 1_000_000:
                raise ValueError("Variant weight must be a positive integer")
            if type(v["frame"]) is not int:
                raise ValueError("Frame must be an integer")
            for phase in range(phases):
                frame = v["frame"] + phase * stride
                if not 0 <= frame < 65536:
                    raise ValueError("Invalid frame")
                relative = f"{sprite.as_posix()}{frame}.png"
                source = root / relative
                with Image.open(source) as image:
                    if image.size != (32, 32):
                        raise ValueError(f"Invalid logical frame dimensions: {source}")
                sources[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
        if "backdrop" in m:
            b = m["backdrop"]
            path = Path(b["sprite"])
            first = b.get("first_frame", 0)
            count = b.get("frames", 1)
            if (
                m.get("ocean", False)
                or path.is_absolute()
                or ".." in path.parts
                or path.parts[0] != "data"
                or first < 0
                or not 1 <= count <= 256
                or first + count > 65536
                or b.get("ticks", 1) < 1
            ):
                raise ValueError("Invalid backdrop")
            for frame in range(first, first + count):
                relative = f"{path.as_posix()}{frame}.png"
                source = root / relative
                with Image.open(source) as image:
                    if image.size != (32, 32):
                        raise ValueError("Backdrop must have 32x32 logical dimensions")
                sources[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
        materials[m["key"]] = m
    if not 0 < len(materials) < 65536:
        raise ValueError("Invalid material count")
    for name in BINDINGS:
        if document["bindings"][name] not in materials:
            raise ValueError(f"Missing binding: {name}")
    if not materials[document["bindings"]["water"]].get("ocean", False):
        raise ValueError("Water requires ocean backdrop")
    pairs = set()
    for p in document.get("pair_treatments", []):
        pair = tuple(sorted((p["a"], p["b"])))
        if (
            pair in pairs
            or pair[0] == pair[1]
            or any(k not in materials for k in pair)
            or p["profile"] not in profiles
        ):
            raise ValueError("Invalid pair treatment")
        pairs.add(pair)
    return sources


def seamless_sources(document, root):
    """Same four-native-pixel shared border used by runtime source preparation."""
    result = {}
    for material in document["materials"]:
        for phase in range(material.get("animation_frames", 1)):
            names = [
                f"{material['sprite']}{v['frame']+phase*material.get('animation_stride',0)}.png"
                for v in material["variants"]
            ]
            master = Image.open(root / names[0]).convert("RGBA")
            for name in names:
                image = Image.open(root / name).convert("RGBA")
                for y in range(32):
                    for x in range(32):
                        distance = min(x, y, 31 - x, 31 - y)
                        if distance >= 4:
                            continue
                        source = master.getpixel((min(x, 31 - x), min(y, 31 - y)))
                        pixel = image.getpixel((x, y))
                        image.putpixel(
                            (x, y),
                            tuple(
                                (source[k] * (4 - distance) + pixel[k] * distance) // 4
                                for k in range(3)
                            )
                            + (pixel[3],),
                        )
                # One source can deliberately be shared by multiple materials.
                if name in result and result[name].tobytes() != image.tobytes():
                    raise ValueError(
                        "Shared frame has inconsistent variant-family borders: " + name
                    )
                result[name] = image
    return result


def padded(image, border):
    n = image.width
    page = Image.new("RGBA", (n + border * 2, n + border * 2))
    page.paste(image, (border, border))
    for i in range(border):
        page.paste(image.crop((0, 0, n, 1)), (border, i))
        page.paste(image.crop((0, n - 1, n, n)), (border, border + n + i))
    page.paste(
        page.crop((border, 0, border + 1, page.height)).resize((border, page.height)),
        (0, 0),
    )
    page.paste(
        page.crop((border + n - 1, 0, border + n, page.height)).resize(
            (border, page.height)
        ),
        (border + n, 0),
    )
    return page


def compile_tileset(document, output, root=ROOT, page_size=1024):
    sources = validate(document, root)
    if page_size < 64 or page_size > 8192 or page_size & (page_size - 1):
        raise ValueError("Page size must be a power of two from 64 to 8192")
    output.mkdir(parents=True, exist_ok=True)
    prepared = seamless_sources(document, root)
    entries, pages = [], []
    border, tile = 4, 40
    columns = page_size // tile
    filenames = sorted(sources)
    for start in range(0, len(filenames), columns * columns):
        batch = filenames[start : start + columns * columns]
        page = Image.new("RGBA", (page_size, page_size))
        page_id = len(pages)
        for i, name in enumerate(batch):
            if name in prepared:
                image = prepared[name]
            else:
                with Image.open(root / name) as im:
                    image = im.convert("RGBA")
            x, y = (i % columns) * tile, (i // columns) * tile
            page.paste(padded(image, border), (x, y))
            entries.append(
                dict(source=name, page=page_id, rect=[x + border, y + border, 32, 32])
            )
        levels = []
        for mip in range(3):
            name = f"textures-{page_id}-mip{mip}.webp"
            # Premultiplied-alpha filtering prevents dark translucent borders.
            page.convert("RGBa").resize(
                (page_size >> mip, page_size >> mip), Image.Resampling.BOX
            ).convert("RGBA").save(
                output / name, lossless=True, exact=True, quality=75, method=4
            )
            levels.append(name)
        pages.append(dict(size=[page_size, page_size], padding=border, levels=levels))
    # Shared normalized contours at native and HD resolution. These are reusable
    # edge displacement tables, not precomposed colored terrain combinations.
    masks = {}
    for profile in document["profiles"]:
        masks[profile["key"]] = {}
        for scale in (1, 4):
            size = 16 * scale
            curves = []
            for curve in profile["contours_q12"]:
                values = []
                for i in range(size + 1):
                    t = i * 4096 // size
                    segment = min(t // 1024, 3)
                    f = t - segment * 1024
                    numerator = curve[segment] * (1024 - f) + curve[segment + 1] * f
                    values.append(
                        numerator // 1024 if numerator >= 0 else -((-numerator) // 1024)
                    )
                assert values[0] == values[-1] == 0
                curves.append(values)
            masks[profile["key"]][str(scale)] = curves
    manifest = dict(
        version=1,
        pages=pages,
        frames=entries,
        masks=masks,
        sources=sources,
        catalog=document,
        catalog_sha256=hashlib.sha256(
            json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        compiler_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        provenance="Existing terrain artwork; shared periodic variant borders and deterministic reusable contour masks. No AI-generated imagery.",
    )
    (output / "atlas.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", type=Path, default=ROOT / "data/terrain/tileset.json")
    ap.add_argument("--output", type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--page-size", type=int, default=1024)
    args = ap.parse_args()
    document = json.loads(args.manifest.read_text())
    sources = validate(document)
    if args.output:
        compile_tileset(document, args.output, page_size=args.page_size)
    print(
        f'Validated {len(document["materials"])} materials and {len(sources)} texture frames'
    )


if __name__ == "__main__":
    main()
