#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthesise original procedural terrain materials for the terrain catalog.

Each material's recipe is its own module under terrain_packs/, built on the
shared library in terrain_core.py; this module renders them and writes their
frames, provenance and catalog files (content/data/terrain/materials/).

Usage (with the pinned development dependencies and Pillow 12.2.0):

    uv venv artifacts/asset-tools/venv
    uv pip install --python artifacts/asset-tools/venv/bin/python -r tools/asset-requirements.txt
    ENC=artifacts/asset-tools/venv/bin/python
    "$ENC" tools/artwork/terrain_synth.py                    # write all frames and catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --material mud --contact-sheet artifacts/terrain/mud.png
    "$ENC" tools/artwork/terrain_synth.py --emit-catalog     # print catalog blocks
    "$ENC" tools/artwork/terrain_synth.py --write-catalog    # catalog from committed frames (no render)
    "$ENC" tools/artwork/terrain_synth.py --hd               # 128x128 renders under artifacts/

Every material is synthesised from scratch: periodic value noise, fractal sums,
domain warps, Worley cells, scattered stamps and palette ramps, all driven by
`random.Random(seed).random()` with `seed = fnv1a32("<material>:<variant>:<phase>")`.
No pixel of an existing tile is ever read into an output; the native grass, sand,
trail, ice and water tiles are only measured for the style targets (luma mean and
spread, neighbour grain, saturation) that keep the new art low-contrast and
painterly beside them.

Ground materials render periodic grid blocks, sliced into 128x128 positional
tiles (`variant_grid`) and box-downsampled to 32x32. Static materials use
8x8 blocks with four seeded interiors and common boundaries, as do lava and
ember field (crossfading in four steps per phase); water and deep water use
one 4x4 block, to keep their animated frame banks small. Feature
counts and noise frequencies scale with canvas size to keep their native density.
The interchangeable blocks preserve shared edge geometry while varying their
interiors. Frames are phase-major, block-major, then
row-major cell: `cell + cells * block + (cells * blocks) * phase`, where
`cells = grid * grid`. Animated materials
render four phases (sixteen for water and deep water).
Outputs are `content/data/gfx/terrain-<name>N.png`, the 128x128 HD frames the classic
tiles were downsampled from (`content/data/highres/v1/terrain-<name>N.png`, registered in
the pack through tools/artwork/highres_pack.py) and `content-src/gfx/<name>/provenance.json`.

Generation runs on any platform. Renders depend on Pillow's resampling and the
C library's sin/atan2/hypot, so another platform may differ in a few low bits;
the provenance records the platform and generator for information only, and
nothing re-renders committed art to compare it.

Python 3, Pillow, NumPy and SciPy (development-only dependencies).
"""
import argparse
import ast
import concurrent.futures
import hashlib
import json
import math
import os
import platform
import random
import sys
from pathlib import Path

import PIL
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from material_tiles import (  # noqa: E402
    ROOT,
    STYLE_REFERENCES,
    TILE,
    fnv1a32,
    luma,
    mean_color,
    pixel_sha256,
    reference_stats,
    reference_tiles,
    runtime_blend,
    share_perimeter,
    style_stats,
    tiles_to_sheet,
    write_frames,
)
from terrain_core import (  # noqa: E402
    Ctx,
    N,
    RECIPES,
    VARIANTS,
    XS,
    YS,
    apply_style,
    band_weight,
    box_down_fields,
    fields_to_image,
    neutral_band,
    style_transform,
)
import terrain_packs  # noqa: E402,F401  registers every material recipe
from terrain_tileset import BUILTIN_NAMES, LEGACY_BINDINGS  # noqa: E402
import terrain_catalog  # noqa: E402
import terrain_stones  # noqa: E402

PILLOW_VERSION = "12.2.0"
FRAME_PREFIX = "content/data/gfx/terrain-"
# The catalog names sprites by their runtime path, under the data search root.
SPRITE_PREFIX = "data/gfx/terrain-"
HD_PREFIX = "content/data/highres/v1/terrain-"
HD_CATEGORY = "procedural-materials"
HD_RECIPE = "procedural terrain synthesis v1"
# Stone maps of the laid paths with whole-stone edges (see terrain_stones.py).
STONE_MAP_PREFIX = "data/terrain/stones/"


# Catalogue order of the built-in types, from the engine's generated name list.
BUILTIN_ORDER = [name for name in json.loads(BUILTIN_NAMES.read_text()) if name not in LEGACY_BINDINGS]
# Legacy bindings whose material is synthesised here too: regular water is an
# ordinary animated tile set rather than a scrolling backdrop.
SYNTHESISED_LEGACY = list(LEGACY_BINDINGS)
SYNTH_ORDER = BUILTIN_ORDER + SYNTHESISED_LEGACY

assert set(SYNTH_ORDER) == set(RECIPES), "every built-in needs a recipe and vice versa"

# --------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------


# Periodic materials take variant 0's pixels in the outer band: fully within
# PERIODIC_BAND render px of the edge, fading to the variant over PERIODIC_FADE.
PERIODIC_BAND = 4
PERIODIC_FADE = 6


def render_fields(name, variant, phase):
    rec = RECIPES[name]
    ctx = Ctx(name, variant, phase, rec.phases)
    rgb, alpha = rec.render(ctx)
    if rec.periodic:
        return rgb, alpha
    return neutral_band(rgb, alpha)


def force_periodic_band(renders):
    """Give every render variant 0's outer band so all variants share edges."""
    weight = band_weight(PERIODIC_BAND, PERIODIC_FADE)
    master_rgb, master_alpha = renders[0]
    out = [renders[0]]
    for rgb, alpha in renders[1:]:
        rgb = [[c + (m - c) * w for c, m, w in zip(channel, master, weight)]
               for channel, master in zip(rgb, master_rgb)]
        if alpha is not None:
            alpha = [a + (m - a) * w for a, m, w in zip(alpha, master_alpha, weight)]
        out.append((rgb, alpha))
    return out


