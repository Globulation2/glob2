#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export an image-generated terrain material into catalogue sprite frames.

    ENC="$(python3 tools/package_assets.py --encoder-python)"
    "$ENC" tools/artwork/export_material.py --name boulders            # write frames
    "$ENC" tools/artwork/export_material.py --name boulders --check    # compare with committed
    "$ENC" tools/artwork/export_material.py --name lava --animate-glow # four procedural glow phases
    "$ENC" tools/artwork/export_material.py --name hedge --prompt      # print the stored prompt

Reads `datasrc/gfx/<name>/material.png` (square, at least 512 pixels), area
averages it to the 128x128 sheet, cuts sixteen 32x32 tiles, shares their
perimeter (tools/artwork/material_tiles.py, the same code the procedural
path uses) and writes `data/gfx/terrain-<name>0..15.png` plus a provenance
record with `method: "image-generator"`, the prompt, reference and source
hashes, runtime pixel hashes and a `replaces` record for the procedural
provenance it supersedes. Silhouette-heavy materials (boulders, hedge,
thicket, lava, ember field, flower meadow, outcrop) take this route; the
procedural recipes in terrain_synth.py stand in until the generated art
lands and are then marked `placeholder_only`.

`--animate-glow` (lava, ember field) keeps the generated crust and channel
layout and applies the recipe's procedural glow ramp per phase to the warm
pixels, so the four phases stay deterministic (`method: "hybrid"`).
"""
import argparse
import hashlib
import json
import math
import random
import sys
from pathlib import Path

import PIL
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_tiles import (  # noqa: E402
    ROOT,
    SHEET,
    TILE,
    box_down,
    fnv1a32,
    luma,
    pixel_sha256,
    share_perimeter,
    sheet_to_tiles,
    style_stats,
    write_frames,
)

FRAME_PREFIX = "data/gfx/terrain-"
VARIANTS = 16
MIN_SOURCE = 512
GLOW_PHASES = 4

# Style references supplied to the image generator: grass and sand establish
# the texture style, trail the earth tones, the inn the organic art style.
REFERENCES = (
    "data/gfx/terrain0.png",
    "data/gfx/terrain128.png",
    "data/gfx/terrain288.png",
    "data/gfx/inn0b0.png",
)

PROMPT_HEADER = (
    "Use case: stylized-concept\n"
    "Asset type: seamless top-down terrain material for Globulation 2, ultimately sixteen 32x32 pixel tiles.\n"
    "Generate a square, edge-to-edge, seamless tileable material sample viewed vertically overhead. "
    "Match the coarse painterly game texture, pastel-earthy palette and understated low contrast of the "
    "reference terrain and organic building; flat diffuse lighting, no cast shadows from off-image objects. "
    "The sample is cut into sixteen tiles that meet in any order, so the texture must be statistically "
    "uniform: no borders, paths, horizon, gradient, focal object or directional shape, no perspective, "
    "text, labels, grids or watermark. Keep features large and soft enough to read after downscaling "
    "each 128x128 region to 32x32. Reference images: grass and sand establish the game's texture style, "
    "trail establishes the earth tones, inn establishes the organic earthy art style.\n"
)

MATERIAL_PROMPTS = {
    "boulders": PROMPT_HEADER + (
        "Material: Boulders, an impassable field of rounded grey-brown stones. Mostly medium and large "
        "weathered boulders, each a soft lumpy dome with a gentle highlight on the upper left and a short "
        "dark contact shadow to the lower right, packed with narrow gaps of dark gravel and dust between "
        "them. About 70% stone and 30% shaded gravel. Stones vary in size but none dominates; keep the "
        "field even so any crop reads as boulders. Cool desaturated greys with a little warm dust, no "
        "moss, no vegetation, no fitted paving, no cracks drawn as a network."
    ),
    "hedge": PROMPT_HEADER + (
        "Material: Hedge, a dense impassable mass of clipped foliage seen from directly above. Tight "
        "rounded leaf clumps in dark saturated green, slightly lighter domes where clumps catch light, "
        "deep shadowed gaps between clumps, a few tiny twig ends, no flowers, no trunks, no ground "
        "visible. About 85% leaf clumps and 15% dark gaps. Keep clumps small and even so any crop reads "
        "as hedge; no outline, no trimmed edge, no wall or fence."
    ),
    "thicket": PROMPT_HEADER + (
        "Material: Thicket, an impassable tangle of woody scrub viewed from above. Dark olive-green "
        "undergrowth with many thin brown and grey-brown branches and twigs crossing in every direction, "
        "sparse small dull leaves, deep shadow beneath the tangle. About 55% branches and twigs, 45% "
        "shadowed undergrowth. Twigs are a few pixels wide after downscaling: thick and bold rather than "
        "hairline. No thorns drawn as spikes, no flowers, no visible soil patches, no tree trunks."
    ),
    "lava": PROMPT_HEADER + (
        "Material: Lava, a cooling lava field viewed from above. Dark red-brown to charcoal crust plates "
        "with slightly rounded edges, separated by a network of thin glowing channels of molten rock in "
        "deep orange and warm yellow. About 75% dark crust and 25% glowing channels; channels are two to "
        "four pixels wide after downscaling and branch irregularly, never as a regular honeycomb. The "
        "crust carries faint warm reflected light near the channels. No flames, no smoke, no bubbles, "
        "no rocks sitting on top. The glow animation is added procedurally; paint the brightest glow at "
        "a steady medium intensity."
    ),
    "ember_field": PROMPT_HEADER + (
        "Material: Ember field, a scorched dark crust viewed from above, dotted with small glowing "
        "embers. Charcoal and dark red-brown ash and cinder with fine cracks, about 3% of the surface "
        "covered by small round embers in orange with warm yellow cores, scattered evenly, no large "
        "molten pools or channels. Embers are two to three pixels across after downscaling. No flames, "
        "no smoke, no rocks, no vegetation. The ember pulse is added procedurally; paint embers at a "
        "steady medium intensity."
    ),
    "flower_meadow": PROMPT_HEADER + (
        "Material: Flower meadow, a wild meadow viewed from above. Soft mid-green grass with painterly "
        "mottling, scattered small round flower heads in pastel pink, pale yellow and lilac, about 3% of "
        "the surface, each one or two pixels across after downscaling, spread evenly without clumping "
        "into rows or rings. Flowers slightly lighter than the grass; no stems drawn, no large blooms, "
        "no bare soil, no path, no border."
    ),
    "outcrop": PROMPT_HEADER + (
        "Material: Outcrop, impassable bare bedrock viewed from above. Four to six large flat slabs of "
        "grey stone per 128 pixel region, each slab a slightly different warm or cool grey, bevelled "
        "along its rim with a gentle highlight on the upper left, separated by narrow dark fissures. Fine "
        "grit on the slab surfaces and a few small patches of yellow-green lichen. About 85% slab, 10% "
        "fissure and 5% lichen. No boulders sitting on the surface, no grass, no cracks drawn as a "
        "regular grid."
    ),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_source(root, name):
    path = root / "datasrc/gfx" / name / "material.png"
    require(path.is_file(), f"missing {path.relative_to(root)}")
    with Image.open(path) as image:
        require(image.width == image.height, f"{path.name}: material must be square")
        require(image.width >= MIN_SOURCE, f"{path.name}: material must be at least {MIN_SOURCE} pixels")
        return path, image.convert("RGBA")


def sheet_tiles(source):
    """Area-average the generated material to the 128x128 sheet and cut it."""
    return sheet_to_tiles(box_down(source, SHEET))


def glow_mask(tile):
    """Warm, bright pixels of an imported lava or ember tile, 0..1 per pixel."""
    pixels = tile.load()
    mask = []
    for y in range(tile.height):
        for x in range(tile.width):
            r, g, b, _ = pixels[x, y]
            warmth = (r - b) / 255
            brightness = luma(r, g, b) / 255
            value = max(0.0, min(1.0, (warmth - 0.2) * 3)) * max(0.0, min(1.0, (brightness - 0.18) * 3))
            mask.append(value)
    return mask


def phase_offsets(name, size):
    """A smooth periodic offset field so neighbouring glow pixels pulse together."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import terrain_synth  # noqa: E402

    rng = random.Random(fnv1a32(f"{name}:glow"))
    field = terrain_synth.lattice_noise(rng, 3)
    step = terrain_synth.N // size
    return [field[(y * step) * terrain_synth.N + x * step] for y in range(size) for x in range(size)]


