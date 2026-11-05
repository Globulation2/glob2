#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate a terrain catalog and compile portable texture pages / mask previews.

Usage: python3 tools/terrain_tileset.py --check
       python3 tools/terrain_tileset.py --output artifacts/terrain/tileset
       python3 tools/terrain_tileset.py --border-table [--compare OLD.json]
Textures and masks are separate: no terrain-pair Cartesian product is baked.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_catalog  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
LEGACY_BINDINGS = ("water", "sand", "grass", "ice", "road")
BUILTIN_NAMES = ROOT / "tools/terrain_builtin_names.json"


def assets(root):
    """The runtime tree, where catalog sprite paths such as data/gfx/... resolve."""
    return Path(root) / "content"


def required_bindings(root=ROOT):
    """The five legacy names plus every paintable built-in presentation name.

    tools/terrain_builtin_names.json lists the engine's paintable built-in
    types; the engine's TerrainMaterials test compares this list with its tables.
    """
    names = list(LEGACY_BINDINGS)
    path = Path(root) / BUILTIN_NAMES.relative_to(ROOT)
    if path.is_file():
        for name in json.loads(path.read_text()):
            if name not in names:
                names.append(name)
    return tuple(names)


BINDINGS = required_bindings()


def material_provenance(document, root=ROOT):
    """Per-material provenance summaries from content-src/gfx/<key>/provenance.json.

    Materials without a provenance file are existing terrain artwork with
    shared periodic variant borders and no AI-generated imagery.
    """
    result = {}
    for material in document["materials"]:
        path = Path(root) / "content-src/gfx" / material["key"] / "provenance.json"
        if not path.is_file():
            result[material["key"]] = dict(method="existing", source="Existing terrain artwork")
            continue
        record = json.loads(path.read_text())
        summary = dict(method=record.get("method", "unknown"), source=path.relative_to(root).as_posix())
        for field in ("generator", "generator_sha256", "source_sha256", "pillow", "replaces"):
            if field in record:
                summary[field] = record[field]
        result[material["key"]] = summary
    return result


def pixel_fingerprint(image):
    """FNV-1a of decoded RGBA bytes; cache identity, not a security signature.

    The runtime computes the same fixed-width hash of the loaded native surface.
    Exporters must fingerprint their decoded runtime image after any lossy encoding.
    """
    value = 14695981039346656037
    for byte in image.convert("RGBA").tobytes():
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def integer(value, minimum, maximum, field):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"{field} must be an integer in {minimum}..{maximum}")
    return value


def data_path(value, field):
    if not isinstance(value, str) or not value.startswith("data/") or "\\" in value or ":" in value:
        raise ValueError(f"{field} must be a data-relative path")
    path = Path(value)
    if ".." in path.parts or len(path.parts) < 2 or value != path.as_posix() or "\0" in value:
        raise ValueError(f"{field} must be a data-relative path")
    return path


def validate(document, root=ROOT):
    """Validate the runtime schema and all native source images before packaging."""
    try:
        return _validate(document, root)
    except (KeyError, TypeError, IndexError, AttributeError) as error:
        raise ValueError(f"Malformed terrain catalog: {error}") from error


