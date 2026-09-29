# Original artwork pack

Part 2 of 3, based on the renderer/zoom PR. This adds the organized artist
originals and 60 verified original-derived runtime frames, with no AI assets.
Other frames retain classic artwork. Partial resource coverage uses standalone
HD textures; full resource/terrain atlases are supplied by the next pack.

Includes both hives, 3 flags, 5 construction frames, 10 trees, 8 wheat, 5 papyrus,
completed middle school, first two racetracks and 24 area markers.

Build: `python3 tools/artwork/package_runtime.py`.
Validate: `python3 tools/artwork/validate_runtime.py` and the original family
validators under tools/artwork. Sources and staging exports are preserved in
datasrc/gfx; production/original-derived contains approved final layers.
AI fallbacks discussed in source audits are planned for part 3, not included here.