def perimeter_structure(rgb, depth=16):
    """Luma spread of the outer `depth` render pixels: how much a perimeter
    carries that the runtime would repeat on every tile."""
    values = [
        luma(r, g, b)
        for r, g, b, x, y in zip(*rgb, XS, YS)
        if min(x, y, N - 1 - x, N - 1 - y) < depth
    ]
    mean = sum(values) / len(values)
    return math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))


def render_phase(name, phase):
    """Render every layout's tiles for one phase at native and HD resolution.

    Grid blocks are sliced after rendering continuous fields. One style
    transform pools every layout so common boundaries keep identical colors.
    Nonperiodic fallback sources still choose the quietest perimeter as their
    master. Both resolutions come from the same floating-point fields.
    """
    rec = RECIPES[name]
    if rec.grid:
        size = rec.grid * N
        blocks = [rec.render(Ctx(name, layout, phase, rec.phases, size=size))
                  for layout in range(rec.blocks)]

        def cell(values, variant):
            gx, gy = variant % rec.grid, variant // rec.grid
            return [values[y * size + x]
                    for y in range(gy * N, (gy + 1) * N)
                    for x in range(gx * N, (gx + 1) * N)]

        renders = [([cell(channel, v) for channel in rgb], None if alpha is None else cell(alpha, v))
                   for rgb, alpha in blocks for v in range(rec.cells)]
    else:
        renders = [render_fields(name, variant, phase) for variant in range(VARIANTS)]
    if rec.periodic:
        order = list(range(len(renders)))
    elif rec.phases == 1:
        order = sorted(range(VARIANTS), key=lambda v: (perimeter_structure(renders[v][0]), v))
    else:
        # Animated phases must keep one variant order; phase 0 decides it.
        order = sorted(range(VARIANTS), key=lambda v: (perimeter_structure(render_fields(name, v, 0)[0]), v))
    renders = [renders[v] for v in order]
    if rec.periodic and not rec.grid:
        renders = force_periodic_band(renders)
    small = [box_down_fields(rgb, alpha) for rgb, alpha in renders]
    pooled = [sum((rgb[k] for rgb, _ in small), []) for k in range(3)]
    transform = style_transform(pooled, rec.style.luma, rec.style.std, rec.style.match)
    native, hd = [], []
    for (rgb, alpha), (small_rgb, small_alpha) in zip(renders, small):
        hd.append(fields_to_image(apply_style(rgb, transform), alpha, N))
        native.append(fields_to_image(apply_style(small_rgb, transform),
                                      small_alpha if alpha is not None else None, TILE))
    return native, hd


def _render_job(args):
    name, phase = args
    native, hd = render_phase(name, phase)
    return (name, phase, [t.tobytes() for t in native], native[0].size,
            [t.tobytes() for t in hd], hd[0].size)


# A worker rendering one 1024 px block peaks at roughly this much memory.
WORKER_MEMORY = 1.5 * 2 ** 30


