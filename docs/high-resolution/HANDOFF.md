# Superseded combined PR

The work is now split into three stacked draft PRs based on current master:

1. [#218 renderer and zoom](https://github.com/Globulation2/glob2/pull/218)
   Worktree `/Users/bradley/glob2-hd-foundation`, branch `codex/hd-renderer-zoom`.
2. [#219 original artwork](https://github.com/Globulation2/glob2/pull/219)
   Worktree `/Users/bradley/glob2-hd-originals`, branch `codex/hd-original-artwork`.
3. [#220 AI artwork](https://github.com/Globulation2/glob2/pull/220)
   Worktree `/Users/bradley/glob2-hd-ai`, branch `codex/hd-ai-artwork`.

Continue work in the appropriate split branch. This combined branch/PR #207 is
retained for the earlier discussion and comparisons, not further implementation.
The final combined runtime PNGs remain unchanged. Historical experiments remain
excluded. See the split PRs for validation and remaining limitations.

---

# Project handoff

## Current branch and decisions

PR https://github.com/Globulation2/glob2/pull/207
Branch `codex/high-resolution-artwork-zoom`, worktree `/Users/bradley/glob2-hd-pr`.
Use `git log -3` for the current head. Main `/Users/bradley/glob2` is separate.

User priorities: original artwork first, minimum AI, retain approved upscales
where usable originals are unavailable. Keep historical experiments out of Git;
keep this PR small and focused on final game assets. Preserve artist originals
byte-for-byte. No unsupported claims about who edited historical artwork.
Units remain with PR #201. HD defaults on with classic still selectable pending
review. Zoom repeats the toroidal map to fill the viewport.

## Completed

487 runtime frames / 546 layers; 60 original-source frames (trees, wheat,
papyrus, hive, flags, construction, school, two racetracks and area markers).
236 preserved source/reference/helper files in datasrc/gfx.
Approved finals are physically separated by origin in datasrc/gfx/production.
Normal packaging uses only these inputs. Runtime remains data/highres/v1;
classic fallback remains data/gfx. Historical AI experiments are untracked and
ignored; they are not needed to build or validate the pack.
See README.md, ASSET-PROVENANCE.md and datasrc/gfx/RECOVERED-RUNTIME.md.

## Remaining

- Playtest latest originals in a newly loaded session; preserve any open user game.
- Controlled profiling: recent dense 50% timing was 97.7 ms HD / 27.7 ms classic,
  slower than previous local runs. Cause unresolved; do not claim readiness.
- Broader platform validation and reviewer feedback; coordinate #198 window work.
- Hospital/tower finishing and damaged/construction combinations remain unresolved.
  Direct/split GIMP audit ruled out layer splitting as the major appearance cause.
  Smaller tower source matches classic visible team pixels, but adds no resolution.
- No verified larger replacements for ripe wheat 14/19, alternative papyrus25–29,
  stones30–39, algae40–49, fruit50–64; keep current fallbacks.
- Recovered UI has an older gold style; water/cloud sources are classic-size512².
- Cursor Blender dependency/render audit pending. Blender2.34 is available (below).
- Await terrain details from Luc; missing walls and tower-2-tex.png remain gaps.

## Tools and checks

```sh
python3 tools/artwork/package_runtime.py --check
python3 tools/artwork/package_runtime.py
python3 tools/artwork/validate_runtime.py
python3 tools/artwork/runtime_provenance.py --check
python3 tools/artwork/pr_comparisons.py
scons release=1 -j8 highres-integration-test
```

Local Pillow/NumPy/SciPy Python:
`/Users/bradley/glob2-ai-upscale-experiment/.cache/ai-upscale/venv/bin/python`.
GIMP2.10: `/Applications/GIMP.app/Contents/MacOS/gimp-console`.
Original exports under tools/artwork write derived staging files; they do not
automatically replace approved production inputs. Review/promote first.
GIMP exports must bake single-layer opacity over transparent backing.

`test/RuntimePackCheck.cpp` is the all-frame/all-hue GL test. Existing local
binary `/tmp/glob2-world-pack-check` predates the capture-path cleanup; rebuild
from current source when needed. Captures now go to `.cache/highres-runtime-check/`.
Gameplay/editor/replay integration and 171 CppUnit tests previously pass; raw
results in validation.txt. Native UI automation was unreliable; use actual
engine handler harnesses. No new native UI dependency is needed.

Blender2.34 Linux i386 static:
`/tmp/globule-blender/blender-2.34-linux-glibc2.2.5-i386-static/blender`.
Docker container `glob2-sprite-render` mounts that directory at `/work`.
Verified version command:

```sh
docker exec glob2-sprite-render sh -c 'LD_LIBRARY_PATH=/opt/legacy/usr/lib qemu-i386 /work/blender-2.34-linux-glibc2.2.5-i386-static/blender -v'
```

The unit worktree's tools/unit-animation/README.md documents reproduction:
`/Users/bradley/glob2-unit-animation-32/`. Use2.34, not2.79, to preserve rendering.
Do not disturb unit jobs; use independent scene copies/staging.

## Correspondence

Stéphane `stephane@magnenat.net`; both archives already imported:
https://h.magnenat.net/~steph/glob2-highres.zip
https://h.magnenat.net/~steph/glob2-highres-more.zip
User authorized thanks and the missing-source questions by email/PR.
Sent Gmail message1a082bc6668fdf53.
Shortlist: PR comment5591525249. Latest checked reply5591994941: Cyrille found
no more files, requests original/upscaled provenance (now folder-separated).
Luc was contacted about terrain. No further imagery found at last check.

Local caches, credentials, Docker setup and unsaved game state do not transfer
with Git. No scheduled follow-up is configured.