def _validate(document, root):
    if type(document.get("version")) is not int or document["version"] not in (1, 2, 3, 4):
        raise ValueError("Unsupported terrain catalog version")
    version = document["version"]
    if version == 1 and "boundary_warp_q8" in document:
        raise ValueError("Boundary warp requires catalog version 2")
    warp = document.get("boundary_warp_q8", [0, 0, 0])
    if not isinstance(warp, list) or len(warp) != 3:
        raise ValueError("Boundary warp requires three amplitudes")
    for amplitude, maximum in zip(warp, (1024, 384, 128)):
        integer(amplitude, 0, maximum, "Boundary warp amplitude")
    compiled = document.get("compiled_pack", "")
    if not isinstance(compiled, str):
        raise ValueError("Compiled pack path must be a string")
    if compiled:
        path = data_path(compiled, "Compiled pack")
        if (
            len(path.parts) < 4
            or path.name != "atlas.json"
        ):
            raise ValueError(
                "Compiled pack must name atlas.json in its own data subdirectory"
            )
    if not isinstance(document["profiles"], list) or not document["profiles"]:
        raise ValueError("Profiles must be a nonempty array")
    if not isinstance(document["materials"], list) or not document["materials"]:
        raise ValueError("Materials must be a nonempty array")
    classes = border_classes(document)
    profiles = {}
    for p in document["profiles"]:
        shape = p.get("shape", "patch")
        if shape not in ("patch", "contextual", "smooth"):
            raise ValueError("Unknown boundary shape")
        smooth = shape == "smooth"
        if (
            not isinstance(p["key"], str)
            or not p["key"]
            or p["key"] in profiles
            or type(p.get("roughness_q8", 0 if smooth else None)) is not int
            or not 0 <= p.get("roughness_q8", 0) <= 512
        ):
            raise ValueError("Invalid or duplicate profile")
        if version < 3 and shape != "patch":
            raise ValueError("Contextual shape requires catalog version 3")
        if version < 4 and smooth:
            raise ValueError("Smooth shape requires catalog version 4")
        for field in ("precedence", "coast"):
            if version < 4 and field in p:
                raise ValueError(f"{field} requires catalog version 4")
        integer(p.get("precedence", 0), 0, 255, "Profile precedence")
        for field in ("wiggle_q8", "wiggle_period", "squareness_q8"):
            if not smooth and field in p:
                raise ValueError(f"{field} requires the smooth shape")
        integer(p.get("wiggle_q8", 0), 0, 2048, "Smooth wiggle")
        integer(p.get("squareness_q8", 0), 0, 256, "Smooth squareness")
        if p.get("wiggle_period", 64) not in (32, 64, 128, 256):
            raise ValueError("Smooth wiggle period must be 32, 64, 128 or 256 pixels")
        if version == 1 and "feather_q8" in p:
            raise ValueError("Boundary feather requires catalog version 2")
        integer(p.get("feather_q8", 256), 128, 512, "Boundary feather")
        for field in ("amplitude_q8", "speckle_q8", "bridge_q8"):
            if version < 3 and field in p:
                raise ValueError(f"{field} requires catalog version 3")
            integer(p.get(field, 0), 0, 1024, field)
        if smooth and "contours_q12" not in p:
            profiles[p["key"]] = p
            continue
        curves = p["contours_q12"]
        limit = {1: 256, 2: 512}.get(version, 1024)
        if not isinstance(curves, list) or (
            len(curves) != 4 if version < 3 else not 4 <= len(curves) <= 64
        ) or any(
            not isinstance(c, list) or len(c) not in ((5,) if version == 1 else (5, 9, 17, 33))
            or c[0] != 0
            or c[-1] != 0
            or any(type(n) is not int or abs(n) > limit for n in c)
            for c in curves
        ):
            raise ValueError(
                "Profiles require four curves (four to sixty-four in version 3) of 5, 9, 17 "
                "or 33 points with shared zero endpoints"
            )
        profiles[p["key"]] = p
    for p in profiles.values():
        if "coast" in p and (p["coast"] not in profiles or p["coast"] == p["key"]):
            raise ValueError("Profile coast must name another profile")
    aliases = document.get("bundle_profiles", {})
    if aliases and version < 4:
        raise ValueError("Bundle profiles require catalog version 4")
    if not isinstance(aliases, dict) or any(
        not isinstance(a, dict) or a.get("profile") not in profiles
        or a.get("class") not in classes
        for key, a in aliases.items()
    ):
        raise ValueError("Bundle profiles must name a profile and a border class")
    materials = {}
    sources = {}
    decor_sprites = set()
    overlay_sprites = set()
    for m in document["materials"]:
        if (
            not isinstance(m["key"], str)
            or not m["key"]
            or m["key"] in materials
            or m["profile"] not in profiles
        ):
            raise ValueError("Invalid material key or profile")
        if version >= 4 and m.get("class") not in classes:
            raise ValueError("Version 4 materials need a known border class")
        if version < 4 and "class" in m:
            raise ValueError("Border classes require catalog version 4")
        sprite = data_path(m["sprite"], "Sprite")
        if m.get("edges", "blend") not in ("blend", "periodic"):
            raise ValueError("Material edges must be 'blend' or 'periodic'")
        if "ocean" in m or "backdrop" in m:
            # Water is an ordinary animated tile material now.
            raise ValueError("Ocean and backdrop materials are no longer supported")
        if not isinstance(m["preview"], list) or len(m["preview"]) != 3 or any(
            type(n) is not int or not 0 <= n <= 255 for n in m["preview"]
        ):
            raise ValueError("Invalid preview color")
        minimap = m.get("minimap", m["preview"])
        if not isinstance(minimap, list) or len(minimap) != 3 or any(
            type(n) is not int or not 0 <= n <= 255 for n in minimap
        ):
            raise ValueError("Invalid minimap color")
        if "seam" in m:
            if version < 3:
                raise ValueError("Seam treatments require catalog version 3")
            seam = m["seam"]
            if not isinstance(seam, dict):
                raise ValueError("Seam must be an object")
            integer(seam.get("height", 0), 0, 255, "Seam height")
            integer(seam.get("cast_q8", 0), 0, 256, "Seam cast_q8")
            integer(seam.get("cast_width_q8", 0), 0, 2048, "Seam cast_width_q8")
            integer(seam.get("fringe_q8", 0), 0, 256, "Seam fringe_q8")
            integer(seam.get("fringe_width_q8", 0), 0, 2048, "Seam fringe_width_q8")
            fringe = seam.get("fringe", [255, 255, 255])
            if not isinstance(fringe, list) or len(fringe) != 3 or any(
                type(n) is not int or not 0 <= n <= 255 for n in fringe
            ):
                raise ValueError("Invalid seam fringe color")
        if "surf" in m:
            if version < 3:
                raise ValueError("Surf requires catalog version 3")
            surf = m["surf"]
            if not isinstance(surf, dict):
                raise ValueError("Surf must be an object")
            integer(surf.get("period"), 1, 65535, "Surf period")
            integer(surf.get("ticks", 1), 1, surf["period"], "Surf ticks")
            for field in ("shallow_q8", "wet_q8", "foam_q8"):
                integer(surf.get(field, 0), 0, 256, f"Surf {field}")
            for field in ("reach_q8", "shallow_width_q8", "wet_width_q8", "foam_width_q8"):
                integer(surf.get(field, 0), 0, 2048, f"Surf {field}")
            if surf.get("reach_q8", 0) + surf.get("foam_width_q8", 0) > 2048:
                raise ValueError("Surf must end within eight pixels")
            for field in ("shallow", "foam"):
                color = surf.get(field, [0, 0, 0])
                if not isinstance(color, list) or len(color) != 3 or any(
                    type(n) is not int or not 0 <= n <= 255 for n in color
                ):
                    raise ValueError(f"Invalid surf {field} color")
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
        if "animation_blend_steps" in m:
            steps = m["animation_blend_steps"]
            if phases == 1 or type(steps) is not int or not 1 <= steps <= ticks:
                raise ValueError("Invalid animation blend steps")
        if (
            not isinstance(m["variants"], list)
            or not m["variants"]
            or sum(v["weight"] for v in m["variants"]) > 1_000_000_000
        ):
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
                source = assets(root) / relative
                with Image.open(source) as image:
                    if image.size != (32, 32):
                        raise ValueError(f"Invalid logical frame dimensions: {source}")
                sources[relative] = hashlib.sha256(source.read_bytes()).hexdigest()
        if "variant_block_group" in m:
            group = m["variant_block_group"]
            if "variant_grid" not in m or not isinstance(group, str) or not 1 <= len(group.encode("utf-8")) <= 64:
                raise ValueError("variant_block_group requires a grid and a short nonempty key")
        blocks = integer(m.get("variant_blocks", 1), 1, 16, "Variant blocks")
        if "variant_blocks" in m and "variant_grid" not in m:
            raise ValueError("variant_blocks requires variant_grid")
        if "variant_grid" in m:
            grid = integer(m["variant_grid"], 1, 16, "Variant grid")
            if grid & (grid - 1) or m.get("edges") != "periodic" or len(m["variants"]) != grid * grid * blocks:
                raise ValueError("variant_grid needs a power of two, periodic edges and grid*grid*blocks variants")
        if "stones" in m:
            # Whole-stone path edges: the stone map spans one variant block.
            if version < 4 or "variant_grid" not in m:
                raise ValueError("stones require catalog version 4 and a variant_grid")
            stones = assets(root) / data_path(m["stones"], "Stone map")
            if not stones.is_file():
                raise ValueError(f"Missing stone map: {stones}")
            with Image.open(stones) as image:
                side = m["variant_grid"] * 128
                if image.size != (2 * side, side):
                    raise ValueError(f"Stone map must be two {side}x{side} blocks: {stones}")
        if "decor" in m:
            decor = m["decor"]
            decor_sprite = data_path(decor["sprite"], "Decor sprite")
            for key in ("full", "edge", "connected", "connected_interior", "connected_foreign"):
                if key.startswith("connected") and key not in decor:
                    continue
                frames = decor.get(key)
                limit = 50 * 16 if key == "connected_foreign" else 256
                if not isinstance(frames, list) or not frames or len(frames) > limit:
                    raise ValueError("Decor frames must be a non-empty array")
                if key.startswith("connected") and len(frames) % 16:
                    raise ValueError("Connected decor needs equal variants for all 16 masks")
                for frame in frames:
                    integer(frame, 0, 65535, "Decor frame")
                    source = assets(root) / f"{decor_sprite.as_posix()}{frame}.png"
                    with Image.open(source) as image:
                        if image.width > 128 or image.height > 128:
                            raise ValueError(f"Decor frame larger than 128x128: {source}")
            if "connected_corners" in decor:
                if not isinstance(decor["connected_corners"], bool):
                    raise ValueError("connected_corners must be a boolean")
                if decor["connected_corners"] and len(decor.get("connected", [])) != 256:
                    raise ValueError("Corner-tiled connected decor needs 16 variants per mask")
            if "connected_interior" in decor and not decor.get("connected_corners"):
                raise ValueError("connected_interior needs connected_corners")
            if "connected_open" in decor:
                if not isinstance(decor["connected_open"], bool):
                    raise ValueError("connected_open must be a boolean")
                if decor["connected_open"] and not decor.get("connected"):
                    raise ValueError("connected_open needs connected frames")
            if "connected_tiers" in decor:
                if not isinstance(decor["connected_tiers"], bool):
                    raise ValueError("connected_tiers must be a boolean")
                if decor["connected_tiers"] and not decor.get("connected_open"):
                    raise ValueError("connected_tiers needs connected_open")
            if "connected_foreign" in decor:
                frames = decor["connected_foreign"]
                if (not isinstance(frames, list) or len(frames) != 50 * 16 or
                        not all(isinstance(f, int) and not isinstance(f, bool) and 0 <= f <= 65535 for f in frames)):
                    raise ValueError("connected_foreign needs 16 frames for each of the 50 own/foreign corner codes")
                if not (decor.get("connected_corners") and decor.get("connected_open")):
                    raise ValueError("connected_foreign needs connected_corners and connected_open")
            decor_sprites.add(decor["sprite"])
        if "overlay" in m:
            overlay = m["overlay"]
            overlay_sprite = data_path(overlay["sprite"], "Overlay sprite")
            cycles = overlay.get("cycles")
            if not isinstance(cycles, list) or not 1 <= len(cycles) <= 64:
                raise ValueError("Overlay cycles must be a non-empty array of at most 64 cycles")
            for cycle in cycles:
                if not isinstance(cycle, list) or not 1 <= len(cycle) <= 64:
                    raise ValueError("Overlay cycle must be a non-empty array of at most 64 steps")
                if all(integer(frame, -1, 65535, "Overlay frame") < 0 for frame in cycle):
                    raise ValueError("Overlay cycle must draw at least one frame")
                for frame in cycle:
                    if frame < 0:
                        continue
                    source = assets(root) / f"{overlay_sprite.as_posix()}{frame}.png"
                    with Image.open(source) as image:
                        if image.width > 64 or image.height > 64:
                            raise ValueError(f"Overlay frame larger than 64x64: {source}")
            integer(overlay.get("ticks", 1), 1, 65535, "Overlay ticks")
            integer(overlay.get("density_q8", 256), 1, 256, "Overlay density")
            overlay_sprites.add(overlay["sprite"])
        materials[m["key"]] = m
    if len(decor_sprites) > 1:
        raise ValueError("All decor blocks must share one sprite")
    if len(overlay_sprites) > 1:
        raise ValueError("All overlay blocks must share one sprite")
    if not 0 < len(materials) < 65536:
        raise ValueError("Invalid material count")
    if not isinstance(document["bindings"], dict):
        raise ValueError("Bindings must be an object")
    for name, material in document["bindings"].items():
        if material not in materials:
            raise ValueError(f"Unknown material in binding: {name}")
    for name in BINDINGS:
        if document["bindings"][name] not in materials:
            raise ValueError(f"Missing binding: {name}")
    pairs = set()
    treatments = document.get("pair_treatments", [])
    if not isinstance(treatments, list):
        raise ValueError("Pair treatments must be an array")
    for p in treatments:
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


