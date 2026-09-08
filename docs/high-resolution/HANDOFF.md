# High-resolution artwork and map zoom — handoff

Last updated 2026-09-08. The user resumed work after the subscription handoff. Resume from this document
and repository state.

## Location and review

- Repository: https://github.com/Globulation2/glob2
- Draft PR: https://github.com/Globulation2/glob2/pull/207
- Branch: `codex/high-resolution-artwork-zoom`
- Local experiment/PR worktree: `/Users/bradley/glob2-hd-pr`
- Main checkout `/Users/bradley/glob2` is separate; do not work there accidentally.
- Last prior commit: `75a7897a3` (24 original markers + building compositing audit).
  The commit containing this handoff adds five papyrus originals. Use `git log -3`
  and the PR head for its hash; this document intentionally avoids a self-reference.
- Changes/exports in this worktree are the assistant's work. The user explicitly
  said they did not edit anything. Do not infer who historically edited artwork.

## User decisions

Prefer recovered original artwork and minimum AI. Retain reviewed experimental
upscales where a usable original is unavailable. Preserve source files exactly.
Do not invent historical explanations for differences between XCFs and game PNGs.
High-resolution graphics default on in OpenGL; respect saved classic preferences.
Keep classic selectable pending developer discussion on the PR.
Units, including death animations, belong with animation PR #201.
Before/after visuals must compare classic enlarged to the same dimensions as the
final runtime output. Do not fill the PR with intermediate experiments.

## Implemented engine behavior

Manifest-driven HD sprites retain original logical dimensions/anchors and classic
CPU/software surfaces. Complete frame fallback rejects missing/invalid layers.
OpenGL uses true higher-resolution textures, normalized texture coordinates,
linear filtering, padded terrain/resource atlas mips, and lazy team-color caches.
Gameplay, replays and editor share a presentation-only camera with 50–300% zoom,
Alt+wheel pointer anchoring, picking conversions, wrapped rendering and culling.
The toroidal map repeats to fill the viewport (superseding the initial plan's
single-period centering). Gameplay/replay zoom controls sit in the right sidebar
footer. Software stays at 100%. Cursor Retina sizing and minimap seams were fixed.
No zoom/artwork state enters simulation orders, replay commands or saves.

## Artwork state

236 preserved source/reference/helper files are cataloged in `datasrc/gfx/`.
Two recovered archives and pre-existing Blender/GIMP/vector sources are organized
under originals, reference-exports, concept-art and provenance. Derived runtime
exports live separately under derived; classic `data/gfx` files are untouched.
Catalog/source hashes preserve provenance; manifests record original native
canvas sizes, selected layers, recipes and source/output hashes.

Pack: **487 logical frames / 546 layer PNGs** at `data/highres/v1/`.
**60 frames now use original sources, without new AI:**

- 2 hive states, 3 flags, 5 construction sprites (recovered-v1).
- 10 tree frames (trees-v1), 8 wheat frames (wheat-v1).
- School `school1b0` and racetracks `racetrack0b0`, `racetrack1b0` (buildings-v1).
- 24 area-marker animation frames (markers-v1).
- 5 papyrus growth frames `ressource20`–`24` (papyrus-v1).

A uniform 4× pack canvas does not imply 4× native detail. Some original buildings
supply only 1.33×–2×; school supplies up to 8×. Native sizes are recorded.
The other 427 frames include AI upscales, generated terrain materials and
non-AI mask resampling; do not label all of them AI-generated.
See `RECOVERED-RUNTIME.md` and `COVERAGE.md` under datasrc/gfx for exact limits.

Terrain uses connected periodic materials and shared rugged transition masks,
with 91,136 directed joins checked across four mips. The user approved subdued
grass/water after several revisions; avoid returning to visually busy materials.
Pool fallback remains locked. School now deliberately uses recovered original.

## Remaining work, in priority order

1. Review/playtest the latest originals in a newly loaded game/editor session.
2. Investigate remaining building finishing only when a useful source or recipe
   exists. Hospital/tower and mechanical inn/racetrack candidates do not reproduce
   the final classic appearance. No damage/construction layer groups are verified.
3. Audit cursor Blender scene dependencies/renderability separately. UI saved
   composites are an older gold style; brush icons are classic-size. Water/cloud
   recovered PNGs are 512×512, equal to classic; not additional native detail.
4. Remaining 42 resource frames have no verified matching larger sources:
   ripe wheat 14/19, alternative papyrus 25–29, stones 30–39, algae 40–49,
   fruit 50–64. Preserve current fallbacks rather than substituting another style.
5. Await terrain information from Luc and any recovered walls / `tower-2-tex.png`.
6. Developer review and broader platform/performance validation before merge.
   Mac local validation is extensive; it is NOT cross-platform acceptance.
   Coordinate renderer/window overlaps with #198 and unit work with #201.

Hospital/tower audit: direct full composites versus separate base/team exports
show only <=3/255 rounding differences. Smaller `tower1.xcf` exactly matches
classic visible team RGB and all alpha; invisible RGB is ignored. Larger
`tower2.xcf` differs in finishing. This rules out our layer split as the large
appearance discrepancy, without proving the files' history. Sources untouched.
Audit code and JSON evidence are committed under tools/artwork and provenance.

## Correspondence