def animate_glow(name, tiles):
    """Four phases: the recipe's glow ramp applied to the imported warm pixels."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import terrain_synth  # noqa: E402

    recipe = terrain_synth.RECIPES[name]
    palette = recipe.palette
    dim = palette.get("glow", palette.get("ember"))
    bright = palette.get("glow_bright", palette.get("ember_core"))
    offsets = phase_offsets(name, tiles[0].width)
    masks = [glow_mask(tile) for tile in tiles]
    phases = []
    for phase in range(GLOW_PHASES):
        t = phase / GLOW_PHASES
        frames = []
        for tile, mask in zip(tiles, masks):
            out = tile.copy()
            pixels = out.load()
            size = tile.width
            for i, m in enumerate(mask):
                if m <= 0:
                    continue
                x, y = i % size, i // size
                pulse = 0.5 + 0.5 * math.sin(2 * math.pi * (t + offsets[i]))
                target = [a + (b - a) * pulse for a, b in zip(dim, bright)]
                r, g, b, a = pixels[x, y]
                weight = m * 0.8
                pixels[x, y] = tuple(
                    int(round(c + (tc - c) * weight)) for c, tc in zip((r, g, b), target)
                ) + (a,)
            frames.append(out)
        phases.append(frames)
    return phases


def require_phase_mode(name, animate):
    """An animated recipe must be exported with --animate-glow, and vice versa.

    Otherwise sixteen frames would be written while the catalog's
    `animation_frames` still references stale frames 16..63 (or the reverse).
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import terrain_synth  # noqa: E402

    recipe = terrain_synth.RECIPES.get(name)
    phases = recipe.phases if recipe else 1
    if phases > 1 and not animate:
        raise ValueError(f"{name}: the recipe has {phases} phases; export it with --animate-glow")
    if phases == 1 and animate:
        raise ValueError(f"{name}: the recipe is not animated; drop --animate-glow")


