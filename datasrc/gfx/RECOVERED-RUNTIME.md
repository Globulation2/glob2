# Recovered artwork in the runtime pack

Ten frames from Stéphane’s second archive now replace experimental upscales.
There is no AI step in these exports. The first archive’s layered building and
resource exports are a separate, still-pending migration; other runtime frames
keep their reviewed experimental versions for now.

| Runtime frame | Historical render | Native canvas | Logical canvas |
| --- | --- | --- | --- |
| `swarm0b0` | `Ruche.png` | 256 × 256 | 128 × 128 |
| `swarm0c0` | `Morph128c.png` | 512 × 512 | 128 × 128 |
| `warflag0` | `WarFlag.png` | 128 × 128 | 32 × 32 |
| `explorationflag0` | `ExploFlag.png` | 128 × 128 | 32 × 32 |
| `clearingflag0` | `RemoveFlag.png` | 128 × 128 | 32 × 32 |
| `buildingsite1` | `Construction64c/m.png` | 256 × 256 | 64 × 64 |
| `buildingsite2` | `Construction96c/m.png` | 384 × 384 | 96 × 96 |
| `buildingsite3` | `Construction128c/m.png` | 512 × 512 | 128 × 128 |
| `buildingsite4` | `Construction192c/m.png` | 384 × 384 | 192 × 192 |
| `buildingsite5` | `Construction256c/m.png` | 512 × 512 | 196 × 196 |

The runtime pack uses 4× texture dimensions uniformly. Some recovered renders
provide only 2×–2.6× native resolution; resizing those to the pack’s canvas does
not invent additional detail. All sprites retain the engine’s logical dimensions.

## Reproduction

Developer dependencies: Python, Pillow, NumPy and SciPy. Runtime loading requires
only the committed PNG files and `data/highres/v1/frames.txt`.

```sh
python3 tools/artwork/import_more_originals.py .cache/original-art/glob2-highres-more.zip
python3 tools/artwork/export_recovered.py
python3 experiments/ai-upscale/export_runtime.py
python3 tools/artwork/validate_recovered.py
python3 experiments/ai-upscale/validate_runtime.py
python3 experiments/ai-upscale/pr_comparisons.py
```

`export_recovered.py` reads unchanged historical renders, writes derived PNGs and
source hashes to `derived/recovered-v1/`, and records native sizes and recipes.
`runtime_overrides.py` validates source/output hashes, dimensions and all paired
layers before copying complete replacements into the pack. Export errors abort;
the engine retains its existing whole-frame fallback for invalid runtime files.
The generated derived PNGs are committed so rebuilding the main runtime pack does
not require image editing applications or this source export step.

Construction color renders use their matching grayscale matte for alpha. White
background contamination is removed before premultiplied-alpha Lanczos resizing.
Hives and flags use their native green chroma to separate the recolorable object
from neutral projected shadows. Interior highlights remain opaque; boundary
pixels have the white matte removed. Shadows use the classic hive’s subdued RGB
(65, 66, 69), capped at alpha 144, on the recovered silhouette. No generated
geometry or old low-resolution team mask is mixed into these replacements.

## Coverage limits

- `buildingsite0` remains on the existing upscale: none of these renders matches
  its small loose pile. `Construction160` is a different arrangement, retained
  without assigning an unverified runtime frame.
- `Morph128fn.png` is a black-background reference of the hive construction state;
  the white-background render is the selected source.
- The 16 direction templates are 32-pixel reference shapes, not HD replacements.
- Walls and missing building states retain the current fallback artwork. The
  old tower Blender scene also references unavailable `tower-2-tex.png`.

Validation checks source hashes, logical sizes, native-to-classic silhouette
registration, transparent backgrounds, subdued shadows and rejection of an
incomplete base/team pair. The engine pack check renders all 487 frames in all
16 team hues and checks OpenGL errors, bounded caches and resource release.