Stéphane: `stephane@magnenat.net`. User authorized thank-you/missing-artwork email
and PR replies. Email sent: Gmail message `1a082bc6668fdf53`.
Archives (already imported; do not import duplicates):
- https://h.magnenat.net/~steph/glob2-highres.zip
- https://h.magnenat.net/~steph/glob2-highres-more.zip
Shortlist: PR comment https://github.com/Globulation2/glob2/pull/207#issuecomment-5591525249
Latest checked reply: https://github.com/Globulation2/glob2/pull/207#issuecomment-5591994941
Cyrille found no more files. Stéphane requests original/upscaled provenance;
current per-frame manifests already retain recipes/hashes. Luc was contacted
about terrain. No newer artwork/email reply found at last check.

## Reproduction and checks

Local Python with Pillow/NumPy/SciPy:
`/Users/bradley/glob2-ai-upscale-experiment/.cache/ai-upscale/venv/bin/python`
GIMP 2.10: `/Applications/GIMP.app/Contents/MacOS/gimp-console`.
From the PR worktree, papyrus native export:

```sh
gimp-console -n -i -d -f -c --batch-interpreter=python-fu-eval \
  -b 'execfile("tools/artwork/export_papyrus_gimp.py")' -b 'pdb.gimp_quit(0)'
python3 tools/artwork/export_papyrus.py
python3 experiments/ai-upscale/export_runtime.py
python3 tools/artwork/validate_papyrus.py
python3 experiments/ai-upscale/validate_runtime.py
python3 experiments/ai-upscale/pr_comparisons.py
```

Other export/validate groups: recovered, trees, wheat, buildings, markers.
Native derived PNGs are committed; ordinary runtime pack rebuilding does not need
GIMP, models or new AI. The game itself only needs the committed runtime pack.
GIMP single-layer PNG export may ignore layer opacity unless composited over an
added transparent layer; current building/papyrus tools account for this.
Papyrus has one growth stage per layer, selected explicitly; do not merge all five.

Engine checks used locally:

```sh
build/src/HighResolutionIntegrationHarness
/tmp/glob2-world-pack-check
/tmp/glob2-world-pack-check software
```

The /tmp executable is a local convenience, not portable build infrastructure;
consult `experiments/ai-upscale/HIGH-RESOLUTION-RUNTIME.md` for build details and
`docs/high-resolution/validation.txt` for recorded results. Previous full CppUnit
suite: 171/171 passing. Latest marker integration covers gameplay/editor/replay,
Alt-wheel order isolation, simulation checksums, all hues and resource release.
The papyrus batch reruns pack, five-stage registration, all-frame GL and software
checks; no C++ changes. Actual GL captures are in ignored
`experiments/ai-upscale/runtime-check/`. Native UI automation was unreliable;
engine integration harness drives actual handlers. Do not kill an open user game
or discard unsaved state to take screenshots. Assets load on the next session.

Performance caveat: all-487/all-16-hue stress reaches ~856 MB CPU and ~1.50 GB GPU
with 944 colored frames, stable on repetition and released on session close.
Dense four-team HD is ~358 MB GPU. Timing varies with machine load; retain raw
measurements, not claims of cross-platform performance or historical baselines.

## Review artifacts

- `docs/high-resolution/README.md`: overall design and pipeline.
- `docs/high-resolution/COMPARISONS.md`: all final before/after frames.
- `docs/high-resolution/images/`: selected comparisons and actual game captures.
- `docs/high-resolution/validation.txt`: recorded checks.
- `datasrc/gfx/RECOVERED-RUNTIME.md`: original-source recipes and gaps.
- `datasrc/gfx/provenance/`: catalog, mappings, hashes and building audit.

After switching subscriptions, clone/fetch the branch if needed and read this
file first. Local caches, /tmp binaries, app credentials and unsaved game sessions
are not transferred by Git. No scheduled follow-up is configured by this handoff.

## Resumed audit

`ASSET-PROVENANCE.md` now lists every active frame by recipe category and links
original source paths/native dimensions. Generate/check it with
`tools/artwork/runtime_provenance.py` / `--check`; unknown recipes fail closed.
Counts: 60 original-source, 116 constrained AI upscales, 273 generated-material
frames, 38 non-AI mask resamples. Source/output hashes are validated.
The latest gameplay/editor/replay integration passes with the papyrus pack.
This run recorded 97.7 ms HD / 27.7 ms classic at dense 50% on the current Mac;
that is materially slower than prior local runs. Do not dismiss it or claim
performance readiness: repeat controlled profiling before merge to distinguish
machine conditions from renderer cost. No renderer or runtime asset changes
were made during this resumed inventory batch.
Cursor render audit remains pending, but the renderer has now been located:
Blender 2.34 Linux i386 static lives under
`/tmp/globule-blender/blender-2.34-linux-glibc2.2.5-i386-static/blender`.
The running Docker container `glob2-sprite-render` contains `/opt/blender/blender`
and the legacy libraries; invoke with
`LD_LIBRARY_PATH=/opt/legacy/usr/lib qemu-i386 /opt/blender/blender`.
Reproduction/dependency details are in the unit PR worktree:
`/Users/bradley/glob2-unit-animation-32/tools/unit-animation/README.md`.
That work used 2.34 deliberately: 2.79 changes projection and surface rendering.
A 2.79 Mac copy also exists under `/tmp/globule-blender/` but is not the preferred
renderer. The earlier app/cache search missed this /tmp + Docker setup.
Do not claim cursor dependencies or render compatibility are verified yet.
Do not disturb running unit jobs; use separate staging paths and scene copies.
No new PR imagery replies were present.