def export_frames(name, source, animate=False):
    """All frames in catalogue order (`variant + 16 * phase`), perimeter shared."""
    require_phase_mode(name, animate)
    tiles = sheet_tiles(source)
    if not animate:
        return share_perimeter(tiles)
    frames = []
    for phase_tiles in animate_glow(name, tiles):
        frames.extend(share_perimeter(phase_tiles))
    return frames


def provenance_document(name, source_path, frames, hashes, root, animate):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import terrain_synth  # noqa: E402

    recipe = terrain_synth.RECIPES.get(name)
    previous = root / "datasrc/gfx" / name / "provenance.json"
    replaces = None
    if previous.is_file():
        record = json.loads(previous.read_text())
        if record.get("method") == "procedural":
            replaces = {
                "method": "procedural",
                "generator": record.get("generator"),
                "generator_sha256": record.get("generator_sha256"),
                "provenance_sha256": sha256_file(previous),
            }
        elif "replaces" in record:
            replaces = record["replaces"]
    phases = GLOW_PHASES if animate else 1
    document = {
        "generator": "Built-in image_gen tool",
        "method": "hybrid" if animate else "image-generator",
        "exporter": "tools/artwork/export_material.py",
        "exporter_sha256": sha256_file(Path(__file__).resolve()),
        "material": name,
        "group": recipe.group if recipe else None,
        "label": recipe.label if recipe else None,
        "prompt": MATERIAL_PROMPTS.get(name),
        "source": "material.png",
        "source_sha256": sha256_file(source_path),
        "references": list(REFERENCES),
        "reference_sha256": {ref: sha256_file(root / ref) for ref in REFERENCES if (root / ref).is_file()},
        "recipe": (
            'encoder_python="$(python3 tools/package_assets.py --encoder-python)"; '
            f'"$encoder_python" tools/artwork/export_material.py --name {name}'
            + (" --animate-glow" if animate else "")
            + f'; "$encoder_python" tools/artwork/validate_material.py --name {name}'
        ),
        "sprite": f"{FRAME_PREFIX}{name}",
        "variants": VARIANTS,
        "phases": phases,
        "frame_layout": "variant + 16 * phase",
        "glow": (
            {"phases": GLOW_PHASES, "ramp": "terrain_synth.RECIPES[name].palette glow ramp", "mask": "warm bright pixels"}
            if animate else None
        ),
        "style_stats": style_stats(frames[:VARIANTS]),
        "pillow": PIL.__version__,
        "platform": {"system": sys.platform, "machine": __import__("platform").machine()},
        "runtime_sha256": hashes,
    }
    if replaces:
        document["replaces"] = replaces
    return document


