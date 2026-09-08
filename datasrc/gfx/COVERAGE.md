# Coverage and open questions

This is a source inventory, not a claim of complete runtime coverage. The
[building visual map](BUILDING-MAP.md) resolves identities using base and hidden
team layers compared with current game sprites. No AI
generation or runtime frame replacement was performed during this import.

| Family | Available originals | Remaining work / limitations |
| --- | --- | --- |
| Buildings | 16 recovered layered XCF files, 4 existing Blender models | All 16 XCF building identities/levels are visually matched; inspect export registration, base/team layers, shadows, damage and construction states. See BUILDING-MAP.md. |
| Units | 9 existing Blender files | Keep animation and higher-resolution rendering aligned with PR #201; migration paths are in the provenance manifest. |
| Resources | Tree and two wheat styles, including layered sources and historical PNGs | Separate styles deliberately; compare source canvas sizes and masks before choosing exports. No complete resource-family coverage claim. |
| Terrain | Layered water, historical water/cloud images and papyrus | No explicit grass/sand transition source set identified in the supplied archive. Water variants need visual matching and repeat-boundary checks. |
| Interface and cursor | Layered controls, vector arrows, headset and 14 cursor Blender files | Verify intended UI element and logical size; map zoom must not enlarge UI/cursors. |
| Area overlays | Guard/harvest vectors, layered guard/forbidden artwork and PNG references | Map historical names to engine frame IDs and preserve translucent layer behavior. |
| Concepts | 20 building concept images | Reference only; not automatically aligned, team-recolorable or export-ready game sprites. |

## Confirmed resolution limits

Some recovered building canvases are already small:

| Original file | Canvas |
| --- | --- |
| `auberge-goth-64.xcf` | 64 × 69 |
| `building8.xcf` | 64 × 69 |
| `science1.xcf` | 64 × 70 |
| `tower1.xcf` | 64 × 108 |

Other originals have substantially larger canvases, for example
`course-steam.xcf` at 1024 × 1024, `science.xcf` at 512 × 562,
`auberge-steam.xcf` at 384 × 384 and `tower2.xcf` at 256 × 430.
Canvas size alone does not establish usable sprite detail: padding, layer
content and logical frame dimensions still need checking.

## Mapping gaps to report before replacement

- Historical building identities are now mapped in [BUILDING-MAP.md](BUILDING-MAP.md).
  `__building12` and `__building19` are both third-level barracks variants; their
  final registration/state relationship remains unverified.
- Damage/construction, shared construction, swarm, flags and wall coverage is
  not yet established from the source layers. Do not label these missing solely
  because their modern runtime filenames are absent.
- The two wheat folders contain different artwork using the same `ressource`
  filenames. Those names are candidates for mapping, not authorization to pick
  one historical style over the other.
- Existing raster exports can be classic-resolution references. Their presence
  is not proof of an available high-resolution replacement.
- Embedded Blender texture dependencies and XCF layer registration still need
  inspection during export. The import verifies file preservation, not renderer
  compatibility or completeness of external dependencies.

Where a source is absent or too small, retain the existing experimental HD
upscale and report the gap. Classic assets remain available for classic/software
mode and load failures. Prefer a usable original model/vector/layer source over AI enhancement.

## Second archive: runtime replacements

Both hive states, all three flags, and `buildingsite1` through `buildingsite5`
now use recovered original renders. White backgrounds are removed deterministically;
construction uses the supplied matching mattes, and hive/flag team layers remain
separate from neutral shadows. See [recipes and limitations](RECOVERED-RUNTIME.md).
`buildingsite0` has no matching replacement here. The `Construction160` variant,
black-background hive variant and 16 classic-size direction templates are retained
as references. Walls remain unavailable. These findings supersede the initial
“not yet established” assessment above for hive/flag/shared construction coverage.

## Tree runtime migration

All ten tree frames now use five native GIMP sources. Saved layer opacity and
transparency are retained, and the four resource atlas levels use the final
original-source images. Eight wheat frames also now use native GIMP exports. Final ripe color states 14/19 retain their existing upscales because matching larger sources were not recovered; remaining resource families are assessed in the papyrus/UI audit below.

## Layered building runtime migration

The completed school (`school1b0`) and first two racetracks now use verified
original base/team groups. Hospital/tower candidate exports did not faithfully
match the classic finishing; mechanical inn/racetrack sources were incomplete
for final colored layers. No matching damage/construction group was verified.
Those frames keep existing fallbacks. See RECOVERED-RUNTIME.md for exact recipes
and observed differences; source bytes remain unchanged.

## Area marker migration and compositing audit

All 24 guard/clearing/forbidden animation frames now use native original images.
Hospital/tower direct-versus-split GIMP tests show only rounding differences; the
smaller tower XCF reproduces classic visible team pixels and alpha exactly. Larger crystal finishing
remains unresolved, so those building fallbacks stay unchanged. See the runtime
recipe guide and compositing-audit manifest for reproducible evidence.

## Papyrus and remaining sources

Five original papyrus growth layers now replace resources 20–24, bringing
original-source coverage to 60 frames. See RECOVERED-RUNTIME.md for the exact
42 remaining resource frame IDs and UI/water audit. Older gold UI artwork and
classic-size water/cloud variants are retained as references. Cursor scenes
remain unaudited. No new imagery arrived after the two archives: Stéphane
reported that Cyrille found no additional files in PR comment 5591994941.
