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
pack. `experiments/ai-upscale/` holds the earlier experiment and is not the
original-source archive. Organizing these files does not change current runtime
artwork; original-source replacements still need export and visual validation.

## Provenance and preservation

Stéphane supplied [glob2-highres.zip](https://h.magnenat.net/~steph/glob2-highres.zip)
in [PR #207](https://github.com/Globulation2/glob2/pull/207#issuecomment-5590914680).
He recovered the files and also included the original building artist's concept
art; supplying the archive is not an attribution of sole authorship. Existing
embedded credits and file contents remain unchanged.

The import accounts for all 158 archive file entries. Exact duplicate bytes
reuse one source file. The 95 files already tracked in the repository are also
preserved. Together these form 202 unique source, concept, reference and helper
files. The ZIP is retained locally in `.cache/original-art/`, outside version
control; its URL and SHA-256 are recorded, and all original file contents are
committed in this tree.

- [Archive path → repository path and source hashes](provenance/stephane-2026-09-08.json)
- [Previous repository path → new path and hashes](provenance/repository-paths.json)
- [Machine-readable combined catalog](provenance/catalog.json)

Verify preserved bytes without image tooling:

```sh
python3 tools/artwork/import_originals.py
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
