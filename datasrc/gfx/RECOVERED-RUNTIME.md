> Original-only PR: only the 60 verified original-derived frames ship here.
> References below to approved upscales describe the separate follow-up PR;
> all unavailable frames use classic artwork in this branch.

# Recovered artwork in the runtime pack

Ten frames from Stéphane’s second archive now replace experimental upscales.
Ten tree frames and eight wheat frames from the first archive are also now incorporated. There is no AI
step in these exports. The school and first two racetracks are now exported too. Other buildings and
the remaining resource families have no verified matching larger sources; other runtime frames keep their reviewed experimental versions for now.

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
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_recovered.py
python3 tools/artwork/validate_runtime.py
python3 tools/artwork/pr_comparisons.py
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


## Trees from the first archive

All `ressource0`–`ressource9` frames now use the five original tree XCFs:

| Frames | Original | Native canvas |
| --- | --- | --- |
| 0 | `trees_1_1.xcf` | 128 × 128 |
| 5 | `trees_1_2.xcf` | 128 × 128 |
| 1, 6 | `trees_2_1.xcf` | 128 × 128 |
| 2, 7 | `trees_3_1.xcf` | 128 × 140 |
| 3, 4, 8, 9 | `trees_4_1.xcf` | 128 × 140 |

GIMP exports the original visible layers with their saved opacity/modes, including
translucent shadows. The native RGBA exports and their layer/source hashes are
committed under `derived/tree-native/`. A premultiplied-alpha Lanczos resize fits
each existing logical canvas at 4×, under `derived/trees-v1/`. There is no color
restoration, AI processing or replacement of native alpha with the old pixel mask.
Native detail is approximately 3.2×–4× the classic canvases. The exporter builds
resource atlas mip levels from the final overridden files, with independent edge
padding, so batching and zoom use the same originals as standalone rendering.

To reproduce native exports, run from the repository root with GIMP 2.10:

```sh
gimp-console -n -i -d -f -c --batch-interpreter=python-fu-eval \
  -b 'execfile("tools/artwork/export_trees_gimp.py")' -b 'pdb.gimp_quit(0)'
python3 tools/artwork/export_trees.py
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_trees.py
python3 tools/artwork/validate_runtime.py
```

Normal runtime-pack rebuilding uses the committed derived PNGs and does not
require GIMP. Validation checks all ten growth/variant mappings, original hashes,
logical canvases and silhouette overlap, and compares each exported atlas level
against the independently resized final sprite. Classic files remain unchanged.


## Wheat from the first archive

Eight matching frames use native 128 × 128 GIMP originals (approximately 3.76×
the classic 34 × 34 canvases): `wheat_1_1` through `wheat_4_1` map to
`ressource10`–`ressource13`, and `wheat_1_2` through `wheat_4_2` map to
`ressource15`–`ressource18`. Visible layers retain original opacity, shadows and
native alpha. Premultiplied-alpha Lanczos resizing fits the existing 136 × 136
runtime canvases. These are the colorful resource dots currently used by the game;
the separate historical corn-stalk style is not substituted.

**Missing source variants:** `ressource14` and `ressource19` have different final
ripe colors. Their historical PNGs are only 34 × 34, and the recovered XCFs have
no hidden layers for that color state. Those two frames retain their existing
upscales. We have not invented a recoloring recipe or mistaken the fourth stage
for the fifth.

Reproduce with GIMP 2.10 from the repository root, then the Python export:

```sh
gimp-console -n -i -d -f -c --batch-interpreter=python-fu-eval \
  -b 'execfile("tools/artwork/export_wheat_gimp.py")' -b 'pdb.gimp_quit(0)'
python3 tools/artwork/export_wheat.py
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_wheat.py
python3 tools/artwork/validate_runtime.py
```

Native files/layer metadata are committed under `derived/wheat-native/`; runtime
exports and source hashes are under `derived/wheat-v1/`. All eight masks retain
matching connected components, a center within half a logical pixel, and no more
than one logical pixel of boundary difference from classic antialiasing. The pack
validator checks their pixels and all four resource atlas mip levels independently.


## Layered buildings

Three completed building frames now use the original XCFs:

| Runtime frame | Source | Native canvas | Logical canvas | Base layers | Team layers |
| --- | --- | --- | --- | --- | --- |
| `school1b0` | `science.xcf` | 512 × 562 | 64 × 70 | 1–6, 8–10 | 0 |
| `racetrack0b0` | `building3r.xcf` | 192 × 192 | 128 × 128 | 1, 2 | 0 |
| `racetrack1b0` | `building10.xcf` | 256 × 256 | 192 × 192 | 1, 2 | 0 |

Layer indices follow GIMP’s top-to-bottom order. The selected groups preserve
saved blend modes, opacity and offsets. Each group is composited over an added
transparent layer so single-layer PNG exports correctly bake layer opacity. In
particular, the school team overlay remains capped at alpha 64 (25%); Lanczos
resizing is not allowed to increase that ceiling through ringing. All groups fit
the existing logical canvas at 4× without changing anchors or simulation geometry.
The source school has up to 8× native resolution; the racetracks provide only
1.5× and 1.33× native detail. Enlarging them to the uniform pack canvas adds no
invented detail. The original school supersedes the previously locked experimental
school image; the approved pool fallback stays byte-identical.

Reproduction (GIMP 2.10 followed by Python/Pillow):

