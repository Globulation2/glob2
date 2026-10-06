#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a catalogue material's installed frames and provenance without writing.

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/validate_material.py --name boulders
    "$ENC" tools/artwork/validate_material.py --all

Generalises validate_trail.py minus the legacy edge masks: provenance hashes
(source and runtime), 32x32 RGBA frames, sixteen distinct variants per phase,
every ordered join equal on ring 0, the material's opacity rule and
style statistics inside the recipe's band. `provenance.json`'s `method`
selects the production path: `procedural` records are checked against the
synthesiser's generator hashes, `image-generator` and `hybrid` records
against `material.png`.
"""
import argparse
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import terrain_synth  # noqa: E402
from export_material import FRAME_PREFIX, sha256_file  # noqa: E402
from material_tiles import ROOT, TILE, pixel_sha256, style_stats  # noqa: E402

# Image-generated art is not pulled through the style transform, so its band is
# wider than the procedural one, but it must stay inside the low-contrast range.
GENERATED_STD_MAX = 26
GENERATED_LUMA_TOLERANCE = 24


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_frame(root, relative):
    path = root / relative
    require(path.is_file(), f"{relative}: missing")
    with Image.open(path) as image:
        require(image.size == (TILE, TILE), f"{relative}: expected {TILE}x{TILE}")
        require(image.mode == "RGBA", f"{relative}: expected RGBA")
        return image.copy()


def validate(name, root=ROOT):
    recipe = terrain_synth.RECIPES.get(name)
    require(recipe is not None, f"{name}: no recipe in terrain_synth.py")
    source_dir = root / "datasrc/gfx" / name
    provenance_path = source_dir / "provenance.json"
    require(provenance_path.is_file(), f"{name}: missing {provenance_path.relative_to(root)}")
    record = json.loads(provenance_path.read_text())
    method = record.get("method")
    require(method in ("procedural", "image-generator", "hybrid"), f"{name}: unknown provenance method {method!r}")
    phases = record.get("phases", 1)
    if method == "procedural":
        require(phases == recipe.phases, f"{name}: provenance phases {phases} differ from the recipe")
        require(record.get("generator_sha256") == terrain_synth.generator_hashes(),
                f"{name}: generator hashes differ; re-run terrain_synth.py")
    else:
        source = source_dir / record.get("source", "material.png")
        require(source.is_file(), f"{name}: missing {source.relative_to(root)}")
        require(sha256_file(source) == record.get("source_sha256"), f"{name}: material.png differs from provenance")
        for reference, digest in record.get("reference_sha256", {}).items():
            path = root / reference
            require(not path.is_file() or sha256_file(path) == digest, f"{name}: reference {reference} changed")
        require(method == "image-generator" or phases == terrain_synth.VARIANTS // 4, f"{name}: hybrid exports need four phases")

    expected = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(terrain_synth.VARIANTS * phases)]
    runtime = record.get("runtime_sha256", {})
    require(list(runtime) == expected, f"{name}: provenance must cover exactly {len(expected)} frames in order")
    frames = []
    for relative in expected:
        frame = load_frame(root, relative)
        require(pixel_sha256(frame) == runtime[relative], f"{relative}: pixels differ from provenance")
        frames.append(frame)

    low, high = recipe.alpha_range
    for phase in range(phases):
        tiles = frames[phase * terrain_synth.VARIANTS : (phase + 1) * terrain_synth.VARIANTS]
        require(len({tile.tobytes() for tile in tiles}) == terrain_synth.VARIANTS,
                f"{name} phase {phase}: variants must be distinct")
        for tile in tiles:
            lo, hi = tile.getchannel("A").getextrema()
            require(low <= lo and hi <= high, f"{name}: alpha {lo}..{hi} outside the material's rule {low}..{high}")
            for other in tiles:
                require(tile.crop((31, 0, 32, 32)).tobytes() == other.crop((0, 0, 1, 32)).tobytes(),
                        f"{name} phase {phase}: horizontal variant join differs")
                require(tile.crop((0, 31, 32, 32)).tobytes() == other.crop((0, 0, 32, 1)).tobytes(),
                        f"{name} phase {phase}: vertical variant join differs")

    stats = style_stats(frames[: terrain_synth.VARIANTS])
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
    names = args.name or [n for n in terrain_synth.BUILTIN_ORDER if (root / "datasrc/gfx" / n / "provenance.json").is_file()]
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
