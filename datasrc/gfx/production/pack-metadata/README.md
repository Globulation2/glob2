# High-resolution runtime pack

2287 registered frames combine approved original-derived artwork, 1,792
unit poses rendered from preserved Blender sources, eight SVG farm markers,
AI-enhanced sprite finals, generated terrain/water materials and resampled masks.
Native sprites remain in `data/gfx`; logical sizes, team colors and animation
cadence are preserved. Unsupported backends and missing frames use classic art.

Approved inputs live in `datasrc/gfx/production`; package them with
`tools/artwork/package_runtime.py`. `manifest.json` records provenance and hashes;
`frames.txt` is the runtime lookup. Terrain/resource atlases contain padded mip
levels matching these frames. See `docs/assets/high-resolution/README.md`,
`tools/artwork/ai/README.md`, and `tools/unit-animation/README.md` for maintenance.

This directory is an approved PNG source pack, not a playable runtime asset tree.
Client builds export WebP through tools/package_assets.py and rewrite frames.txt
with WebP filenames. Lossless exports retain every RGBA pixel; release exports
select the smaller permitted lossless or Q90 WebP image with exact alpha and
geometry. The PNG manifest hashes describe sources, not encoded WebP bytes.
The HD terrain atlas covers 272 legacy connected tiles. Catalogue materials
(`terrain-<name>N`) ship standalone 4x frames rendered by terrain_synth.py from
the same source the classic tiles are downsampled from; ice and cobblestone use
native fallback.

Related: [artwork production](../../../../docs/assets/artwork-workflow.md).