def export(name, root=ROOT, animate=False, hd=False):
    source_path, source = load_source(root, name)
    frames = export_frames(name, source, animate)
    hashes = write_frames(frames, f"{FRAME_PREFIX}{name}", 0, root)
    document = provenance_document(name, source_path, frames, hashes, root, animate)
    (root / "datasrc/gfx" / name / "provenance.json").write_text(json.dumps(document, indent=2) + "\n")
    if hd:
        out = root / "artifacts/terrain/hd"
        out.mkdir(parents=True, exist_ok=True)
        sheet = box_down(source, SHEET)
        for i, tile in enumerate(sheet_to_tiles(sheet)):
            tile.save(out / f"terrain-{name}{i}.png")
    return frames, document


def check(name, root=ROOT, animate=False):
    """Re-export in memory and compare with the committed frames and provenance."""
    source_path, source = load_source(root, name)
    provenance_path = root / "datasrc/gfx" / name / "provenance.json"
    require(provenance_path.is_file(), f"missing {provenance_path.relative_to(root)}")
    record = json.loads(provenance_path.read_text())
    require(record.get("method") in ("image-generator", "hybrid"),
            f"{name}: provenance method is {record.get('method')!r}, not an image-generator export")
    require(record.get("source_sha256") == sha256_file(source_path), f"{name}: material.png differs from provenance")
    animate = animate or record.get("method") == "hybrid"
    require_phase_mode(name, animate)
    frames = export_frames(name, source, animate)
    expected = [f"{FRAME_PREFIX}{name}{i}.png" for i in range(len(frames))]
    require(list(record.get("runtime_sha256", {})) == expected, f"{name}: provenance frame list differs")
    for relative, frame in zip(expected, frames):
        with Image.open(root / relative) as committed:
            digest = pixel_sha256(committed)
        require(digest == pixel_sha256(frame), f"{relative}: committed pixels differ from a fresh export")
        require(digest == record["runtime_sha256"][relative], f"{relative}: provenance hash differs")
    return len(frames)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", required=True, help="material key, e.g. boulders")
    parser.add_argument("--hd", action="store_true", help="also write the 128x128 sheet tiles under artifacts/terrain/hd/")
    parser.add_argument("--check", action="store_true", help="compare committed frames with a fresh export")
    parser.add_argument("--prompt", action="store_true", help="print the stored generation prompt")
    parser.add_argument("--animate-glow", action="store_true", help="four procedural glow phases (lava, ember_field)")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    if args.prompt:
        prompt = MATERIAL_PROMPTS.get(args.name)
        if prompt is None:
            parser.error(f"no stored prompt for {args.name}; prompts exist for {', '.join(MATERIAL_PROMPTS)}")
        print(prompt)
        return
    if args.check:
        count = check(args.name, root, args.animate_glow)
        print(f"PASS {args.name}: {count} committed frames match material.png and provenance")
        return
    frames, document = export(args.name, root, args.animate_glow, args.hd)
    stats = document["style_stats"]
    print(f"Exported {len(frames)} frames for {args.name} ({document['method']}); "
          f"luma {stats['luma']} std {stats['std']} grain {stats['grain']} sat {stats['sat']}")


if __name__ == "__main__":
    main()