def border_classes(document):
    """Border classes in dominance order, strongest first (version 4)."""
    if document["version"] < 4:
        if "border_classes" in document:
            raise ValueError("Border classes require catalog version 4")
        return {}
    classes = document.get("border_classes")
    if not isinstance(classes, list) or not classes:
        raise ValueError("Version 4 catalogs need a nonempty border_classes array")
    result = {}
    for rank, entry in enumerate(classes):
        if (
            not isinstance(entry, dict)
            or not isinstance(entry.get("key"), str)
            or not entry["key"]
            or entry["key"] in result
            or type(entry.get("shoreline", False)) is not bool
            or set(entry) - {"key", "shoreline"}
        ):
            raise ValueError("Invalid or duplicate border class")
        result[entry["key"]] = dict(rank=rank, shoreline=entry.get("shoreline", False))
    return result


def border_profile(document, a, b):
    """Profile key drawing the contact between materials a and b.

    Mirrors Catalog::profileFor in src/render/terrain/TerrainBorderRules.cpp.
    """
    materials = {m["key"]: m for m in document["materials"]}
    profiles = {p["key"]: p for p in document["profiles"]}
    for p in document.get("pair_treatments", []):
        if {p["a"], p["b"]} == {a, b}:
            return p["profile"]
    ma, mb = materials[a], materials[b]
    pa, pb = profiles[ma["profile"]], profiles[mb["profile"]]
    if document["version"] < 4:
        ra, rb = pa.get("roughness_q8", 0), pb.get("roughness_q8", 0)
        if ra != rb:
            return pa["key"] if ra > rb else pb["key"]
        return pa["key"] if a < b else pb["key"]
    classes = border_classes(document)
    ca, cb = classes[ma["class"]], classes[mb["class"]]
    if ca["rank"] == cb["rank"]:
        qa, qb = pa.get("precedence", 0), pb.get("precedence", 0)
        if qa != qb:
            return pa["key"] if qa > qb else pb["key"]
        return min(pa["key"], pb["key"])
    strong, weak = (pa, cb) if ca["rank"] < cb["rank"] else (pb, ca)
    if weak["shoreline"] and "coast" in strong:
        return strong["coast"]
    return strong["key"]


