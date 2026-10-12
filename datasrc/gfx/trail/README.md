# Trail classic terrain source

`material.png` is an AI-generated, opaque overhead earth material made with the
built-in image generator. Grass, sand, the former cobblestone tile and an inn
were supplied as style references. The selected treatment uses desaturated
clay-brown earth, fine grit and scattered embedded stones; it depicts neither
constructed paving nor harvestable vegetation. It replaces classic artwork
only, independently of the original-derived HD pack.

The exact generation prompt and source hash are in `provenance.json`. The source
is retained because image generation itself is not reproducible from a prompt;
the conversion from this selected image to runtime PNGs is deterministic.

With Pillow 12.2.0 installed (`tools/asset-requirements.txt`), run from the
repository root. The asset pipeline's pinned interpreter is also available via
`python3 tools/package_assets.py --encoder-python`:

```sh
encoder_python="$(python3 tools/package_assets.py --encoder-python)"
"$encoder_python" tools/artwork/export_trail.py
"$encoder_python" tools/terrain_borders.py
"$encoder_python" tools/artwork/validate_trail.py
```

The exporter area-averages the material into a 128×128 sheet and divides it into
sixteen opaque 32×32 tiles. A shared textured perimeter and one-pixel inward
blend allow arbitrary variants to meet without abrupt material seams. Output
slots remain `terrain288.png` through `terrain303.png`. The legacy border recipe, retained for provenance, uses
the first variant to create fifteen transparent four-side masks in slots
319–333, with coordinate-dependent fraying inside each receiving cell. It also
reproduces the existing ice masks unchanged.

Detailed terrain now uses the material catalog and shared compositor described
in [terrain authoring](../../../docs/assets/terrain-materials.md); the retained
four-side sprites are not used by that renderer.

These sprite slots, terrain ID 4, legacy external name `road`, experiment key
`road-terrain`, and import/export colors remain stable. Gameplay properties are
independent of these textures. No high-resolution artwork is added.

`validate_trail.py` reads the installed assets without rewriting them. It checks
source and runtime provenance hashes, tile dimensions and opacity, distinct
variants, every pair of horizontal and vertical joins, and the fifteen edge
masks' coverage and cell bounds. After intentionally revising artwork, update
the source/runtime hashes in `provenance.json` with the selected outputs.

Related: [artwork production](../../../docs/assets/artwork-workflow.md).
