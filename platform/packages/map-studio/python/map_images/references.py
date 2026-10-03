# SPDX-License-Identifier: GPL-3.0-or-later
"""Render exact native references and compose the final image prompt.

Pillow is needed only for reference preparation, not for catalog/selection.
Generated examples retain the engine's categorical colors and colony markers;
nearest-neighbor scaling preserves each source cell. Image generation itself
is deliberately a separate provider operation.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess

from .common import digest, glob2_source, nearest_resampling, write_json
from .prompts import image_prompt
from .selection import MAX_EXAMPLE_COUNT, validate_selection


def reference_sheets(references, out):
    """Pack up to ten examples into at most five input files, retaining every tile."""
    from PIL import Image, ImageDraw

    if not 1 <= len(references) <= MAX_EXAMPLE_COUNT:
        raise ValueError(f"Reference sheets need one to {MAX_EXAMPLE_COUNT} examples")
    out = Path(out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    sheets = []
    for start in range(0, len(references), 2):
        group = references[start : start + 2]
        canvas = Image.new("RGB", (2048, 2048), "#202020")
        draw = ImageDraw.Draw(canvas)
        for offset, item in enumerate(group):
            draw.text(
                (1024 * offset + 8, 480),
                f"Reference {start + offset + 1}: {item['generator_id']}",
                fill="white",
            )
            # Each original quadrant represents 256 cells. Reducing its 1024px
            # quadrant to 512px leaves two pixels per cell and retains all data.
            with Image.open(item["path"]) as source:
                if source.size != (2048, 2048):
                    raise ValueError("Reference sheet needs an exact 2048px mosaic")
                image = source.convert("RGB")
            canvas.paste(
                image.resize((1024, 1024), nearest_resampling()), (1024 * offset, 512)
            )
        target = out / f"reference-sheet-{start // 2 + 1}.png"
        canvas.save(target)
        sheets.append(
            {
                "path": str(target),
                "sha256": digest(target),
                "example_ids": [item["generator_id"] for item in group],
            }
        )
    return sheets


def prepare(args):
    """Render a saved selection offline, preserving each native map and its logs."""
    from PIL import Image

    plan = args.selection_dir.resolve()
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError("Prepared output must be a new or empty directory")
    catalog = json.loads((plan / "catalog.json").read_text(encoding="utf-8"))
    bindings = json.loads((plan / "selection-inputs.json").read_text(encoding="utf-8"))
    for name in ("concept", "catalog"):
        path = plan / (name + (".txt" if name == "concept" else ".json"))
        if digest(path) != bindings[name + "_sha256"]:
            raise ValueError(f"Selection's {name} input changed; rerun selection")
    # Legacy saved selections lack this binding; retain their offline usability.
    # New selections are immutable inputs just like their concept and catalog.
    if (
        "selection_sha256" in bindings
        and digest(plan / "selection.json") != bindings["selection_sha256"]
    ):
        raise ValueError("Selection input changed; rerun selection")
    selection = validate_selection(
        json.loads((plan / "selection.json").read_text(encoding="utf-8")),
        catalog,
        bindings["count"],
    )
    binary = args.binary.resolve()
    if digest(binary) != catalog["binary_sha256"]:
        raise ValueError("Generate references with the same binary used for selection")
    repo = glob2_source()
    entries = {entry["id"]: entry for entry in catalog["generators"]}
    out.mkdir(parents=True, exist_ok=True)
    references = []
    for example in selection["examples"]:
        identifier = example["generator_id"]
        folder = out / identifier
        folder.mkdir()
        # Failed defaults/seeds stay visible; no generator substitutions or quiet retries.
        command = [
            str(binary),
            "--generate-map",
            identifier,
            "--seed",
            str(args.seed),
            "--width",
            "256",
            "--height",
            "256",
            "--teams",
            "4",
            "--output",
            str(folder / "map.map"),
            "--preview",
            str(folder / "preview.png"),
            "--map-image",
            str(folder / "tile.png"),
            "--json",
            str(folder / "report.json"),
        ]
        write_json(folder / "command.json", command)
        result = subprocess.run(
            command,
            cwd=repo,
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
            env=dict(os.environ, GLOB2_USER_DIR=str(folder / "profile")),
        )
        (folder / "stdout.log").write_text(result.stdout, encoding="utf-8")
        (folder / "stderr.log").write_text(result.stderr, encoding="utf-8")
        if result.returncode:
            raise ValueError(
                f"{identifier} refused seed {args.seed}; inspect {folder}; retry explicitly"
            )
        with Image.open(folder / "tile.png") as source:
            if source.size != (256, 256):
                raise ValueError(f"Unexpected categorical tile size {source.size}")
            tile = source.convert("RGB")
        # Do not add colony symbols or recolor/rewrite the generated world.
        tile = tile.resize((1024, 1024), nearest_resampling())
        mosaic = Image.new("RGB", (2048, 2048))
        for x, y in ((0, 0), (1024, 0), (0, 1024), (1024, 1024)):
            mosaic.paste(tile, (x, y))
        target = folder / "reference-2x2.png"
        mosaic.save(target)
        references.append(
            {
                "generator_id": identifier,
                "seed": args.seed,
                "path": str(target),
                "sha256": digest(target),
                "map_sha256": digest(folder / "map.map.gz"),
            }
        )
    sheets = reference_sheets(references, out)
    concept = (plan / "concept.txt").read_text(encoding="utf-8").strip()
    prompt = image_prompt(concept, selection, entries)
    (out / "image-prompt.txt").write_text(prompt, encoding="utf-8")
    write_json(
        out / "image-inputs.json",
        {
            "concept": concept,
            "selection": selection,
            "references": references,
            "reference_sheets": sheets,
            "prompt_sha256": digest(out / "image-prompt.txt"),
            "binary_sha256": digest(binary),
            "selection_sha256": digest(plan / "selection.json"),
            "selection_inputs_sha256": digest(plan / "selection-inputs.json"),
            "catalog_sha256": digest(plan / "catalog.json"),
            "concept_sha256": digest(plan / "concept.txt"),
        },
    )
    print(
        json.dumps(
            {
                "prompt": str(out / "image-prompt.txt"),
                "reference_sheets": [item["path"] for item in sheets],
            },
            indent=2,
        )
    )
