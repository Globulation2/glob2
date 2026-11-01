# SPDX-License-Identifier: GPL-3.0-or-later
"""Choose a torus tile from a generated 2x2 categorical map mosaic.

This is a seam *selection*, not a repair. It scores terrain and resource types
at the prospective wrap boundary and at any original mosaic joins retained
inside the crop. Four complete colony markers must survive. The score does not
establish playable routes, sustainable economies, or exact repetition.

Coordinates in the search are native map cells. Pixel conversion happens only
when decoding the image and writing the crop. Keeping those units separate
prevents accidentally extracting a quarter-size map from a 2x2 mosaic.
"""

from __future__ import annotations

from pathlib import Path

from .common import nearest_resampling

# Order matches the engine's categorical map-image palette. Equal-distance and
# equal-vote ties deliberately prefer the earlier entry, as the importer does.
COLORS = (
    (0, 128, 0),
    (240, 220, 140),
    (0, 64, 255),
    (0, 64, 0),
    (255, 255, 0),
    (128, 128, 128),
    (0, 255, 255),
    (255, 0, 255),
    (255, 0, 0),
    (255, 128, 0),
    (128, 0, 255),
    (255, 255, 255),
)
MOSAIC_PIXELS = 2048
GRID_CELLS = 512
TILE_CELLS = GRID_CELLS // 2
PIXELS_PER_CELL = MOSAIC_PIXELS // GRID_CELLS
SEAM_BAND_CELLS = 4
COLONY_COLOR = 11
MIN_MARKER_CELLS = 4
COLONIES_PER_TILE = 4


def decode(path: Path, *, normalize: bool = False):
    """Quantize each pixel, then majority-vote each 4x4 native cell."""
    from PIL import Image

    with Image.open(path) as source:
        if normalize:
            if source.width != source.height:
                raise ValueError(
                    f"Repeated map mosaic must be square, got {source.size}"
                )
        elif source.size != (MOSAIC_PIXELS, MOSAIC_PIXELS):
            raise ValueError(f"Expected a 2048×2048 edited mosaic, got {source.size}")
        # Native import rejects transparency. Reject it here too rather than
        # discarding alpha and turning invisible white pixels into colonies.
        if source.convert("RGBA").getextrema()[3] != (255, 255):
            raise ValueError("Map mosaic must be fully opaque")
        image = source.convert("RGB")
        if normalize and image.size != (MOSAIC_PIXELS, MOSAIC_PIXELS):
            image = image.resize((MOSAIC_PIXELS, MOSAIC_PIXELS), nearest_resampling())
    nearest = {}
    for _, color in image.getcolors(image.width * image.height):
        nearest[color] = min(
            range(len(COLORS)),
            key=lambda index: sum((a - b) ** 2 for a, b in zip(color, COLORS[index])),
        )
    data = (
        image.get_flattened_data()
        if hasattr(image, "get_flattened_data")
        else image.getdata()
    )
    pixels = bytes(nearest[pixel] for pixel in data)
    grid = []
    for y in range(GRID_CELLS):
        for x in range(GRID_CELLS):
            counts = [0] * len(COLORS)
            for dy in range(PIXELS_PER_CELL):
                start = (y * PIXELS_PER_CELL + dy) * MOSAIC_PIXELS + x * PIXELS_PER_CELL
                for dx in range(PIXELS_PER_CELL):
                    counts[pixels[start + dx]] += 1
            grid.append(max(range(len(COLORS)), key=lambda index: counts[index]))
    return image, grid