def border_table(document):
    """Every unordered material pair and the profile drawing its contact."""
    keys = sorted(m["key"] for m in document["materials"])
    return {(a, b): border_profile(document, a, b) for i, a in enumerate(keys) for b in keys[i + 1:]}


def seamless_sources(document, root):
    """Same four-native-pixel shared border used by runtime source preparation."""
    result = {}
    for material in document["materials"]:
        periodic = material.get("edges") == "periodic"
        for phase in range(material.get("animation_frames", 1)):
            names = [
                f"{material['sprite']}{v['frame']+phase*material.get('animation_stride',0)}.png"
                for v in material["variants"]
            ]
            master = Image.open(assets(root) / names[0]).convert("RGBA")
            for name in names:
                image = Image.open(assets(root) / name).convert("RGBA")
                # Periodic materials already share one periodic edge band.
                for y in range(0 if periodic else 32):
                    for x in range(32):
                        distance = min(x, y, 31 - x, 31 - y)
                        if distance >= 4:
                            continue
                        source = master.getpixel((min(x, 31 - x), min(y, 31 - y)))
                        pixel = image.getpixel((x, y))
                        alpha = source[3] * (4 - distance) + pixel[3] * distance
                        image.putpixel(
                            (x, y),
                            tuple(
                                (source[k] * source[3] * (4 - distance)
                                 + pixel[k] * pixel[3] * distance) // alpha if alpha else 0
                                for k in range(3)
                            ) + (alpha // 4,),
                        )
                # One source can deliberately be shared by multiple materials.
                if name in result and result[name].tobytes() != image.tobytes():
                    raise ValueError(
                        "Shared frame has inconsistent variant-family borders: " + name
                    )
                result[name] = image
    return result


def seamless_names(document, periodic):
    """Frames referenced by materials with (or without) periodic edges."""
    return {
        f"{material['sprite']}{v['frame']+phase*material.get('animation_stride',0)}.png"
        for material in document["materials"]
        if (material.get("edges") == "periodic") == periodic
        for phase in range(material.get("animation_frames", 1))
        for v in material["variants"]
    }


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


def compile_tileset(document, output, root=ROOT, page_size=1024, runtime_fingerprints=None):
    sources = validate(document, root)
    if page_size < 64 or page_size > 8192 or page_size & (page_size - 1):
        raise ValueError("Page size must be a power of two from 64 to 8192")
    output.mkdir(parents=True, exist_ok=True)
    prepared = seamless_sources(document, root)
    entries, pages = [], []
    border, tile = 4, 40
    columns = page_size // tile
    # Only border-blended frames differ from their native sources. Periodic
    # materials and decor are read directly; packing them would ship them twice.
    filenames = sorted(set(prepared) & seamless_names(document, periodic=False))
    for start in range(0, len(filenames), columns * columns):
        batch = filenames[start : start + columns * columns]
        page = Image.new("RGBA", (page_size, page_size))
        page_id = len(pages)
        for i, name in enumerate(batch):
            image = prepared[name]
            x, y = (i % columns) * tile, (i // columns) * tile
            page.paste(padded(image, border), (x, y))
            if runtime_fingerprints is None:
                with Image.open(assets(root) / name) as native:
                    fingerprint = pixel_fingerprint(native)
            else:
                fingerprint = runtime_fingerprints[name]
            entries.append(
                dict(source=name, page=page_id, rect=[x + border, y + border, 32, 32],
                     native_rgba_fnv1a64=fingerprint)
            )
        levels = []
        # The runtime reads level 0 only and composed view pages make their own
        # mips. The premultiplied round trip keeps the pixels earlier packs stored.
        name = f"textures-{page_id}-mip0.webp"
        page.convert("RGBa").convert("RGBA").save(
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
            for curve in profile.get("contours_q12", [[0, 0, 0, 0, 0]] * 4):
                values = []
                for i in range(size + 1):
                    t = i * 4096 // size
                    count = len(curve) - 1
                    scaled = t * count
                    segment = min(scaled // 4096, count - 1)
                    f = scaled - segment * 4096
                    numerator = curve[segment] * (4096 - f) + curve[segment + 1] * f
                    values.append(
                        numerator // 4096 if numerator >= 0 else -((-numerator) // 4096)
                    )
                assert values[0] == values[-1] == 0
                curves.append(values)
            masks[profile["key"]][str(scale)] = curves
    manifest = dict(
        version=1,
        pages=pages,
        frames=entries,
        masks=masks,
        sources={name: sources[name] for name in filenames},
        catalog=document,
        catalog_sha256=hashlib.sha256(
            json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
        compiler_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        provenance=dict(
            note="Shared periodic variant borders and deterministic reusable contour masks; per-material sources below.",
            materials=material_provenance(document, root),
        ),
    )
    (output / "atlas.json").write_text(json.dumps(manifest, separators=(",", ":")) + "\n")
    return manifest


def read_catalog(path):
    """A catalog from a runtime tree, or from an index file (or a whole
    single-file catalog) with its material files beside it in materials/."""
    path = Path(path)
    if path.is_dir():
        return terrain_catalog.load(path)
    return terrain_catalog.assemble(json.loads(path.read_text()), lambda key: json.loads(
        (path.parent / "materials" / f"{key}.json").read_text()))


def unlisted_materials(content):
    """Material files the index does not list: stale packs left behind."""
    listed = {Path(p).name for p in terrain_catalog.sources(content)[1:]}
    directory = Path(content) / terrain_catalog.MATERIALS
    return sorted(p.name for p in directory.glob("*.json") if p.name not in listed)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manifest", type=Path, default=assets(ROOT),
                    help="runtime tree or catalog index to read (default: the checkout's content/)")
    ap.add_argument("--output", type=Path)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--page-size", type=int, default=1024)
    ap.add_argument("--border-table", action="store_true",
                    help="print the profile drawing every material contact")
    ap.add_argument("--compare", type=Path,
                    help="with --border-table, list only pairs whose profile differs from this catalog")
    args = ap.parse_args()
    document = read_catalog(args.manifest)
    sources = validate(document)
    if args.manifest.is_dir() and unlisted_materials(args.manifest):
        raise SystemExit(f"Material files not listed in {terrain_catalog.INDEX}: "
                         + ", ".join(unlisted_materials(args.manifest)))
    if args.border_table:
        table = border_table(document)
        previous = border_table(read_catalog(args.compare)) if args.compare else None
        for (a, b), profile in table.items():
            if previous is None:
                print(f"{a:>14} | {b:<14} {profile}")
            elif previous.get((a, b)) != profile:
                print(f"{a:>14} | {b:<14} {previous.get((a, b))} -> {profile}")
    if args.output:
        compile_tileset(document, args.output, page_size=args.page_size)
    print(
        f'Validated {len(document["materials"])} materials and {len(sources)} texture frames'
    )


if __name__ == "__main__":
    main()
