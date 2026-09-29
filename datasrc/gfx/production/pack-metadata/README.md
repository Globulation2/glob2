# High-resolution runtime pack

1852 registered frames, including 1,792 unit poses across seven animation sets.
Unit textures render onto a fixed 128x128 pixel canvas from the preserved Blender originals, without AI
(4x for the 32px-native explorer set, ~3.37x for the 38px-native worker sets, 3.2x for the 40px-native warrior sets).
Native-resolution sprites remain in `data/gfx`; logical geometry, team colors and animation timing are preserved.

Approved inputs live in `datasrc/gfx/production`; package them with `tools/artwork/package_runtime.py`.
See `manifest.json` for all frame/layer/source hashes, `tools/unit-animation/README.md` for unit render recipes,
and `datasrc/gfx/RECOVERED-RUNTIME.md` for world-art exports.
