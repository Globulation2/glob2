#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a catalogue material's installed frames and provenance without writing.

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/validate_material.py --name boulders
    "$ENC" tools/artwork/validate_material.py --all

Generalises validate_trail.py minus the legacy edge masks: provenance hashes
(source and runtime), 32x32 RGBA frames, distinct variants per phase,
native joins and positional HD joins, the material's opacity rule and
style statistics inside the recipe's band. `provenance.json`'s `method`
selects the production path: `procedural` records are checked against the
synthesiser's generator hashes, `image-generator` and `hybrid` records
against `material.png`.
"""
import argparse
import json
import statistics
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_synth  # noqa: E402
from export_material import FRAME_PREFIX, GLOW_PHASES, sha256_file  # noqa: E402
from material_tiles import ROOT, TILE, luma, pixel_sha256, style_stats  # noqa: E402

# Image-generated art is not pulled through the style transform, so its band is
# wider than the procedural one, but it must stay inside the low-contrast range.
GENERATED_STD_MAX = 26
GENERATED_LUMA_TOLERANCE = 24


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_frame(root, relative, size=TILE):
    path = root / relative
    require(path.is_file(), f"{relative}: missing")
    with Image.open(path) as image:
        require(image.size == (size, size), f"{relative}: expected {size}x{size}")
        require(image.mode == "RGBA", f"{relative}: expected RGBA")
        return image.copy()


def validate_grid_joins(name, phase, tiles, grid):
    """Check every positional layout and their interchangeable perimeter strips."""
    import numpy as np

    cells = grid * grid
    require(bool(tiles) and len(tiles) % cells == 0, f"{name}: incomplete positional layout")
    tile_size = tiles[0].width
    size, strip = grid * tile_size, max(1, tile_size // 8)
    blocks = []
    for first in range(0, len(tiles), cells):
        block = Image.new("RGBA", (size, size))
        for v, tile in enumerate(tiles[first:first + cells]):
            block.paste(tile, ((v % grid) * tile_size, (v // grid) * tile_size))
        blocks.append(block)
    perimeter = ((0, 0, strip, size), (size - strip, 0, size, size),
                 (0, 0, size, strip), (0, size - strip, size, size))
    for layout, block in enumerate(blocks[1:], 1):
        for edge, box in enumerate(perimeter):
            require(block.crop(box).tobytes() == blocks[0].crop(box).tobytes(),
                    f"{name} phase {phase}: {tile_size}px layout {layout} perimeter strip {edge} differs")
    require(len({block.crop((strip, strip, size - strip, size - strip)).tobytes() for block in blocks}) == len(blocks),
            f"{name} phase {phase}: layout interiors must be distinct")
    for layout, block in enumerate(blocks):
        rgb = np.asarray(block.convert("RGB"), dtype=np.float64)
        lum = 0.299 * rgb[:, :, 0] + 0.587 * rgb[:, :, 1] + 0.114 * rgb[:, :, 2]
        steps_by_axis = (("x", np.abs(lum - np.roll(lum, -1, axis=1)).mean(axis=0)),
                         ("y", np.abs(lum - np.roll(lum, -1, axis=0)).mean(axis=1)))
        for axis, steps in steps_by_axis:
            # Interior samples include recurring interpolation phases and
            # intentional plank gaps, unlike the two immediate neighbours.
            interior = sorted(step for i, step in enumerate(steps) if (i + 1) % tile_size)
            upper_decile = interior[int(0.9 * (len(interior) - 1))]
            for edge in range(tile_size - 1, size, tile_size):
                # A broad crest can put the largest slope on a cell cut. Compare
                # its immediate neighborhood too; an isolated seam still has
                # at least one quiet side and cannot supply its own baseline.
                local = (steps[(edge - 1) % size] + steps[(edge + 1) % size]) / 2
                require(steps[edge] <= 1.5 * max(upper_decile, local) + 0.25,
                        f"{name} phase {phase}: {tile_size}px positional block has a seam in layout {layout} at {axis}={edge}")


def validate(name, root=ROOT):
    recipe = terrain_synth.RECIPES.get(name)
    require(recipe is not None, f"{name}: no recipe in terrain_synth.py")
    source_dir = root / "content-src/gfx" / name
    provenance_path = source_dir / "provenance.json"
    require(provenance_path.is_file(), f"{name}: missing {provenance_path.relative_to(root)}")
    record = json.loads(provenance_path.read_text())
    method = record.get("method")
    require(method in ("procedural", "image-generator", "hybrid"), f"{name}: unknown provenance method {method!r}")
    phases = record.get("phases", 1)
    require(phases == recipe.phases,
            f"{name}: provenance phases {phases} differ from the recipe's {recipe.phases}; "
            f"export animated materials with --animate-glow")
    if method == "procedural":
        require(not recipe.placeholder_only, f"{name}: recipe is placeholder_only but provenance is procedural")
        require(record.get("generator_sha256") == terrain_synth.generator_hashes(),
                f"{name}: generator hashes differ; re-run terrain_synth.py")
    else:
        source = source_dir / record.get("source", "material.png")
        require(source.is_file(), f"{name}: missing {source.relative_to(root)}")
        require(sha256_file(source) == record.get("source_sha256"), f"{name}: material.png differs from provenance")
        for reference, digest in record.get("reference_sha256", {}).items():
            path = root / reference
            require(not path.is_file() or sha256_file(path) == digest, f"{name}: reference {reference} changed")
        require(method == "image-generator" or phases == GLOW_PHASES, f"{name}: hybrid exports need {GLOW_PHASES} phases")

    stride = ((recipe.grid ** 2 or terrain_synth.VARIANTS) * recipe.blocks
              if method == "procedural" else terrain_synth.VARIANTS)
    expected = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(stride * phases)]
    runtime = record.get("runtime_sha256", {})
    require(list(runtime) == expected, f"{name}: provenance must cover exactly {len(expected)} frames in order")
    frames = []
    for relative in expected:
        frame = load_frame(root, relative)
        require(pixel_sha256(frame) == runtime[relative], f"{relative}: pixels differ from provenance")
        frames.append(frame)

    # Edge flags describe procedural output; image/hybrid exporters retain
    # their own reflected, shared-ring contract and need no HD block frames.
    grid = recipe.grid if method == "procedural" else 0
    periodic = recipe.periodic if method == "procedural" else False
    low, high = recipe.alpha_range
    for phase in range(phases):
        tiles = frames[phase * stride : (phase + 1) * stride]
        require(len({tile.tobytes() for tile in tiles}) == stride,
                f"{name} phase {phase}: variants must be distinct")
        if grid:
            validate_grid_joins(name, phase, tiles, grid)
            hd_tiles = []
            for frame_index in range(phase * stride, (phase + 1) * stride):
                relative = f"{terrain_synth.HD_PREFIX}{name}{frame_index}.png"
                frame = load_frame(root, relative, size=terrain_synth.N)
                require(pixel_sha256(frame) == record.get("highres_sha256", {}).get(relative),
                        f"{relative}: HD pixels differ from provenance")
                hd_tiles.append(frame)
            validate_grid_joins(name, phase, hd_tiles, grid)
        for tile in tiles:
            lo, hi = tile.getchannel("A").getextrema()
            require(low <= lo and hi <= high, f"{name}: alpha {lo}..{hi} outside the material's rule {low}..{high}")
            for other in tiles:
                if grid:
                    continue
                if periodic:
                    # Periodic edges: every variant carries the same outer ring,
                    # which continues across the edge like one wrapping tile.
                    for box in ((0, 0, 1, 32), (31, 0, 32, 32), (0, 0, 32, 1), (0, 31, 32, 32)):
                        require(tile.crop(box).tobytes() == other.crop(box).tobytes(),
                                f"{name} phase {phase}: periodic variant ring differs")
                    continue
                require(tile.crop((31, 0, 32, 32)).tobytes() == other.crop((0, 0, 1, 32)).tobytes(),
                        f"{name} phase {phase}: horizontal variant join differs")
                require(tile.crop((0, 31, 32, 32)).tobytes() == other.crop((0, 0, 32, 1)).tobytes(),
                        f"{name} phase {phase}: vertical variant join differs")

    stats = style_stats(frames[:stride])
    if method == "procedural":
        require(abs(stats["luma"] - recipe.style.luma) <= 6, f"{name}: luma {stats['luma']} off target {recipe.style.luma}")
        tolerance = 3 + 3 * (1 - recipe.style.match)
        require(abs(stats["std"] - recipe.style.std) <= tolerance, f"{name}: std {stats['std']} off target {recipe.style.std}")
        require(stats["grain"] <= recipe.style.grain_max, f"{name}: grain {stats['grain']} above {recipe.style.grain_max}")
    else:
        require(abs(stats["luma"] - recipe.style.luma) <= GENERATED_LUMA_TOLERANCE,
                f"{name}: luma {stats['luma']} too far from the recipe target {recipe.style.luma}")
        limit = max(GENERATED_STD_MAX, recipe.style.std + 6)
        require(stats["std"] <= limit, f"{name}: std {stats['std']} above {limit}")
        require(stats["grain"] <= recipe.style.grain_max + 4, f"{name}: grain {stats['grain']} too coarse")
    return dict(method=method, frames=len(frames), stats=stats)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--name", action="append", help="material key (repeatable)")
    group.add_argument("--all", action="store_true", help="every material with a provenance record")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    names = args.name or [n for n in terrain_synth.SYNTH_ORDER if (root / "content-src/gfx" / n / "provenance.json").is_file()]
    failures = 0
    for name in names:
        try:
            result = validate(name, root)
        except ValueError as error:
            failures += 1
            print(f"FAIL {error}")
            continue
        s = result["stats"]
        print(f"PASS {name:<14} {result['method']:<15} {result['frames']:3d} frames  "
              f"luma {s['luma']} std {s['std']} grain {s['grain']} sat {s['sat']} alpha {s['alpha']}")
    if failures:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