```sh
gimp-console -n -i -d -f -c --batch-interpreter=python-fu-eval \
  -b 'execfile("tools/artwork/export_buildings_gimp.py")' -b 'pdb.gimp_quit(0)'
python3 tools/artwork/export_buildings.py
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_buildings.py
python3 tools/artwork/validate_runtime.py
```

`provenance/building-runtime-recipes.json` specifies the groups. Native composites
and source layer metadata live under `derived/building-native/`; final layers and
source/output hashes live under `derived/buildings-v1/`. Both layers must validate
before replacement. The game loads the committed runtime PNGs without GIMP/Python.

### Candidates retained as fallbacks

- Hospital `hopital1.xcf`: the saved-layer export is much paler than the classic
  sprite, with different basin/background treatment and no matching finished
  shadow. This export was not adopted.
- Defense tower `tower2.xcf`: saved layers reproduce crystal opacity but not the
  classic crystal color/texture treatment or full shadow. This export was not adopted.
- Mechanical inn `auberge-steam.xcf` and racetrack `course-steam.xcf`: large source
  drawings/material masks do not reproduce the complete colored base/team artwork.
- Damaged and construction variants: no verified matching layer combinations were
  found in these XCFs. Existing runtime upscales remain for those states.

These are differences observed between our exports and the classic sprites, not
claims about who edited the artwork or when. All recovered XCFs and classic game
assets remain byte-for-byte unchanged. No new AI generation or recoloring was used.


## Area markers

All 24 area marker frames now use original 128 × 128 images without resampling
or AI. Guard `g0`–`g7` map to `area-guard0`–`7`; clearing `h1`–`h8` map to
`area-clearing0`–`7`; forbidden `i0`–`i7` map to `area-forbidden0`–`7`.
Recovered `-archive-2026` variants are used, except `i4.png`, which the import
reused because its original bytes already existed in the repository. The full
phase sequence is retained, including repeated/symmetric forbidden pulse frames.
The forbidden sequence is not reordered by image similarity.

`tools/artwork/export_markers.py` writes `derived/markers-v1/` with source hashes
and native dimensions. Approved copies are in production/original-derived; package_runtime.py assembles them.
`tools/artwork/validate_markers.py` verifies every source/runtime pixel, logical
32 × 32 size, animation-frame coverage and alpha coverage relative to classic.
Native alpha coverage differs by less than 1.1% after accounting for scale.

```sh
python3 tools/artwork/export_markers.py
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_markers.py
```

## Follow-up: hospital/tower compositing audit

The direct GIMP XCF composite and our separately exported base/team reconstruction
agree within rounding: at most one 8-bit code value for the hospital, and three
for the tower (only five tower pixels differ by more than one). This rules out
splitting the selected groups as the cause of the large finishing differences.
Saved layer opacity and modes were preserved; originals were not resaved.

The smaller `tower1.xcf` reproduces the classic tower team layer’s visible pixels and alpha **exactly**, at
64 × 108 pixels. The larger `tower2.xcf` does not contain that same finished
color/texture treatment in its selected layers. RGB stored under fully transparent pixels is ignored. The smaller source therefore
confirms our export path can reproduce the classic artwork, but does not supply
higher-resolution crystal detail. The cause/history of the differences between
source files remains unknown; the hospital and tower runtime fallbacks are unchanged.

Reproduce the audit with GIMP 2.10 using
`tools/artwork/audit_building_compositing_gimp.py` through `python-fu-eval`, then
run `python3 tools/artwork/measure_building_compositing.py`. Disposable images stay
in `.cache/original-art/compositing-audit/`; measurements and source hashes are
recorded in [the audit manifest](provenance/building-compositing-audit.json).

## Papyrus and remaining resource/UI audit

All five papyrus stages (`ressource20`–`24`) now use layers 0–4 of
`originals/terrain/papyrus.xcf`, respectively named `0.4`, `0.55`, `0.7`,
`0.85`, and `full`. The historical file stays in its cataloged location even
though the artwork is a resource. Each 199 × 199 native composite is reduced
to 128 × 128 for the unchanged logical 32 × 32 canvas. Native alpha, offsets,
and saved opacity are retained; no AI or classic alpha mask is used.

Run `export_papyrus_gimp.py` with GIMP 2.10 as above, then
`export_papyrus.py`, the runtime exporter, and `validate_papyrus.py`.
Committed `derived/papyrus-native/` and `derived/papyrus-v1/` make the runtime
pack reproducible without GIMP. All four resource atlas mips use these exports.
Five stages overlap classic silhouettes by 83.6–95.7%, with centers within
0.58 logical pixels; fine leaf antialiasing differs from classic downsampling.

This brings original-source runtime coverage to **60 of 487 frames**.
The other 42 resource frames lack verified matching larger sources: wheat ripe
states 14/19, alternative papyrus 25–29, stones 30–39, algae 40–49 and fruit
50–64. The older corn-stalk sources are an alternative style, not replacements
for the current colorful wheat dots.

The remaining UI XCF composites use an older gold theme; the brush sources are
already 32 × 32. They are preserved without changing the current interface.
The recovered water/cloud raster variants are 512 × 512, equal to classic
canvas sizes, so they do not add native resolution. Cursor Blender sources
still need a dedicated render/dependency audit; they have not been declared
missing. The UI audit used temporary previews; no preview-generation tooling is retained.

Export scripts write staging files under derived/. They do not automatically replace approved production inputs; review and promote the selected outputs before packaging. Historical experiment tooling is not shipped.
