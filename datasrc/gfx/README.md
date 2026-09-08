# Original artwork

This is the shared home for Globulation 2's original artwork, combining the
repository's existing sources with the archive recovered by Stéphane Magnenat
on 8 September 2026. Start with the [browsable catalog](CATALOG.md) and
[visually verified building map](BUILDING-MAP.md).

```
originals/
  buildings/          Layered artwork by family/level; existing Blender models
  units/              Original Blender animation sources
  resources/          Tree, wheat and historical wheat variants
  terrain/            Layered water and papyrus sources
  ui/                 Controls, arrows and headset artwork
  cursors/            Blender cursor models
  overlays/           Guard, harvest and forbidden-area sources
concept-art/
  buildings/          Original design drawings and concept paintings
reference-exports/    Historical PNG exports; preserve separately from sources
  buildings/          Recovered hive, flags, construction renders and direction templates
  resources/          Tree and both wheat styles
  terrain/water/      Historical water and cloud variants
  ui/                 Historical interface graphics
  overlays/           Area-marker raster references
tools/units/          Historical rendered-frame renaming helper
provenance/           Source URLs, archive paths, old repository paths and hashes
```

## Preserve first, render second

- Keep editable originals byte-for-byte. Render/export into separate working
  directories, then package verified outputs under `data/highres/`.
- Prefer deterministic exports from original sources. Keep the current experimental upscale as the HD fallback
  when a usable original source is missing; retain the classic runtime frame
  for classic/software mode and load failures. Report missing frames,
  insufficient resolution and layer/alignment problems before proposing new AI work.
- Preserve historical basenames and group verified buildings by family/level. Concept artwork
  guides style; it is not interchangeable with an aligned game sprite.
- Do not resave old Blender/GIMP files just to upgrade their file format. Work
  from copies, keep layer roles intact and record reproducible export settings.
- The archive supplies historical variants, not an approved mapping for every
  building state. See [coverage and open questions](COVERAGE.md).

`data/gfx/` remains the classic runtime artwork. `data/highres/` is the runtime
pack. Historical experiments are excluded from Git and are not the
original-source archive. Fifty-five frames (ten from the second archive, ten trees, eight wheat frames, three layered buildings and five papyrus and 24 area markers from the first) replace
experimental art in the runtime pack. See [export recipes and coverage](RECOVERED-RUNTIME.md).
Other layered buildings and remaining resources still need runtime migration; the two final ripe wheat color states still lack larger originals.

## Provenance and preservation

Stéphane supplied [glob2-highres.zip](https://h.magnenat.net/~steph/glob2-highres.zip)
in [PR #207](https://github.com/Globulation2/glob2/pull/207#issuecomment-6090914680).
He recovered the files and also included the original building artist's concept
art; supplying the archive is not an attribution of sole authorship. Existing
embedded credits and file contents remain unchanged.

The import accounts for all 158 archive file entries. Exact duplicate bytes
reuse one source file. The 95 files already tracked in the repository are also
preserved. Together these form 202 unique source, concept, reference and helper
files from the first import. The second archive adds 34 artwork files, for a
combined 236 files; its 15 AppleDouble filesystem metadata entries are documented
but not committed as artwork. The ZIP is retained locally in `.cache/original-art/`, outside version
control; its URL and SHA-256 are recorded, and all original file contents are
committed in this tree.

- [Archive path → repository path and source hashes](provenance/stephane-2026-09-08.json)
- [Second archive provenance and exclusions](provenance/stephane-2026-09-08-more.json)
- [Previous repository path → new path and hashes](provenance/repository-paths.json)
- [Machine-readable combined catalog](provenance/catalog.json)

Verify preserved bytes without image tooling:

```sh
python3 tools/artwork/import_originals.py
python3 tools/artwork/import_more_originals.py
```

Verify against a downloaded archive, or regenerate the catalog with Pillow:

```sh
python3 tools/artwork/import_originals.py .cache/original-art/glob2-highres.zip
python3 tools/artwork/catalog_originals.py
```

## Existing workflow migration

Blender units moved from `datasrc/gfx/globules/` to `datasrc/gfx/originals/units/`.
Building models moved to `datasrc/gfx/originals/buildings/`; cursor models moved
to `datasrc/gfx/originals/cursors/`. All historical basenames remain unchanged.
Scripts outside this branch, including work associated with unit animation
PR #201, should update their source directory using the path manifest above.
Unit rendering work remains coordinated with that PR.

The old `rename_globules.py` helper lives in `tools/units/` within this tree.
It operates on its current working directory; run it only in a disposable render
output directory, not in the source archive. Its historical behavior is unchanged.

## Second recovered archive

Stéphane supplied [glob2-highres-more.zip](https://h.magnenat.net/~steph/glob2-highres-more.zip)
in [this PR comment](https://github.com/Globulation2/glob2/pull/207#issuecomment-6091064207).
The historical PNGs are under `reference-exports/buildings/`, grouped as swarm,
flags, construction and direction templates. White/black-background renders and
separate matte files are preserved unchanged. They require transparency extraction
before runtime use; the 32-pixel direction templates are references, not new HD art.

[Current per-frame original/upscaled inventory](../../docs/high-resolution/ASSET-PROVENANCE.md).

## Production directory separation

Approved runtime inputs are now physically separated under `production/`:
`original-derived/`, `ai-upscaled/`, `ai-materials/`, and `resampled-masks/`.
Prebuilt mixed atlases and runtime metadata have separate directories there.
`derived/` contains intermediate deterministic exports from originals only.
Historical trials are excluded from Git. Normal packaging uses approved inputs only. See [production workflow](production/README.md).
