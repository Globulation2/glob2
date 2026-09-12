# Original artwork pack

The pack contains the organized artist originals, 60 verified original-derived
world frames, and 1,792 unit animation poses rendered from the original Blender
rigs. No AI assets are included.
Other frames retain classic artwork. Partial resource coverage uses standalone
HD textures; full resource/terrain atlases are supplied by the next pack.

World artwork includes both hives, 3 flags, 5 construction frames, 10 trees, 8 wheat, 5 papyrus,
completed middle school, first two racetracks and 24 area markers.

Build: `python3 tools/artwork/package_runtime.py`.
Validate: `python3 tools/artwork/validate_runtime.py` and the original family
validators under tools/artwork. Sources and staging exports are preserved in
datasrc/gfx; production/original-derived contains approved final layers.
AI fallbacks discussed in source audits are planned for part 3, not included here.

Unit textures render onto a fixed 128×128 pixel canvas (4× for the 32px-native
explorer set, ~3.37× and 3.2× for the 38px/40px-native worker and warrior sets),
while the native 32-pose sprites remain in `data/gfx`. This preserves logical sprite size,
32 poses per direction and the normal 25 FPS display cadence. The seven sets
cover explorer flight, worker walk/swim/harvest-build, and warrior walk/swim/fight.
The classic artwork setting and software backend retain native unit textures.
See [the unit pipeline](../../tools/unit-animation/README.md) for reproducible
render settings, layer mapping, CPU limits and validation.