def marker_components(grid, width=GRID_CELLS, height=GRID_CELLS):
    """Return inclusive bounds of 8-connected, nontrivial white components.

    A colony marker can span several cells. Count components rather than white
    pixels, and ignore single-cell highlights using the established four-cell
    minimum. Components crossing an outer mosaic edge are intentionally separate:
    they would not be a whole marker in an ordinary square image crop.
    """
    seen = set()
    markers = []
    for index, value in enumerate(grid):
        if value != COLONY_COLOR or index in seen:
            continue
        component = [index]
        seen.add(index)
        for cell in component:
            x, y = cell % width, cell // width
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if 0 <= x + dx < width and 0 <= y + dy < height:
                        neighbor = (y + dy) * width + x + dx
                        if grid[neighbor] == COLONY_COLOR and neighbor not in seen:
                            seen.add(neighbor)
                            component.append(neighbor)
        if len(component) >= MIN_MARKER_CELLS:
            xs = [cell % width for cell in component]
            ys = [cell // width for cell in component]
            markers.append((min(xs), min(ys), max(xs), max(ys)))
    return markers


def choose_crop(
    grid,
    tile_width=TILE_CELLS,
    tile_height=TILE_CELLS,
    colonies=COLONIES_PER_TILE,
    pixels_per_cell=PIXELS_PER_CELL,
):
    width, height = tile_width * 2, tile_height * 2
    """Score every cell-aligned tile and return the stable legacy JSON report.

    Prefix sums reduce each candidate's seam evaluation to constant time.
    Terrain mismatch costs four, resource-type mismatch costs one. Four nested
    edge pairs discourage a crop whose first row matches only by accident. Ties
    prefer crops nearest the mosaic center, then smaller x and y coordinates.
    """
    if len(grid) != width * height or any(
        not isinstance(value, int) or not 0 <= value < len(COLORS) for value in grid
    ):
        raise ValueError("Unexpected categorical grid dimensions")
    terrain = [2 if value in (2, 6) else 1 if value == 1 else 0 for value in grid]
    resources = [value if 3 <= value <= 10 else -1 for value in grid]

    def mismatch(first, second):
        return 4 * (terrain[first] != terrain[second]) + (
            resources[first] != resources[second]
        )

    horizontal = []
    vertical = []
    for x in range(tile_width + 1):
        prefix = [0]
        for y in range(height):
            row = y * width
            prefix.append(
                prefix[-1]
                + sum(
                    mismatch(row + x + band, row + x + tile_width - 1 - band)
                    for band in range(SEAM_BAND_CELLS)
                )
            )
        horizontal.append(prefix)
    for y in range(tile_height + 1):
        prefix = [0]
        for x in range(width):
            prefix.append(
                prefix[-1]
                + sum(
                    mismatch(
                        (y + band) * width + x, (y + tile_height - 1 - band) * width + x
                    )
                    for band in range(SEAM_BAND_CELLS)
                )
            )
        vertical.append(prefix)

    # Score original central joins too. Moving a discontinuity from the crop's
    # boundary into its interior must not make it disappear from the objective.
    join_vertical = [0]
    join_horizontal = [0]
    for position in range(height):
        join_vertical.append(
            join_vertical[-1]
            + mismatch(position * width + tile_width - 1, position * width + tile_width)
        )
    for position in range(width):
        join_horizontal.append(
            join_horizontal[-1]
            + mismatch(
                (tile_height - 1) * width + position, tile_height * width + position
            )
        )

    markers = marker_components(grid, width, height)
    ranked = []
    for y in range(tile_height + 1):
        for x in range(tile_width + 1):
            overlapping = [
                marker
                for marker in markers
                if marker[0] < x + tile_width
                and marker[2] >= x
                and marker[1] < y + tile_height
                and marker[3] >= y
            ]
            if len(overlapping) != colonies or any(
                marker[0] < x
                or marker[2] >= x + tile_width
                or marker[1] < y
                or marker[3] >= y + tile_height
                for marker in overlapping
            ):
                continue
            cost = (
                horizontal[x][y + tile_height]
                - horizontal[x][y]
                + vertical[y][x + tile_width]
                - vertical[y][x]
            )
            if 0 < x < tile_width:
                cost += join_vertical[y + tile_height] - join_vertical[y]
            if 0 < y < tile_height:
                cost += join_horizontal[x + tile_width] - join_horizontal[x]
            center_x, center_y = tile_width // 2, tile_height // 2
            ranked.append((cost, (x - center_x) ** 2 + (y - center_y) ** 2, x, y))
    ranked.sort()
    if not ranked:
        raise ValueError("No crop with exactly four uncut marker components")
    cost, _, x, y = ranked[0]
    return {
        "pixel_origin": [pixels_per_cell * x, pixels_per_cell * y],
        "score": cost,
        "candidates_scored": (tile_width + 1) * (tile_height + 1),
        "valid_candidates": len(ranked),
        "global_marker_components": len(markers),
        "criterion": "Four-cell terrain mismatch weight4 plus resource-type mismatch weight1; "
        "includes known internal joins, not only crop edges. Four whole colony "
        "markers required. Heuristic score, not playability certification.",
        "top_30": ranked[:30],
    }


def validate_prepared_inputs(path: Path):
    """Bind a crop run to the prompt/reference files supplied by preparation.

    This records provenance, not an attestation that an external image provider
    followed those inputs. Refuse changed prompt or sheet bytes before writing
    an accepted crop, so experiments cannot silently mix preparation runs.
    """
    import json
    from .common import digest

    manifest = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or not isinstance(
        manifest.get("reference_sheets"), list
    ):
        raise ValueError("Invalid prepared image-inputs manifest")
    prompt = path.parent / "image-prompt.txt"
    if digest(prompt) != manifest.get("prompt_sha256"):
        raise ValueError("Prepared image prompt changed")
    sheets = manifest["reference_sheets"]
    if not sheets:
        raise ValueError("Prepared input manifest has no reference sheets")
    sheet_paths = []
    for sheet in sheets:
        if not isinstance(sheet, dict) or not isinstance(sheet.get("path"), str):
            raise ValueError("Invalid prepared reference sheet")
        target = Path(sheet["path"])
        if not target.is_absolute():
            target = path.parent / target
        if digest(target) != sheet.get("sha256"):
            raise ValueError(f"Prepared reference sheet changed: {target}")
        sheet_paths.append(str(target.resolve()))
    return {
        "path": str(path.resolve()),
        "sha256": digest(path),
        "prompt_sha256": manifest["prompt_sha256"],
        "prompt_path": str(prompt.resolve()),
        "reference_sheet_paths": sheet_paths,
        "reference_sheet_sha256": [sheet["sha256"] for sheet in sheets],
    }


def crop_image(
    source: Path,
    output: Path,
    report_path: Path,
    *,
    normalize: bool = False,
    require_repeated_map: bool = False,
    overlay: Path | None = None,
    inputs: Path | None = None,
):
    """Write a 1024px tile from the full normalized mosaic, with crop evidence.

    Normalization is explicit: it accepts any square source resolution and
    scales the *whole* mosaic to the established 2048px search representation.
    The repeated-map contract rejects extra/missing marker components before
    producing an accepted tile. It is a necessary input check, not proof that
    all four quadrants are identical. Old strict-resolution callers retain
    their existing report fields and acceptance behavior.
    """
    import hashlib
    from PIL import Image, ImageDraw
    from .common import digest, write_json

    destinations = [output, report_path] + ([overlay] if overlay is not None else [])
    paths = [source.resolve()] + [path.resolve() for path in destinations]
    if inputs is not None and inputs.resolve() in paths[1:]:
        raise ValueError("Prepared input manifest may not be overwritten")
    if len(set(paths)) != len(paths):
        raise ValueError("Input, crop, report and overlay must have distinct paths")
    provenance = validate_prepared_inputs(inputs) if inputs is not None else None
    if provenance is not None:
        protected = {Path(provenance["prompt_path"])} | {
            Path(path) for path in provenance["reference_sheet_paths"]
        }
        if protected.intersection(paths[1:]):
            raise ValueError(
                "Prepared prompt and reference sheets may not be overwritten"
            )
    with Image.open(source) as original:
        original_size = list(original.size)
    image, grid = decode(source, normalize=normalize)
    if require_repeated_map:
        count = len(marker_components(grid))
        expected = COLONIES_PER_TILE * 4
        if count != expected:
            raise ValueError(
                f"Repeated map mosaic needs exactly {expected} colony marker "
                f"components, found {count}"
            )
    report = choose_crop(grid)
    x, y = report["pixel_origin"]
    size = TILE_CELLS * PIXELS_PER_CELL
    if normalize or require_repeated_map or overlay is not None or inputs is not None:
        # Pixel hash excludes PNG encoder metadata and records the exact search
        # input, even when the source was normalized from another resolution.
        report["input"] = {
            "path": str(source.resolve()),
            "sha256": digest(source),
            "original_size": original_size,
            "normalized_size": list(image.size),
            "normalized_rgb_sha256": hashlib.sha256(image.tobytes()).hexdigest(),
            "resampling": "nearest" if normalize else "none",
            "required_marker_components": (
                COLONIES_PER_TILE * 4 if require_repeated_map else None
            ),
        }
    if provenance is not None:
        report["prepared_inputs"] = provenance
    output.parent.mkdir(parents=True, exist_ok=True)
    image.crop((x, y, x + size, y + size)).save(output)
    if overlay is not None:
        annotation = image.copy()
        ImageDraw.Draw(annotation).rectangle(
            (x, y, x + size - 1, y + size - 1), outline="#FF0000", width=4
        )
        overlay.parent.mkdir(parents=True, exist_ok=True)
        annotation.save(overlay)
        report["crop_overlay"] = str(overlay.resolve())
    report_path.parent.mkdir(parents=True, exist_ok=True)
    write_json(report_path, report)
    return report