def default_jobs():
    """One render process per core, as many as physical memory allows."""
    cores = os.cpu_count() or 1
    try:
        memory = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (AttributeError, ValueError, OSError):
        return min(2, cores)
    return max(1, min(cores, int(memory // WORKER_MEMORY)))


def synthesize_both(names, jobs=None):
    """Render and perimeter-share all variants at both resolutions:
    ({name: {phase: [native tiles]}}, {name: {phase: [HD tiles]}})."""
    tasks = [(name, phase) for name in names for phase in range(RECIPES[name].phases)]
    # Each 1024px seeded block keeps several scalar and RGB fields alive, so
    # the automatic worker count is bounded by memory as well as cores.
    jobs = jobs or default_jobs()
    if jobs > 1 and len(tasks) > 1:
        with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(tasks))) as pool:
            rendered = list(pool.map(_render_job, tasks))
    else:
        rendered = [_render_job(task) for task in tasks]
    native, hd = {}, {}
    for name, phase, small, small_size, large, large_size in rendered:
        tiles = [Image.frombytes("RGBA", small_size, d) for d in small]
        periodic = RECIPES[name].periodic
        native.setdefault(name, {})[phase] = tiles if periodic else share_perimeter(tiles, 2)
        tiles = [Image.frombytes("RGBA", large_size, d) for d in large]
        hd.setdefault(name, {})[phase] = tiles if periodic else share_perimeter(tiles, 2 * (N // TILE))
    return native, hd


def synthesize(names, jobs=None, hd=False):
    """Render and perimeter-share all variants: {name: {phase: [tiles]}}."""
    return synthesize_both(names, jobs)[1 if hd else 0]


def frames_of(name, phases):
    """Flatten phases; each contains block-major, row-major cell tiles."""
    return [tile for phase in sorted(phases) for tile in phases[phase]]


def preview_colors(name, phases):
    rec = RECIPES[name]
    mean = mean_color(phases[0])
    preview = tuple(rec.preview) if rec.preview else mean
    minimap = tuple(rec.minimap) if rec.minimap else preview
    return [int(c) for c in preview], [int(c) for c in minimap], mean


# --------------------------------------------------------------------------
# Catalog and provenance
# --------------------------------------------------------------------------


def material_block(name, phases):
    rec = RECIPES[name]
    preview, minimap, _ = preview_colors(name, phases)
    block = {
        "key": name,
        "sprite": f"{SPRITE_PREFIX}{name}",
        "profile": rec.profile,
        "class": rec.border_class,
        "preview": list(preview),
        "variants": [{"frame": i, "weight": 1} for i in range(rec.cells * rec.blocks)],
    }
    if rec.phases > 1:
        block["animation_frames"] = rec.phases
        block["animation_stride"] = rec.cells * rec.blocks
        block["animation_ticks"] = rec.animation_ticks
        if rec.blend_steps:
            block["animation_blend_steps"] = rec.blend_steps
    if rec.periodic:
        block["edges"] = "periodic"
    if rec.grid:
        block["variant_grid"] = rec.grid
    if rec.blocks > 1:
        block["variant_blocks"] = rec.blocks
    if rec.block_group:
        block["variant_block_group"] = rec.block_group
    block["minimap"] = list(minimap)
    block["seam"] = dict(rec.seam)
    if rec.stone_edges:
        block["stones"] = f"{STONE_MAP_PREFIX}{name}.png"
    return block


def catalog_fragment(results):
    names = [n for n in SYNTH_ORDER if n in results]
    return {
        "materials": [material_block(name, results[name]) for name in names],
        "bindings": {name: name for name in names},
    }


def merge_catalog(document, fragment):
    """Replace or append the fragment's materials and bindings.

    Everything else in the catalog (version, profiles, warp, existing
    materials) is left untouched, so re-running after a recipe change only
    moves the blocks that terrain_synth.py owns.
    """
    by_key = {m["key"]: i for i, m in enumerate(document["materials"])}
    for block in fragment["materials"]:
        if block["key"] in by_key:
            previous = document["materials"][by_key[block["key"]]]
            # Raised decor, ground overlays and shoreline surf are authored
            # elsewhere; refreshing ground must retain them.
            block = dict(block, **{k: previous[k] for k in ("decor", "overlay", "surf") if k in previous})
            document["materials"][by_key[block["key"]]] = block
        else:
            document["materials"].append(block)
    document["bindings"].update(fragment["bindings"])
    return document


def committed_phases(name, root):
    """The committed native frames of a material's first phase, as rendered."""
    rec = RECIPES[name]
    return [[Image.open(root / f"{FRAME_PREFIX}{name}{i}.png").copy() for i in range(rec.cells * rec.blocks)]]


def write_catalog(results, root):
    content = root / "content"
    document = terrain_catalog.load(content)
    merge_catalog(document, catalog_fragment(results))
    terrain_catalog.save(content, document)


def presentation_initialisers(results):
    lines = ["// TerrainPresentations colours: {minimap}, {overview}; derived by terrain_synth.py"]
    for name in BUILTIN_ORDER:
        if name not in results:
            continue
        preview, minimap, _ = preview_colors(name, results[name])
        lines.append(
            f"/* {name:<14} */ {{{{{minimap[0]:3d}, {minimap[1]:3d}, {minimap[2]:3d}}}, "
            f"{{{preview[0]:3d}, {preview[1]:3d}, {preview[2]:3d}}}}},"
        )
    return "\n".join(lines)


def generator_sources(name):
    """The files a material's frames come from: this pipeline, the shared
    library, the material's own module and the family helpers it imports.
    Tuning one material therefore changes only that material's record."""
    here = Path(__file__).resolve().parent
    module = Path(sys.modules[RECIPES[name].render.__module__].__file__).resolve()
    helpers = sorted({
        module.parent / f"{node.module}.py"
        for node in ast.walk(ast.parse(module.read_text()))
        if isinstance(node, ast.ImportFrom) and node.level == 1
    })
    return [here / "terrain_synth.py", here / "terrain_core.py", here / "material_tiles.py", module, *helpers]


def generator_hashes(name):
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in generator_sources(name)}


def platform_record():
    """Where the frames were produced; libm and rounding can differ elsewhere."""
    libc = platform.libc_ver()
    return {
        "system": sys.platform,
        "machine": platform.machine(),
        "libc": " ".join(part for part in libc if part) or None,
    }


def provenance_document(name, phases, hashes, root, hd_hashes=None):
    rec = RECIPES[name]
    preview, minimap, mean = preview_colors(name, phases)
    references = reference_stats(root)
    return {
        "generator": "tools/artwork/terrain_synth.py",
        "method": "procedural",
        "generator_sha256": generator_hashes(name),
        "material": name,
        "group": rec.group,
        "label": rec.label,
        "recipe": rec.describe(),
        "seed": f'fnv1a32("{name}:<variant>:<phase>")',
        "render_size": N,
        "block_render_size": N * rec.grid if rec.grid else N,
        "tile_size": TILE,
        "variants": rec.cells * rec.blocks,
        "phases": rec.phases,
        "frame_layout": f"cell + {rec.cells} * block + {rec.cells * rec.blocks} * phase",
        "sprite": f"{FRAME_PREFIX}{name}",
        "preview": list(preview),
        "minimap": list(minimap),
        "mean_color": list(mean),
        "style_stats": {str(phase): style_stats(tiles) for phase, tiles in sorted(phases.items())},
        "style_references": {
            f"{prefix}{first}..{first + 15}.png": {"use": "statistics only", "stats": references[key]}
            for key, (prefix, first) in STYLE_REFERENCES.items()
        },
        "python": platform.python_version(),
        "pillow": PIL.__version__,
        "platform": platform_record(),
        "runtime_sha256": hashes,
        "highres_sha256": hd_hashes or {},
    }


def _stone_map_job(name):
    return name, terrain_stones.stone_map(name).tobytes()


def write_stone_maps(names, root, jobs=None):
    """Render each named path's stone geometry and write its stone map."""
    names = [n for n in names if RECIPES[n].stone_edges]
    out = root / "content" / STONE_MAP_PREFIX
    out.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ProcessPoolExecutor(max_workers=jobs or default_jobs()) as pool:
        for name, data in pool.map(_stone_map_job, names):
            size = RECIPES[name].grid * N
            Image.frombytes("RGBA", (2 * size, size), data).save(out / f"{name}.png", optimize=True)
            print(f"{name:<18} stone map {size}x{size} (two blocks)")
    return names


def write_material(name, phases, hd_phases, root):
    from highres_pack import register_frames, source_record

    frames = frames_of(name, phases)
    hashes = write_frames(frames, f"{FRAME_PREFIX}{name}", 0, root)
    hd_frames = frames_of(name, hd_phases)
    generators = [source_record(path, root) for path in generator_sources(name)]
    register_frames([
        {"id": f"terrain-{name}{i}", "image": tile, "recipe": f"{HD_RECIPE}: {name}", "sources": generators}
        for i, tile in enumerate(hd_frames)
    ], HD_CATEGORY, root)
    hd_hashes = {f"{HD_PREFIX}{name}{i}.png": pixel_sha256(tile) for i, tile in enumerate(hd_frames)}
    document = provenance_document(name, phases, hashes, root, hd_hashes)
    path = root / "content-src/gfx" / name / "provenance.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n")
    return hashes


# --------------------------------------------------------------------------
# Contact sheet
# --------------------------------------------------------------------------


def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except (TypeError, OSError):
        return ImageFont.load_default()


def contact_sheet(results, root, columns=2):
    """Every material: name/stats, 16 variants, and runtime-neighbour fields.

    Tiles are shown after the runtime's own four-pixel border blend
    (`runtime_blend`, the same arithmetic as `seamless_sources` in
    tools/terrain_tileset.py), so joins and the 3x3 fields look as they do in
    the game. Translucent materials sit over the ocean backdrop's mean colour.
    """
    scale = 4
    blocks = []
    legacy = {key: reference_tiles(key, root) for key in STYLE_REFERENCES}
    entries = [(name, results[name][0], RECIPES[name].group, True) for name in SYNTH_ORDER if name in results]
    entries += [(name, tiles, "legacy", False) for name, tiles in legacy.items()]
    font = _font(13)
    small = _font(11)
    grid_w = 8 * TILE * scale
    join_w = 2 * TILE * scale
    field_w = 3 * TILE * 2
    block_w = grid_w + 16 + join_w + 16 + field_w + 16
    max_variants = max(len(tiles) for _, tiles, _, _ in entries)
    strip_h = ((max_variants + VARIANTS - 1) // VARIANTS) * TILE
    panel_h = ((max_variants + 7) // 8) * TILE * scale
    block_h = 20 + 16 + strip_h + 8 + max(panel_h, 3 * TILE + 8 + 6 * TILE) + 8
    for name, tiles, group, generated in entries:
        if not (generated and RECIPES[name].periodic):
            tiles = runtime_blend(tiles)
        block = Image.new("RGBA", (block_w, block_h), (40, 40, 44, 255))
        draw = ImageDraw.Draw(block)
        stats = style_stats(tiles)
        title = f"{name}  [{group}]" if generated else f"{name}  [native reference]"
        draw.text((4, 2), title, font=font, fill=(240, 240, 240, 255))
        draw.text(
            (4, 18),
            f"luma {stats['luma']}  std {stats['std']}  grain {stats['grain']}  sat {stats['sat']}  alpha {stats['alpha']}",
            font=small, fill=(200, 200, 200, 255),
        )
        y = 36
        block.alpha_composite(tiles_to_sheet(tiles, columns=VARIANTS), (4, y))
        y += strip_h + 8
        block.alpha_composite(tiles_to_sheet(tiles, columns=8, scale=scale), (4, y))
        rng = random.Random(fnv1a32(name + ":join"))
        grid = RECIPES[name].grid if generated else 0

        def field_tiles(width):
            if grid:
                # Include the block's wrap join, with the same positional rule as runtime.
                count = RECIPES[name].blocks
                return [tiles[(fnv1a32(f"{name}:preview:{(x + grid - 1) // grid}:{(y + grid - 1) // grid}") % count)
                              * grid * grid + ((y + grid - 1) % grid) * grid + (x + grid - 1) % grid]
                        for y in range(width) for x in range(width)]
            return [tiles[rng.randrange(len(tiles))] for _ in range(width * width)]

        picks = field_tiles(2)
        block.alpha_composite(tiles_to_sheet(picks, columns=2, scale=scale), (4 + grid_w + 16, y))
        # A field at 1x and 2x using actual runtime neighbours.
        field = field_tiles(3)
        fx = 4 + grid_w + 16 + join_w + 16
        block.alpha_composite(tiles_to_sheet(field, columns=3), (fx, y))
        block.alpha_composite(tiles_to_sheet(field, columns=3, scale=2), (fx, y + 3 * TILE + 8))
        if stats["alpha"] < 250:
            # Translucent materials are shown over the ocean backdrop's mean colour.
            ocean = Image.new("RGBA", block.size, (69, 52, 200, 255))
            ocean.alpha_composite(block)
            block = ocean
        blocks.append(block)
    rows = (len(blocks) + columns - 1) // columns
    sheet = Image.new("RGBA", (columns * block_w, rows * block_h), (20, 20, 22, 255))
    for i, block in enumerate(blocks):
        sheet.paste(block, ((i % columns) * block_w, (i // columns) * block_h))
    return sheet


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def require_pillow():
    """Warn when Pillow differs from the pinned version: its resampling kernels
    can shift a few low bits of the art, which is harmless."""
    if PIL.__version__ != PILLOW_VERSION:
        print(f"note: rendering with Pillow {PIL.__version__}; the pinned version is {PILLOW_VERSION}",
              file=sys.stderr)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--material", action="append", help="material name (repeatable; default all)")
    parser.add_argument("--contact-sheet", type=Path, help="write a review sheet PNG (no frames written)")
    parser.add_argument("--emit-catalog", action="store_true", help="print catalog blocks and colour initialisers")
    parser.add_argument("--write-catalog", action="store_true",
                        help="merge the material blocks and bindings into the terrain catalog (content/data/terrain/) "
                             "from the committed frames, without rendering (writing frames also does this)")
    parser.add_argument("--hd", action="store_true", help="write 128x128 renders under artifacts/terrain/hd/ (no frames written)")
    parser.add_argument("--write", action="store_true", help="also write frames when another mode is selected")
    parser.add_argument("--stone-maps", action="store_true",
                        help="write the stone maps of the selected whole-stone paths (content/data/terrain/stones/) "
                             "and their catalog blocks, without writing frames")
    parser.add_argument("--jobs", type=int, default=0, help="parallel render processes (default: one per core, bounded by memory)")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to read and write")
    args = parser.parse_args(argv)
    require_pillow()
    names = args.material or list(SYNTH_ORDER)
    unknown = [n for n in names if n not in RECIPES]
    if unknown:
        parser.error(f"unknown material(s): {', '.join(unknown)}; known: {', '.join(SYNTH_ORDER)}")
    root = args.root.resolve()
    jobs = args.jobs or None

    if args.stone_maps:
        written = write_stone_maps(names, root, jobs)
        write_catalog({name: committed_phases(name, root) for name in written}, root)
        print(f"Wrote {len(written)} stone maps and merged their catalog blocks")
        return

    if args.hd:
        hd = synthesize(names, jobs, hd=True)
        out = root / "artifacts/terrain/hd"
        out.mkdir(parents=True, exist_ok=True)
        for name, phases in hd.items():
            for i, tile in enumerate(frames_of(name, phases)):
                tile.save(out / f"terrain-{name}{i}.png")
        print(f"Wrote HD renders for {len(hd)} materials to {out}")

    # Review modes do not write frames unless --write is given; --hd alone is
    # complete after the renders above. The catalog needs only each material's
    # mean colour, so --write-catalog alone reads it from the committed frames.
    modes = args.contact_sheet is not None or args.emit_catalog or args.write_catalog or args.hd
    renders = args.contact_sheet is not None or args.emit_catalog or not modes or args.write
    if args.write_catalog and not renders:
        write_catalog({name: committed_phases(name, root) for name in names}, root)
        print(f"Merged {len(names)} material blocks from committed frames into content/data/terrain/")
        return
    if not renders:
        return
    results, hd_results = synthesize_both(names, jobs)
    if args.contact_sheet is not None:
        args.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        contact_sheet(results, root).save(args.contact_sheet)
        print(f"Wrote contact sheet {args.contact_sheet}")
    if args.emit_catalog:
        print(json.dumps(catalog_fragment(results), indent=2))
        print(presentation_initialisers(results))
    if not modes or args.write:
        for name in names:
            hashes = write_material(name, results[name], hd_results[name], root)
            stats = style_stats(results[name][0])
            print(f"{name:<14} {len(hashes):3d} frames  luma {stats['luma']:5.1f} std {stats['std']:4.1f} "
                  f"grain {stats['grain']:4.1f} sat {stats['sat']:.2f}")
        print(f"Wrote {len(names)} materials under {root / 'content/data/gfx'} and {root / 'content-src/gfx'}")
    if args.write_catalog or not modes or args.write:
        # Written frames carry their catalog blocks from the same render.
        write_catalog(results, root)
        print(f"Merged {len(results)} material blocks into content/data/terrain/")


if __name__ == "__main__":
    main()
